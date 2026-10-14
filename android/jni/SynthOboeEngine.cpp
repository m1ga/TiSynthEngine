#include <jni.h>
#include <oboe/Oboe.h>

#include <algorithm>
#include <array>
#include <atomic>
#include <chrono>
#include <cmath>
#include <cstdint>
#include <memory>
#include <mutex>
#include <thread>
#include <vector>

#include "../../shared/synth_core.h"
#include "../../shared/synth_spatial.h"

namespace {
constexpr int kMaxVoices = 32;
constexpr int kMaxSteps = 32;
constexpr int kMaxWavetableSamples = 64;
constexpr int kQueueCapacity = 128;
// Mirrors SynthThread.EXTRA_*: decay ms, sustain, filter type, cutoff, cutoff
// end, Q, FM ratio, FM depth, FM depth end (NaN = no sweep), spatial enabled, x, y.
constexpr int kExtraCount = 12;
constexpr double kPi = 3.14159265358979323846;

template <typename T> T clampValue(T value, T low, T high) {
  return std::max(low, std::min(high, value));
}

double smoothStep(double x) {
  const double t = clampValue(x, 0.0, 1.0);
  return t * t * (3.0 - 2.0 * t);
}

double polyBlep(double t, double dt) {
  if (t < dt) {
    const double x = t / dt;
    return x + x - x * x - 1.0;
  }
  if (t > 1.0 - dt) {
    const double x = (t - 1.0) / dt;
    return x * x + x + x + 1.0;
  }
  return 0.0;
}

double periodicWave(int type, double phase, double increment, double pulseWidth) {
  const double t = phase - std::floor(phase);
  const double dt = clampValue(std::abs(increment), 1.0e-9, 0.5);
  if (type == 1) {
    const double width = clampValue(pulseWidth, 0.05, 0.95);
    double value = t < width ? 1.0 : -1.0;
    value += polyBlep(t, dt);
    double shifted = t - width;
    if (shifted < 0.0)
      shifted += 1.0;
    return value - polyBlep(shifted, dt);
  }
  if (type == 2)
    return 2.0 * t - 1.0 - polyBlep(t, dt);
  if (type == 3)
    return 1.0 - 4.0 * std::abs(t - 0.5);
  return std::sin(t * 2.0 * kPi);
}

double quantizeWave(double value, int bitDepth) {
  if (bitDepth < 2)
    return value;
  const int safeDepth = clampValue(bitDepth, 2, 16);
  const double levels = static_cast<double>((1u << safeDepth) - 1u);
  const double normalized = clampValue((value + 1.0) * 0.5, 0.0, 1.0);
  return std::round(normalized * levels) / levels * 2.0 - 1.0;
}

double wavetableWave(double phase, const std::array<double, kMaxWavetableSamples> &table,
                     int tableSize) {
  if (tableSize < 2)
    return std::sin((phase - std::floor(phase)) * 2.0 * kPi);
  const double t = phase - std::floor(phase);
  const int index = std::min(tableSize - 1, static_cast<int>(t * tableSize));
  return table[index];
}

double eventGain(int profile, int waveType, int voiceCount) {
  if (profile == 0)
    return 1.0;
  constexpr double sineGain = 0.44;
  double gain = waveType == 1   ? sineGain / std::sqrt(2.0)
                : waveType == 0 ? sineGain
                                : sineGain * std::sqrt(1.5);
  if (profile == 2)
    gain *= std::sqrt(2.0);
  else if (profile == 3)
    gain /= std::sqrt(2.0);
  return gain / std::sqrt(static_cast<double>(std::max(1, voiceCount)));
}

struct Command {
  bool spatial = false;
  double spatialX = 0.0, spatialY = 0.0;
  int type = 0;
  double frequency = 440.0;
  int duration = 1, attack = 0, release = 0;
  double volume = 1.0;
  int eventVoiceCount = 1, waveType = 0;
  double pan = 0.0, panEnd = 0.0, frequencyEnd = 0.0;
  double lfoFreq = 0.0, lfoDepth = 0.0;
  double noiseRate = 0.0, noiseRateEnd = 0.0, lowPassHz = 0.0;
  double pulseWidth = 0.5, pulseWidthEnd = 0.0;
  int bitDepth = 0, wavetableSize = 0;
  std::array<double, kMaxWavetableSamples> wavetable{};
  std::array<double, kMaxSteps> steps{};
  int stepCount = 0, stepDurationMs = 100;
  int decay = 0;
  double sustain = 1.0;
  int filterType = SYNTH_FILTER_NONE;
  double filterHz = 1000.0, filterHzEnd = 0.0, filterQ = 0.7071;
  double fmRatio = 1.0, fmDepth = 0.0, fmDepthEnd = 0.0;
  bool fmDepthSweep = false;
};

struct Voice {
  bool spatial = false;
  SynthSpatial spatialState{};
  int spatialDrain = 0;
  bool active = false;
  double phase = 0.0, increment = 0.0, incrementDelta = 0.0;
  int duration = 1, played = 0, attack = 0, release = 0;
  double volume = 0.0, envelope = 0.0;
  int waveType = 0;
  double pan = 0.0, panDelta = 0.0, left = 0.70710678, right = 0.70710678;
  double lfoPhase = 0.0, lfoIncrement = 0.0, lfoDepth = 0.0;
  double noisePhase = 0.0, noiseIncrement = 1.0, noiseIncrementDelta = 0.0;
  double heldNoise = 0.0, filterState = 0.0, filterAlpha = 0.0;
  double pulseWidth = 0.5, pulseWidthDelta = 0.0;
  int bitDepth = 0, wavetableSize = 0;
  std::array<double, kMaxWavetableSamples> wavetable{};
  std::array<double, kMaxSteps> steps{};
  int stepCount = 0, step = 0, stepSamples = 1, samplesInStep = 0;
  uint64_t noise = 1;
  int decay = 0;
  double sustain = 1.0;
  SynthFilter filter{};
  double filterHz = 0.0, filterHzEnd = 0.0, filterQ = 0.7071;
  bool fmActive = false;
  double fmRatio = 1.0, fmDepth = 0.0, fmDepthDelta = 0.0, modPhase = 0.0;
};

struct Tail {
  bool spatial = false;
  SynthSpatial spatialState{};
  int spatialDrain = 0;
  bool active = false;
  double phase = 0.0, increment = 0.0, volume = 0.0;
  int waveType = 0;
  double left = 0.0, right = 0.0;
  uint64_t noise = 1;
  double noisePhase = 0.0, noiseIncrement = 1.0, heldNoise = 0.0;
  double filterState = 0.0, filterAlpha = 0.0, pulseWidth = 0.5;
  int bitDepth = 0, wavetableSize = 0;
  std::array<double, kMaxWavetableSamples> wavetable{};
  int total = 1, remaining = 0;
  SynthFilter filter{};
  bool fmActive = false;
  double fmRatio = 1.0, fmDepth = 0.0, modPhase = 0.0;
};

class Engine final : public oboe::AudioStreamDataCallback,
                     public oboe::AudioStreamErrorCallback,
                     public std::enable_shared_from_this<Engine> {
public:
  Engine(int maxVoices, int profile)
      : maxVoices_(clampValue(maxVoices, 1, kMaxVoices)),
        profile_(clampValue(profile, 0, 3)), voices_(maxVoices_),
        tails_(maxVoices_) {}

  bool open() {
    std::lock_guard<std::mutex> guard(streamMutex_);
    return openLocked();
  }

  bool start() {
    std::lock_guard<std::mutex> guard(streamMutex_);
    if (closing_.load(std::memory_order_acquire))
      return false;
    runningRequested_.store(true, std::memory_order_release);
    if (startLocked())
      return true;
    closeStreamLocked();
    return reopenAndStartLocked();
  }

  int sampleRate() const {
    return reportedSampleRate_.load(std::memory_order_relaxed);
  }
  void setVolume(double value) { master_.store(clampValue(value, 0.0, 2.0)); }

  bool enqueue(Command command) {
    std::lock_guard<std::mutex> guard(producerMutex_);
    const uint32_t write = write_.load(std::memory_order_relaxed);
    const uint32_t next = (write + 1) % kQueueCapacity;
    if (next == read_.load(std::memory_order_acquire))
      return true;
    queue_[write] = command;
    write_.store(next, std::memory_order_release);
    return false;
  }

  bool enqueueChord(std::vector<Command> commands) {
    std::lock_guard<std::mutex> guard(producerMutex_);
    uint32_t write = write_.load(std::memory_order_relaxed);
    const uint32_t read = read_.load(std::memory_order_acquire);
    const uint32_t used = (write + kQueueCapacity - read) % kQueueCapacity;
    if (commands.size() > kQueueCapacity - 1 - used)
      return true;
    for (const Command &command : commands) {
      queue_[write] = command;
      write = (write + 1) % kQueueCapacity;
    }
    write_.store(write, std::memory_order_release);
    return false;
  }

  bool stopAll() {
    Command command;
    command.type = 2;
    return enqueue(command);
  }

  void close() {
    runningRequested_.store(false, std::memory_order_release);
    closing_.store(true, std::memory_order_release);
    std::lock_guard<std::mutex> guard(streamMutex_);
    closeStreamLocked();
  }

  oboe::DataCallbackResult onAudioReady(oboe::AudioStream *, void *audioData,
                                        int32_t frames) override {
    processCommands();
    auto *output = static_cast<float *>(audioData);
    for (int32_t frame = 0; frame < frames; ++frame) {
      double left = 0.0, right = 0.0;
      for (Voice &voice : voices_)
        renderVoice(voice, left, right);
      for (Tail &tail : tails_)
        renderTail(tail, left, right);
      limit(left, right);
      output[frame * 2] = static_cast<float>(clampValue(outputL_, -1.0, 1.0));
      output[frame * 2 + 1] =
          static_cast<float>(clampValue(outputR_, -1.0, 1.0));
    }
    return oboe::DataCallbackResult::Continue;
  }

  void onErrorAfterClose(oboe::AudioStream *failedStream,
                         oboe::Result error) override {
    if (error != oboe::Result::ErrorDisconnected ||
        closing_.load(std::memory_order_acquire) ||
        !runningRequested_.load(std::memory_order_acquire))
      return;

    std::lock_guard<std::mutex> guard(streamMutex_);
    if (closing_.load(std::memory_order_acquire) ||
        !runningRequested_.load(std::memory_order_acquire))
      return;
    if (stream_ && stream_.get() != failedStream)
      return;

    // Oboe has already stopped and closed the disconnected stream before this
    // callback. Drop in-flight voices because their sample counts and phase
    // increments belong to the previous route's sample rate. Commands queued
    // while the route settles remain available to the new stream.
    stream_.reset();
    clearRenderState();
    reopenAndStartLocked();
  }

private:
  bool openLocked() {
    oboe::AudioStreamBuilder builder;
    const std::shared_ptr<Engine> engine = shared_from_this();
    builder.setDirection(oboe::Direction::Output)
        ->setFormat(oboe::AudioFormat::Float)
        ->setChannelCount(2)
        ->setPerformanceMode(oboe::PerformanceMode::LowLatency)
        ->setSharingMode(oboe::SharingMode::Exclusive)
        ->setUsage(oboe::Usage::Game)
        ->setDataCallback(
            std::static_pointer_cast<oboe::AudioStreamDataCallback>(engine))
        ->setErrorCallback(
            std::static_pointer_cast<oboe::AudioStreamErrorCallback>(engine));
    std::shared_ptr<oboe::AudioStream> newStream;
    oboe::Result result = builder.openStream(newStream);
    if (result != oboe::Result::OK) {
      builder.setSharingMode(oboe::SharingMode::Shared);
      result = builder.openStream(newStream);
    }
    if (result != oboe::Result::OK || !newStream)
      return false;
    renderSampleRate_ = newStream->getSampleRate();
    if (renderSampleRate_ <= 0)
      renderSampleRate_ = 48000;
    reportedSampleRate_.store(renderSampleRate_, std::memory_order_relaxed);
    const int burst = newStream->getFramesPerBurst();
    if (burst > 0)
      newStream->setBufferSizeInFrames(burst * 2);
    initializeLimiter();
    stream_ = std::move(newStream);
    return true;
  }

  bool startLocked() {
    return stream_ && stream_->requestStart() == oboe::Result::OK;
  }

  void closeStreamLocked() {
    if (stream_) {
      stream_->stop();
      stream_->close();
      stream_.reset();
    }
  }

  bool reopenAndStartLocked() {
    constexpr int kRestartAttempts = 4;
    for (int attempt = 0; attempt < kRestartAttempts; ++attempt) {
      if (closing_.load(std::memory_order_acquire) ||
          !runningRequested_.load(std::memory_order_acquire))
        return false;
      if (attempt > 0)
        std::this_thread::sleep_for(std::chrono::milliseconds(20));
      if (openLocked() &&
          !closing_.load(std::memory_order_acquire) && startLocked())
        return true;
      closeStreamLocked();
    }
    return false;
  }

  void clearRenderState() {
    for (Voice &voice : voices_)
      voice = Voice{};
    for (Tail &tail : tails_)
      tail = Tail{};
  }

  int toSamples(int ms) const {
    return std::max(1, static_cast<int>(ms * renderSampleRate_ / 1000.0));
  }

  void initializeLimiter() {
    lookahead_ = std::max(1, static_cast<int>(renderSampleRate_ * 0.010));
    delayL_.assign(lookahead_, 0.0);
    delayR_.assign(lookahead_, 0.0);
    peakValues_.assign(lookahead_ + 1, 0.0);
    peakIndices_.assign(lookahead_ + 1, 0);
    delayIndex_ = peakHead_ = peakCount_ = 0;
    sampleIndex_ = 0;
    limiterGain_ = smoothedMaster_ = 1.0;
    outputL_ = outputR_ = 0.0;
    releaseCoeff_ = 1.0 - std::exp(-1.0 / (0.100 * renderSampleRate_));
  }

  void processCommands() {
    uint32_t read = read_.load(std::memory_order_relaxed);
    const uint32_t write = write_.load(std::memory_order_acquire);
    while (read != write) {
      const Command command = queue_[read];
      if (command.type == 1)
        startVoice(command);
      else if (command.type == 2) {
        for (Voice &voice : voices_) {
          if (voice.active)
            snapshotTail(voice, 20);
          voice.active = false;
        }
      }
      read = (read + 1) % kQueueCapacity;
    }
    read_.store(read, std::memory_order_release);
  }

  void startVoice(const Command &command) {
    Voice *voice = nullptr;
    int leastRemaining = INT32_MAX;
    for (Voice &candidate : voices_) {
      if (!candidate.active) {
        voice = &candidate;
        break;
      }
      const int remaining = candidate.duration - candidate.played;
      if (remaining < leastRemaining) {
        leastRemaining = remaining;
        voice = &candidate;
      }
    }
    if (!voice)
      return;
    if (voice->active)
      snapshotTail(*voice, 6);
    *voice = Voice{};
    voice->active = true;
    voice->spatial = command.spatial;
    if (voice->spatial) {
      synth_spatial_init(&voice->spatialState, command.spatialX, command.spatialY, renderSampleRate_);
      voice->spatialDrain = voice->spatialState.taps - 1;
    }
    voice->duration = std::max(1, command.duration);
    voice->attack = clampValue(command.attack, 0, voice->duration - 1);
    voice->release =
        clampValue(command.release, 0, voice->duration - voice->attack);
    voice->waveType = clampValue(command.waveType, 0, 5);
    voice->volume =
        clampValue(command.volume, 0.0, 2.0) *
        eventGain(profile_, voice->waveType, command.eventVoiceCount);
    voice->pan = clampValue(command.pan, -1.0, 1.0);
    updatePan(*voice);
    voice->increment =
        clampValue(command.frequency, 1.0, renderSampleRate_ * 0.45) /
        renderSampleRate_;
    if (command.frequencyEnd >= 1.0 && voice->duration > 1) {
      voice->incrementDelta =
          (command.frequencyEnd / renderSampleRate_ - voice->increment) /
          (voice->duration - 1);
    }
    const double panEnd = clampValue(command.panEnd, -1.0, 1.0);
    if (voice->duration > 1)
      voice->panDelta = (panEnd - voice->pan) / (voice->duration - 1);
    voice->lfoDepth = clampValue(command.lfoDepth, 0.0, 20000.0);
    voice->lfoIncrement =
        clampValue(command.lfoFreq, 0.0, 20000.0) / renderSampleRate_;
    const double noiseRate = command.noiseRate > 0.0
                                 ? clampValue(command.noiseRate, 1.0,
                                              renderSampleRate_ * 0.45)
                                 : renderSampleRate_;
    voice->noiseIncrement = noiseRate / renderSampleRate_;
    if (command.noiseRateEnd >= 1.0 && voice->duration > 1) {
      voice->noiseIncrementDelta =
          (clampValue(command.noiseRateEnd, 1.0, renderSampleRate_ * 0.45) /
               renderSampleRate_ -
           voice->noiseIncrement) /
          (voice->duration - 1);
    }
    const double cutoff = clampValue(command.lowPassHz, 0.0,
                                     renderSampleRate_ * 0.45);
    voice->filterAlpha = cutoff > 0.0
                             ? 1.0 - std::exp(-2.0 * kPi * cutoff / renderSampleRate_)
                             : 0.0;
    voice->pulseWidth = clampValue(command.pulseWidth, 0.05, 0.95);
    if (command.pulseWidthEnd >= 0.05 && voice->duration > 1) {
      voice->pulseWidthDelta =
          (clampValue(command.pulseWidthEnd, 0.05, 0.95) - voice->pulseWidth) /
          (voice->duration - 1);
    }
    voice->decay =
        clampValue(command.decay, 0, std::max(0, voice->duration - voice->attack));
    voice->sustain = clampValue(command.sustain, 0.0, 1.0);
    synth_filter_init(&voice->filter, clampValue(command.filterType, 0, 3));
    if (voice->filter.type != SYNTH_FILTER_NONE) {
      const double maxHz = renderSampleRate_ * 0.45;
      voice->filterHz = clampValue(command.filterHz, SYNTH_FILTER_MIN_HZ, maxHz);
      voice->filterHzEnd = command.filterHzEnd >= SYNTH_FILTER_MIN_HZ
                               ? clampValue(command.filterHzEnd, SYNTH_FILTER_MIN_HZ, maxHz)
                               : 0.0;
      voice->filterQ = command.filterQ;
      synth_filter_set(&voice->filter, voice->filterHz, voice->filterQ,
                       renderSampleRate_);
    }
    voice->fmActive =
        voice->waveType == 0 && (command.fmDepth > 0.0 || command.fmDepthSweep);
    if (voice->fmActive) {
      voice->fmRatio = clampValue(command.fmRatio, 0.1, 16.0);
      voice->fmDepth = clampValue(command.fmDepth, 0.0, 10.0);
      if (command.fmDepthSweep && voice->duration > 1) {
        voice->fmDepthDelta =
            (clampValue(command.fmDepthEnd, 0.0, 10.0) - voice->fmDepth) /
            (voice->duration - 1);
      }
    }
    voice->bitDepth = command.bitDepth == 0 ? 0 : clampValue(command.bitDepth, 2, 16);
    voice->wavetableSize = clampValue(command.wavetableSize, 0, kMaxWavetableSamples);
    voice->wavetable = command.wavetable;
    voice->stepCount = clampValue(command.stepCount, 0, kMaxSteps);
    voice->steps = command.steps;
    voice->stepSamples = toSamples(command.stepDurationMs);
    if (voice->stepCount > 0)
      voice->increment = voice->steps[0] / renderSampleRate_;
    voice->noise = reinterpret_cast<uintptr_t>(voice) ^ sampleIndex_;
    voice->noise = voice->noise * 6364136223846793005ULL + 1442695040888963407ULL;
    voice->heldNoise = static_cast<int32_t>(voice->noise >> 32) / 2147483648.0;
  }

  void updatePan(Voice &voice) {
    const double angle = (voice.pan + 1.0) * kPi * 0.25;
    voice.left = std::cos(angle);
    voice.right = std::sin(angle);
  }

  void snapshotTail(const Voice &voice, int milliseconds) {
    Tail *tail = nullptr;
    for (Tail &candidate : tails_) {
      if (!candidate.active) {
        tail = &candidate;
        break;
      }
      if (!tail || candidate.remaining < tail->remaining)
        tail = &candidate;
    }
    if (!tail)
      return;
    tail->active = true;
    tail->spatial = voice.spatial;
    if (tail->spatial) {
      tail->spatialState = voice.spatialState;
      tail->spatialDrain = tail->spatialState.taps - 1;
    }
    tail->phase = voice.phase;
    tail->increment = clampValue(voice.increment, -0.45, 0.45);
    tail->volume = voice.volume * clampValue(voice.envelope, 0.0, 1.0);
    tail->waveType = voice.waveType;
    tail->left = voice.left;
    tail->right = voice.right;
    tail->noise = voice.noise;
    tail->noisePhase = voice.noisePhase;
    tail->noiseIncrement = voice.noiseIncrement;
    tail->heldNoise = voice.heldNoise;
    tail->filterState = voice.filterState;
    tail->filterAlpha = voice.filterAlpha;
    tail->pulseWidth = voice.pulseWidth;
    tail->bitDepth = voice.bitDepth;
    tail->wavetableSize = voice.wavetableSize;
    tail->wavetable = voice.wavetable;
    tail->filter = voice.filter;
    tail->fmActive = voice.fmActive;
    tail->fmRatio = voice.fmRatio;
    tail->fmDepth = voice.fmDepth;
    tail->modPhase = voice.modPhase;
    tail->total = toSamples(milliseconds);
    tail->remaining = tail->total;
  }

  void renderTail(Tail &tail, double &left, double &right) {
    if (!tail.active)
      return;
    if (tail.spatial && tail.remaining <= 0) {
      float l, r;
      synth_spatial_process(&tail.spatialState, 0, &l, &r);
      left += l; right += r;
      if (--tail.spatialDrain <= 0) tail.active = false;
      return;
    }
    double wave;
    if (tail.waveType == 4) {
      tail.noisePhase += tail.noiseIncrement;
      if (tail.noisePhase >= 1.0) {
        tail.noisePhase -= std::floor(tail.noisePhase);
        tail.noise = tail.noise * 6364136223846793005ULL + 1442695040888963407ULL;
        tail.heldNoise = static_cast<int32_t>(tail.noise >> 32) / 2147483648.0;
      }
      wave = tail.heldNoise;
    } else if (tail.waveType == 5) {
      wave = wavetableWave(tail.phase, tail.wavetable, tail.wavetableSize);
    } else if (tail.fmActive) {
      wave = synth_fm_sine(tail.phase, tail.modPhase, tail.fmDepth);
    } else {
      wave = periodicWave(tail.waveType, tail.phase, tail.increment, tail.pulseWidth);
    }
    wave = quantizeWave(wave, tail.bitDepth);
    if (tail.filterAlpha > 0.0) {
      tail.filterState += tail.filterAlpha * (wave - tail.filterState);
      wave = tail.filterState;
    }
    wave = synth_filter_process(&tail.filter, wave);
    const double fade = smoothStep(static_cast<double>(tail.remaining - 1) /
                                   std::max(1, tail.total - 1));
    const double sample = wave * tail.volume * fade;
    if (tail.spatial) {
      float spatialL, spatialR;
      synth_spatial_process(&tail.spatialState, static_cast<float>(sample), &spatialL, &spatialR);
      left += spatialL; right += spatialR;
    } else {
      left += sample * tail.left;
      right += sample * tail.right;
    }
    tail.phase += tail.increment;
    tail.phase -= std::floor(tail.phase);
    if (tail.fmActive) {
      tail.modPhase += tail.increment * tail.fmRatio;
      tail.modPhase -= std::floor(tail.modPhase);
    }
    if (--tail.remaining <= 0 && !tail.spatial)
      tail.active = false;
  }

  void renderVoice(Voice &voice, double &left, double &right) {
    if (!voice.active)
      return;
    if (voice.spatial && voice.played >= voice.duration) {
      float l, r;
      synth_spatial_process(&voice.spatialState, 0, &l, &r);
      left += l; right += r;
      if (--voice.spatialDrain <= 0) voice.active = false;
      return;
    }
    if (voice.stepCount > 0 && ++voice.samplesInStep >= voice.stepSamples) {
      voice.samplesInStep = 0;
      voice.step = (voice.step + 1) % voice.stepCount;
      voice.increment = voice.steps[voice.step] / renderSampleRate_;
    }
    double increment = voice.increment;
    if (voice.lfoDepth > 0.0) {
      increment +=
          std::sin(voice.lfoPhase * 2.0 * kPi) * voice.lfoDepth /
          renderSampleRate_;
      voice.lfoPhase += voice.lfoIncrement;
      if (voice.lfoPhase >= 1.0)
        voice.lfoPhase -= 1.0;
    }
    increment = clampValue(increment, -0.45, 0.45);
    double wave;
    if (voice.waveType == 4) {
      voice.noisePhase += clampValue(voice.noiseIncrement, 1.0e-9, 1.0);
      if (voice.noisePhase >= 1.0) {
        voice.noisePhase -= std::floor(voice.noisePhase);
        voice.noise =
            voice.noise * 6364136223846793005ULL + 1442695040888963407ULL;
        voice.heldNoise = static_cast<int32_t>(voice.noise >> 32) / 2147483648.0;
      }
      wave = voice.heldNoise;
    } else if (voice.waveType == 5) {
      wave = wavetableWave(voice.phase, voice.wavetable, voice.wavetableSize);
    } else if (voice.fmActive) {
      wave = synth_fm_sine(voice.phase, voice.modPhase, voice.fmDepth);
    } else {
      wave = periodicWave(voice.waveType, voice.phase, increment, voice.pulseWidth);
    }
    wave = quantizeWave(wave, voice.bitDepth);
    if (voice.filterAlpha > 0.0) {
      voice.filterState += voice.filterAlpha * (wave - voice.filterState);
      wave = voice.filterState;
    }
    if (voice.filter.type != SYNTH_FILTER_NONE) {
      if (voice.filterHzEnd > 0.0 && synth_filter_update_due(1, voice.played)) {
        synth_filter_set(&voice.filter,
                         synth_geometric_at(voice.filterHz, voice.filterHzEnd,
                                            synth_sweep_progress(voice.played, voice.duration)),
                         voice.filterQ, renderSampleRate_);
      }
      wave = synth_filter_process(&voice.filter, wave);
    }
    const double envelope =
        synth_envelope(voice.played, voice.duration, voice.attack, voice.decay,
                       voice.sustain, voice.release);
    voice.envelope = envelope;
    const double sample = wave * envelope * voice.volume;
    if (voice.spatial) {
      float spatialL, spatialR;
      synth_spatial_process(&voice.spatialState, static_cast<float>(sample), &spatialL, &spatialR);
      left += spatialL; right += spatialR;
    } else {
      left += sample * voice.left;
      right += sample * voice.right;
    }
    voice.phase += increment;
    voice.phase -= std::floor(voice.phase);
    if (voice.fmActive) {
      voice.modPhase += increment * voice.fmRatio;
      voice.modPhase -= std::floor(voice.modPhase);
      voice.fmDepth = clampValue(voice.fmDepth + voice.fmDepthDelta, 0.0, 10.0);
    }
    voice.increment += voice.incrementDelta;
    voice.noiseIncrement += voice.noiseIncrementDelta;
    voice.pulseWidth = clampValue(voice.pulseWidth + voice.pulseWidthDelta, 0.05, 0.95);
    if (voice.panDelta != 0.0) {
      voice.pan = clampValue(voice.pan + voice.panDelta, -1.0, 1.0);
      updatePan(voice);
    }
    if (++voice.played >= voice.duration) {
      if (voice.spatial) voice.envelope = 0;
      else voice.active = false;
    }
  }

  void limit(double left, double right) {
    const double targetMaster = master_.load(std::memory_order_relaxed);
    smoothedMaster_ += (targetMaster - smoothedMaster_) / lookahead_;
    left *= smoothedMaster_;
    right *= smoothedMaster_;
    const int index = delayIndex_;
    const double delayedL = delayL_[index], delayedR = delayR_[index];
    delayL_[index] = left;
    delayR_[index] = right;
    delayIndex_ = (index + 1) % lookahead_;

    const int capacity = lookahead_ + 1;
    const int64_t oldest = static_cast<int64_t>(sampleIndex_) - lookahead_;
    while (peakCount_ > 0 && peakIndices_[peakHead_] < oldest) {
      peakHead_ = (peakHead_ + 1) % capacity;
      --peakCount_;
    }
    const double peak = std::max(std::abs(left), std::abs(right));
    while (peakCount_ > 0) {
      const int last = (peakHead_ + peakCount_ - 1) % capacity;
      if (peakValues_[last] > peak)
        break;
      --peakCount_;
    }
    const int insert = (peakHead_ + peakCount_) % capacity;
    peakValues_[insert] = peak;
    peakIndices_[insert] = sampleIndex_++;
    ++peakCount_;
    const double windowPeak = peakValues_[peakHead_];
    const double ceiling = profile_ == 3 ? 0.70 : 0.85;
    const double targetGain = windowPeak > ceiling ? ceiling / windowPeak : 1.0;
    if (targetGain < limiterGain_)
      limiterGain_ += (targetGain - limiterGain_) / lookahead_;
    else
      limiterGain_ = std::min(targetGain, limiterGain_ + (1.0 - limiterGain_) *
                                                             releaseCoeff_);
    outputL_ = delayedL * limiterGain_;
    outputR_ = delayedR * limiterGain_;
  }

  int maxVoices_, profile_, renderSampleRate_ = 48000;
  std::atomic<int> reportedSampleRate_{48000};
  std::vector<Voice> voices_;
  std::vector<Tail> tails_;
  std::shared_ptr<oboe::AudioStream> stream_;
  std::mutex streamMutex_;
  std::atomic<bool> runningRequested_{false};
  std::atomic<bool> closing_{false};
  std::array<Command, kQueueCapacity> queue_{};
  std::atomic<uint32_t> read_{0}, write_{0};
  std::mutex producerMutex_;
  std::atomic<double> master_{1.0};
  std::vector<double> delayL_, delayR_, peakValues_;
  std::vector<int64_t> peakIndices_;
  int lookahead_ = 1, delayIndex_ = 0, peakHead_ = 0, peakCount_ = 0;
  uint64_t sampleIndex_ = 0;
  double releaseCoeff_ = 0.0, limiterGain_ = 1.0, smoothedMaster_ = 1.0;
  double outputL_ = 0.0, outputR_ = 0.0;
};

using EngineHandle = std::shared_ptr<Engine>;

EngineHandle fromHandle(jlong handle) {
  return *reinterpret_cast<EngineHandle *>(handle);
}

Command makeCommand(Engine *engine, double frequency, int durationMs,
                    int attackMs, int releaseMs, double volume, int waveType,
                    double pan, double panEnd, double frequencyEnd,
                    double lfoFreq, double lfoDepth, double noiseRate,
                    double noiseRateEnd, double lowPassHz, double pulseWidth,
                    double pulseWidthEnd, int bitDepth) {
  Command command;
  command.type = 1;
  command.frequency = frequency;
  command.duration =
      std::max(1, static_cast<int>(durationMs * engine->sampleRate() / 1000.0));
  command.attack =
      std::max(0, static_cast<int>(attackMs * engine->sampleRate() / 1000.0));
  command.release =
      std::max(0, static_cast<int>(releaseMs * engine->sampleRate() / 1000.0));
  command.volume = volume;
  command.waveType = waveType;
  command.pan = pan;
  command.panEnd = panEnd;
  command.frequencyEnd = frequencyEnd;
  command.lfoFreq = lfoFreq;
  command.lfoDepth = lfoDepth;
  command.noiseRate = noiseRate;
  command.noiseRateEnd = noiseRateEnd;
  command.lowPassHz = lowPassHz;
  command.pulseWidth = pulseWidth;
  command.pulseWidthEnd = pulseWidthEnd;
  command.bitDepth = bitDepth;
  return command;
}

void applyExtras(JNIEnv *env, Command &command, jdoubleArray extras,
                 int sampleRate) {
  double v[kExtraCount] = {0.0, 1.0, 0.0, 1000.0, 0.0, 0.7071, 1.0, 0.0, NAN};
  if (extras) {
    const int count = std::min<int>(env->GetArrayLength(extras), kExtraCount);
    env->GetDoubleArrayRegion(extras, 0, count, v);
  }
  command.decay = std::max(0, static_cast<int>(v[0] * sampleRate / 1000.0));
  command.sustain = v[1];
  command.filterType = static_cast<int>(v[2]);
  command.filterHz = v[3];
  command.filterHzEnd = v[4];
  command.filterQ = v[5];
  command.fmRatio = v[6];
  command.fmDepth = v[7];
  command.fmDepthSweep = !std::isnan(v[8]);
  command.fmDepthEnd = command.fmDepthSweep ? v[8] : 0.0;
  command.spatial = v[9] != 0.0;
  command.spatialX = v[10];
  command.spatialY = v[11];
}
} // namespace

extern "C" JNIEXPORT jlong JNICALL Java_ti_synthengine_SynthThread_nativeCreate(
    JNIEnv *, jclass, jint voices, jint profile) {
  auto engine = std::make_shared<Engine>(voices, profile);
  if (!engine->open())
    return 0;
  return reinterpret_cast<jlong>(new EngineHandle(std::move(engine)));
}
extern "C" JNIEXPORT jint JNICALL
Java_ti_synthengine_SynthThread_nativeGetSampleRate(JNIEnv *, jclass,
                                                    jlong handle) {
  return fromHandle(handle)->sampleRate();
}
extern "C" JNIEXPORT jboolean JNICALL
Java_ti_synthengine_SynthThread_nativeStart(JNIEnv *, jclass, jlong handle) {
  return fromHandle(handle)->start();
}
extern "C" JNIEXPORT void JNICALL
Java_ti_synthengine_SynthThread_nativeSetMasterVolume(JNIEnv *, jclass,
                                                      jlong handle,
                                                      jdouble value) {
  fromHandle(handle)->setVolume(value);
}
extern "C" JNIEXPORT jboolean JNICALL
Java_ti_synthengine_SynthThread_nativeEnqueueNote(
    JNIEnv *env, jclass, jlong handle, jdouble frequency, jint duration,
    jint attack, jint release, jdouble volume, jint waveType, jdouble pan,
    jdouble panEnd, jdouble frequencyEnd, jdouble lfoFreq, jdouble lfoDepth,
    jdouble noiseRate, jdouble noiseRateEnd, jdouble lowPassHz,
    jdouble pulseWidth, jdouble pulseWidthEnd, jint bitDepth,
    jdoubleArray wavetable, jdoubleArray steps, jint stepCount, jint stepMs,
    jdoubleArray extras) {
  EngineHandle engine = fromHandle(handle);
  Command command =
      makeCommand(engine.get(), frequency, duration, attack, release, volume,
                  waveType, pan, panEnd, frequencyEnd, lfoFreq, lfoDepth,
                  noiseRate, noiseRateEnd, lowPassHz, pulseWidth, pulseWidthEnd,
                  bitDepth);
  if (wavetable) {
    const int count = std::min<int>(env->GetArrayLength(wavetable),
                                    kMaxWavetableSamples);
    env->GetDoubleArrayRegion(wavetable, 0, count, command.wavetable.data());
    command.wavetableSize = count;
  }
  if (steps && stepCount > 0) {
    const int count =
        std::min<int>({stepCount, kMaxSteps, env->GetArrayLength(steps)});
    env->GetDoubleArrayRegion(steps, 0, count, command.steps.data());
    command.stepCount = count;
    command.stepDurationMs = stepMs;
  }
  applyExtras(env, command, extras, engine->sampleRate());
  return engine->enqueue(command);
}
extern "C" JNIEXPORT jboolean JNICALL
Java_ti_synthengine_SynthThread_nativeEnqueueChord(
    JNIEnv *env, jclass, jlong handle, jdoubleArray frequencies, jint duration,
    jint attack, jint release, jdouble volume, jint waveType,
    jdoubleArray pans, jdouble noiseRate, jdouble lowPassHz,
    jdouble pulseWidth, jint bitDepth, jdoubleArray wavetable,
    jdoubleArray extras) {
  if (!frequencies)
    return true;
  EngineHandle engine = fromHandle(handle);
  const int count = std::min<int>(env->GetArrayLength(frequencies), kMaxVoices);
  if (count <= 0)
    return true;
  std::vector<double> freq(count), pan(count, 0.0);
  env->GetDoubleArrayRegion(frequencies, 0, count, freq.data());
  if (pans)
    env->GetDoubleArrayRegion(
        pans, 0, std::min<int>(count, env->GetArrayLength(pans)), pan.data());
  std::vector<Command> commands;
  commands.reserve(count);
  for (int i = 0; i < count; ++i) {
    Command command =
        makeCommand(engine.get(), freq[i], duration, attack, release, volume,
                    waveType, pan[i], pan[i], 0.0, 0.0, 0.0, noiseRate, 0.0,
                    lowPassHz, pulseWidth, 0.0, bitDepth);
    if (wavetable) {
      const int tableCount = std::min<int>(env->GetArrayLength(wavetable),
                                           kMaxWavetableSamples);
      env->GetDoubleArrayRegion(wavetable, 0, tableCount, command.wavetable.data());
      command.wavetableSize = tableCount;
    }
    applyExtras(env, command, extras, engine->sampleRate());
    command.eventVoiceCount = count;
    commands.push_back(command);
  }
  return engine->enqueueChord(std::move(commands));
}
extern "C" JNIEXPORT jboolean JNICALL
Java_ti_synthengine_SynthThread_nativeStopAll(JNIEnv *, jclass, jlong handle) {
  return fromHandle(handle)->stopAll();
}
extern "C" JNIEXPORT void JNICALL
Java_ti_synthengine_SynthThread_nativeDestroy(JNIEnv *, jclass, jlong handle) {
  std::unique_ptr<EngineHandle> engineHandle(
      reinterpret_cast<EngineHandle *>(handle));
  (*engineHandle)->close();
}
