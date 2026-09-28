package ti.synthengine;

import android.util.Log;

import org.appcelerator.kroll.KrollDict;
import org.appcelerator.kroll.KrollModule;
import org.appcelerator.kroll.annotations.Kroll;

import java.util.HashSet;
import java.util.Set;
import java.util.concurrent.Executors;
import java.util.concurrent.ScheduledExecutorService;
import java.util.concurrent.TimeUnit;

@Kroll.module(name = "TiSynthEngine", id = "ti.synthengine")
public class TiSynthEngineModule extends KrollModule {

    private static final String TAG = "TiSynthEngine";

    private static final int MAX_VOICES_LIMIT = SynthContract.MAX_MAX_VOICES;

    private static final double DEFAULT_FREQUENCY_HZ = SynthContract.DEFAULT_FREQUENCY_HZ;

    private static final int DURATION_DEFAULT_MS = SynthContract.DEFAULT_DURATION_MS;
    private static final int DURATION_MIN_MS = SynthContract.MIN_DURATION_MS;
    private static final int DURATION_MAX_MS = SynthContract.MAX_DURATION_MS;

    private static final int ATTACK_DEFAULT_MS = SynthContract.DEFAULT_ATTACK_MS;
    private static final int ATTACK_MIN_MS = SynthContract.MIN_ATTACK_MS;
    private static final int ATTACK_MAX_MS = SynthContract.MAX_ATTACK_MS;

    private static final int RELEASE_DEFAULT_MS = SynthContract.DEFAULT_RELEASE_MS;
    private static final int RELEASE_MIN_MS = SynthContract.MIN_RELEASE_MS;
    private static final int RELEASE_MAX_MS = SynthContract.MAX_RELEASE_MS;

    private static final double VOLUME_DEFAULT = SynthContract.DEFAULT_VOLUME;
    private static final double VOLUME_MIN = SynthContract.MIN_VOLUME;
    private static final double VOLUME_MAX = SynthContract.MAX_VOLUME;

    private static final double MIN_FREQUENCY_HZ = SynthContract.MIN_FREQUENCY_HZ;
    private static final int PATTERN_MAX_STEPS = SynthContract.MAX_PATTERN_STEPS;

    private volatile SynthThread synthThread;
    private ScheduledExecutorService patternScheduler;
    private final Set<String> warnedKeys = new HashSet<>();

    public TiSynthEngineModule() {
        super();
    }

    // -------------------------------------------------------------------------
    // Public API
    // -------------------------------------------------------------------------

    @Kroll.method
    public KrollDict getDefaults() {
        KrollDict defaults = new KrollDict();
        defaults.put("maxVoices", SynthContract.DEFAULT_MAX_VOICES);
        defaults.put("mixingProfile", SynthContract.DEFAULT_MIXING_PROFILE);
        defaults.put("audioSession", SynthContract.DEFAULT_AUDIO_SESSION);
        defaults.put("frequency", DEFAULT_FREQUENCY_HZ);
        defaults.put("duration", DURATION_DEFAULT_MS);
        defaults.put("attack", ATTACK_DEFAULT_MS);
        defaults.put("release", RELEASE_DEFAULT_MS);
        defaults.put("volume", VOLUME_DEFAULT);
        defaults.put("waveType", SynthContract.DEFAULT_WAVE_TYPE);
        defaults.put("pan", SynthContract.DEFAULT_PAN);
        defaults.put("panEnd", SynthContract.DEFAULT_PAN);
        defaults.put("frequencyEnd", SynthContract.DEFAULT_FREQUENCY_END_HZ);
        defaults.put("lfoFreq", SynthContract.DEFAULT_LFO_FREQUENCY_HZ);
        defaults.put("lfoDepth", SynthContract.DEFAULT_LFO_DEPTH);
        defaults.put("noiseRate", SynthContract.DEFAULT_NOISE_RATE_HZ);
        defaults.put("noiseRateEnd", SynthContract.DEFAULT_NOISE_RATE_HZ);
        defaults.put("lowPassHz", SynthContract.DEFAULT_LOW_PASS_HZ);
        defaults.put("pulseWidth", SynthContract.DEFAULT_PULSE_WIDTH);
        defaults.put("pulseWidthEnd", 0.0);
        defaults.put("waveTable", new Object[]{});
        defaults.put("bitDepth", SynthContract.DEFAULT_BIT_DEPTH);
        defaults.put("panSpread", SynthContract.DEFAULT_PAN_SPREAD);
        defaults.put("bpm", SynthContract.DEFAULT_BPM);
        defaults.put("swing", SynthContract.DEFAULT_SWING);
        defaults.put("stepDuration", SynthContract.DEFAULT_STEP_DURATION_MS);

        KrollDict limits = new KrollDict();
        limits.put("maxVoices", range(SynthContract.MIN_MAX_VOICES, MAX_VOICES_LIMIT));
        limits.put("frequency", range(MIN_FREQUENCY_HZ, maxFrequencyHz()));
        limits.put("duration", range(DURATION_MIN_MS, DURATION_MAX_MS));
        limits.put("attack", range(ATTACK_MIN_MS, ATTACK_MAX_MS));
        limits.put("release", range(RELEASE_MIN_MS, RELEASE_MAX_MS));
        limits.put("volume", range(VOLUME_MIN, VOLUME_MAX));
        limits.put("waveType", range(SynthContract.MIN_WAVE_TYPE, SynthContract.MAX_WAVE_TYPE));
        limits.put("patternSteps", range(1, PATTERN_MAX_STEPS));
        limits.put("pan", range(-1.0, 1.0));
        limits.put("panSpread", range(0.0, 1.0));
        limits.put("lfoFreq", range(0.0, SynthContract.MAX_LFO));
        limits.put("lfoDepth", range(0.0, SynthContract.MAX_LFO));
        limits.put("noiseRate", range(0.0, maxFrequencyHz()));
        limits.put("lowPassHz", range(0.0, maxFrequencyHz()));
        limits.put("pulseWidth", range(SynthContract.MIN_PULSE_WIDTH,
                SynthContract.MAX_PULSE_WIDTH));
        limits.put("waveTableSamples", range(SynthContract.MIN_WAVETABLE_SAMPLES,
                SynthContract.MAX_WAVETABLE_SAMPLES));
        limits.put("bitDepth", range(0, SynthContract.MAX_BIT_DEPTH));
        limits.put("bpm", range(SynthContract.MIN_BPM, SynthContract.MAX_BPM));
        limits.put("swing", range(SynthContract.MIN_SWING, SynthContract.MAX_SWING));
        limits.put("stepDuration", range(DURATION_MIN_MS, DURATION_MAX_MS));

        KrollDict waveTypes = new KrollDict();
        waveTypes.put("SINE", 0);
        waveTypes.put("SQUARE", 1);
        waveTypes.put("SAWTOOTH", 2);
        waveTypes.put("TRIANGLE", 3);
        waveTypes.put("NOISE", 4);
        waveTypes.put("WAVETABLE", 5);

        KrollDict result = new KrollDict();
        result.put("apiVersion", SynthContract.API_VERSION);
        result.put("defaults", defaults);
        result.put("limits", limits);
        result.put("mixingProfiles", new Object[]{"conservative", "raw", "balanced", "speakerSafe"});
        result.put("audioSessions", new Object[]{"playback", "ambient"});
        result.put("waveTypes", waveTypes);
        return result;
    }

    @Kroll.method
    public boolean startEngine(KrollDict options) {
        if (!acceptValidation("startEngine", SynthContract.validateStartEngine(options))) return false;
        if (synthThread != null) return true;
        int maxVoices = readOptionalIntOption(options, "startEngine", "maxVoices",
                SynthContract.DEFAULT_MAX_VOICES, 1, MAX_VOICES_LIMIT);
        int mixingProfile = readMixingProfile(options);
        if (mixingProfile < 0) return false;
        synthThread = new SynthThread(maxVoices, mixingProfile);
        synthThread.start();
        return true;
    }

    @Kroll.method
    public boolean setVolume(KrollDict options) {
        if (synthThread == null) return false;
        if (!acceptValidation("setVolume", SynthContract.validateSetVolume(options))) return false;
        double volume = readOptionalDoubleOption(options, "setVolume", "volume",
                1.0, VOLUME_MIN, VOLUME_MAX);
        synthThread.setMasterVolume(volume);
        return true;
    }

    @Kroll.method
    public boolean playTone(KrollDict options) {
        if (synthThread == null) return false;
        if (options == null) options = new KrollDict();

        double maxFreq = maxFrequencyHz();
        if (!acceptValidation("playTone", SynthContract.validateTone(options, maxFreq))) return false;

        double frequency;
        if (containsKey(options, "note")) {
            Object noteObj = options.get("note");
            frequency = parseNoteToFrequency(noteObj != null ? noteObj.toString() : "");
        } else {
            frequency = readOptionalDoubleOption(options, "playTone", "frequency",
                    DEFAULT_FREQUENCY_HZ, MIN_FREQUENCY_HZ, maxFreq);
        }

        int duration = readOptionalIntOption(options, "playTone", "duration",
                DURATION_DEFAULT_MS, DURATION_MIN_MS, DURATION_MAX_MS);

        int    attack  = readOptionalIntOption(options, "playTone", "attack",
                ATTACK_DEFAULT_MS, ATTACK_MIN_MS, ATTACK_MAX_MS);
        int    release = readOptionalIntOption(options, "playTone", "release",
                RELEASE_DEFAULT_MS, RELEASE_MIN_MS, RELEASE_MAX_MS);
        double volume  = readOptionalDoubleOption(options, "playTone", "volume",
                VOLUME_DEFAULT, VOLUME_MIN, VOLUME_MAX);
        int    waveType= readOptionalIntOption(options, "playTone", "waveType",
                0, 0, SynthContract.MAX_WAVE_TYPE);
        double pan     = readOptionalDoubleOption(options, "playTone", "pan",
                0.0, -1.0, 1.0);
        double panEnd  = readOptionalDoubleOption(options, "playTone", "panEnd",
                pan, -1.0, 1.0);
        double freqEnd = 0.0;
        if (containsKey(options, "frequencyEnd")) {
            Double fe = parseFiniteDouble(options.get("frequencyEnd"));
            if (fe != null && fe >= MIN_FREQUENCY_HZ) {
                freqEnd = clamp(fe, MIN_FREQUENCY_HZ, maxFreq);
            }
        }
        double lfoFreq  = readOptionalDoubleOption(options, "playTone", "lfoFreq",
                0.0, 0.0, 20000.0);
        double lfoDepth = readOptionalDoubleOption(options, "playTone", "lfoDepth",
                0.0, 0.0, 20000.0);
        double noiseRate = readOptionalDoubleOption(options, "playTone", "noiseRate",
                0.0, 0.0, maxFreq);
        double noiseRateEnd = readOptionalDoubleOption(options, "playTone", "noiseRateEnd",
                0.0, 0.0, maxFreq);
        double lowPassHz = readOptionalDoubleOption(options, "playTone", "lowPassHz",
                0.0, 0.0, maxFreq);
        double pulseWidth = readOptionalDoubleOption(options, "playTone", "pulseWidth",
                SynthContract.DEFAULT_PULSE_WIDTH, SynthContract.MIN_PULSE_WIDTH,
                SynthContract.MAX_PULSE_WIDTH);
        double pulseWidthEnd = readOptionalDoubleOption(options, "playTone", "pulseWidthEnd",
                0.0, 0.0, SynthContract.MAX_PULSE_WIDTH);
        int bitDepth = readOptionalIntOption(options, "playTone", "bitDepth",
                0, 0, SynthContract.MAX_BIT_DEPTH);
        double[] waveTable = parseWaveTable(options);

        if (attack >= duration) attack = Math.max(0, duration - 1);
        int maxRelease = Math.max(0, duration - attack);
        if (release > maxRelease) release = maxRelease;

        boolean dropped = synthThread.enqueueNote(
                frequency, duration, attack, release, volume,
                waveType, pan, panEnd, freqEnd, lfoFreq, lfoDepth,
                noiseRate, noiseRateEnd, lowPassHz, pulseWidth, pulseWidthEnd,
                bitDepth, waveTable, null, 0, 100);
        if (dropped) {
            warnOnce("playTone", "queue", "overflow", "command_queue_full", "drop_newest");
        }
        return !dropped;
    }

    @Kroll.method
    public boolean playChord(KrollDict options) {
        if (synthThread == null || options == null) return false;

        double maxFreq = maxFrequencyHz();
        if (!acceptValidation("playChord", SynthContract.validateChord(options, maxFreq))) return false;
        double[] freqs = parseFreqArray(options, "playChord", maxFreq);
        if (freqs == null || freqs.length == 0) return false;

        int duration = readOptionalIntOption(options, "playChord", "duration",
                DURATION_DEFAULT_MS, DURATION_MIN_MS, DURATION_MAX_MS);

        int    attack    = readOptionalIntOption(options, "playChord", "attack",
                ATTACK_DEFAULT_MS, ATTACK_MIN_MS, ATTACK_MAX_MS);
        int    release   = readOptionalIntOption(options, "playChord", "release",
                RELEASE_DEFAULT_MS, RELEASE_MIN_MS, RELEASE_MAX_MS);
        double volume    = readOptionalDoubleOption(options, "playChord", "volume",
                VOLUME_DEFAULT, VOLUME_MIN, VOLUME_MAX);
        int    waveType  = readOptionalIntOption(options, "playChord", "waveType",
                0, 0, SynthContract.MAX_WAVE_TYPE);
        double panSpread = readOptionalDoubleOption(options, "playChord", "panSpread",
                0.0, 0.0, 1.0);
        double noiseRate = readOptionalDoubleOption(options, "playChord", "noiseRate",
                0.0, 0.0, maxFreq);
        double lowPassHz = readOptionalDoubleOption(options, "playChord", "lowPassHz",
                0.0, 0.0, maxFreq);
        double pulseWidth = readOptionalDoubleOption(options, "playChord", "pulseWidth",
                SynthContract.DEFAULT_PULSE_WIDTH, SynthContract.MIN_PULSE_WIDTH,
                SynthContract.MAX_PULSE_WIDTH);
        int bitDepth = readOptionalIntOption(options, "playChord", "bitDepth",
                0, 0, SynthContract.MAX_BIT_DEPTH);
        double[] waveTable = parseWaveTable(options);

        if (attack >= duration) attack = Math.max(0, duration - 1);
        int maxRelease = Math.max(0, duration - attack);
        if (release > maxRelease) release = maxRelease;

        // Optional per-voice pans override
        double[] pansOverride = null;
        if (containsKey(options, "pans")) {
            Object raw = options.get("pans");
            if (raw instanceof Object[]) {
                Object[] arr = (Object[]) raw;
                pansOverride = new double[arr.length];
                for (int i = 0; i < arr.length; i++) {
                    Double v = parseFiniteDouble(arr[i]);
                    pansOverride[i] = v != null ? clamp(v, -1.0, 1.0) : 0.0;
                }
            }
        }

        double[] pans = new double[freqs.length];
        for (int i = 0; i < freqs.length; i++) {
            double pan = 0.0;
            if (pansOverride != null && i < pansOverride.length) {
                pan = pansOverride[i];
            } else if (panSpread > 0.0 && freqs.length > 1) {
                pan = panSpread * (2.0 * i / (freqs.length - 1) - 1.0);
            }
            pans[i] = pan;
        }
        boolean dropped = synthThread.enqueueChord(freqs, duration, attack, release,
                volume, waveType, pans, noiseRate, lowPassHz, pulseWidth,
                bitDepth, waveTable);
        if (dropped) {
            warnOnce("playChord", "queue", "overflow", "command_queue_full", "drop_newest");
        }
        return !dropped;
    }

    @Kroll.method
    public boolean playPattern(KrollDict options) {
        if (synthThread == null || options == null) return false;

        if (!acceptValidation("playPattern",
                SynthContract.validatePattern(options, maxFrequencyHz()))) return false;

        if (patternScheduler != null) {
            patternScheduler.shutdownNow();
            patternScheduler = null;
        }

        Object stepsObj = containsKey(options, "steps") ? options.get("steps") : null;
        if (!(stepsObj instanceof Object[])) return false;
        Object[] steps = (Object[]) stepsObj;
        if (steps.length == 0) return false;
        int stepCount = Math.min(steps.length, PATTERN_MAX_STEPS);
        if (steps.length > PATTERN_MAX_STEPS) {
            warnOnce("playPattern", "steps", "too_many_steps", steps.length,
                    "truncated_to_" + PATTERN_MAX_STEPS);
        }

        Double bpmVal  = parseFiniteDouble(containsKey(options, "bpm")
                ? options.get("bpm") : null);
        double bpm     = (bpmVal != null) ? clamp(bpmVal, 20.0, 300.0) : 120.0;

        Double swingVal = parseFiniteDouble(containsKey(options, "swing")
                ? options.get("swing") : null);
        double swing   = (swingVal != null) ? clamp(swingVal, -0.49, 0.49) : 0.0;

        double quarter = 60000.0 / bpm;
        double maxFreq = maxFrequencyHz();

        Object globalNoteValue = containsKey(options, "noteValue")
                ? options.get("noteValue") : null;
        Double globalStepDuration = parseFiniteDouble(containsKey(options, "stepDuration")
                ? options.get("stepDuration") : null);
        int globalAttack = readOptionalIntOption(options, "playPattern", "attack",
                ATTACK_DEFAULT_MS, ATTACK_MIN_MS, ATTACK_MAX_MS);
        int globalRelease = readOptionalIntOption(options, "playPattern", "release",
                RELEASE_DEFAULT_MS, RELEASE_MIN_MS, RELEASE_MAX_MS);
        double globalVolume = readOptionalDoubleOption(options, "playPattern", "volume",
                VOLUME_DEFAULT, VOLUME_MIN, VOLUME_MAX);
        int globalWaveType = readOptionalIntOption(options, "playPattern", "waveType", 0, 0,
                SynthContract.MAX_WAVE_TYPE);
        double globalPan = readOptionalDoubleOption(options, "playPattern", "pan", 0.0, -1.0, 1.0);
        double globalPanEnd = readOptionalDoubleOption(options, "playPattern", "panEnd",
                globalPan, -1.0, 1.0);
        double globalFrequencyEnd = readOptionalDoubleOption(options, "playPattern", "frequencyEnd",
                0.0, 0.0, maxFreq);
        double globalLfoFreq = readOptionalDoubleOption(options, "playPattern", "lfoFreq",
                0.0, 0.0, 20000.0);
        double globalLfoDepth = readOptionalDoubleOption(options, "playPattern", "lfoDepth",
                0.0, 0.0, 20000.0);
        double globalNoiseRate = readOptionalDoubleOption(options, "playPattern", "noiseRate",
                0.0, 0.0, maxFreq);
        double globalNoiseRateEnd = readOptionalDoubleOption(options, "playPattern", "noiseRateEnd",
                0.0, 0.0, maxFreq);
        double globalLowPassHz = readOptionalDoubleOption(options, "playPattern", "lowPassHz",
                0.0, 0.0, maxFreq);
        double globalPulseWidth = readOptionalDoubleOption(options, "playPattern", "pulseWidth",
                SynthContract.DEFAULT_PULSE_WIDTH, SynthContract.MIN_PULSE_WIDTH,
                SynthContract.MAX_PULSE_WIDTH);
        double globalPulseWidthEnd = readOptionalDoubleOption(options, "playPattern", "pulseWidthEnd",
                0.0, 0.0, SynthContract.MAX_PULSE_WIDTH);
        int globalBitDepth = readOptionalIntOption(options, "playPattern", "bitDepth",
                0, 0, SynthContract.MAX_BIT_DEPTH);
        double[] globalWaveTable = parseWaveTable(options);

        patternScheduler = Executors.newSingleThreadScheduledExecutor();
        final ScheduledExecutorService scheduler = patternScheduler;

        double cumulativeMs = 0.0;
        for (int i = 0; i < stepCount; i++) {
            KrollDict step = normalizePatternStep(steps[i]);
            if (step == null) continue;

            double stepMs = resolvePatternStepMs(
                    step, globalNoteValue, globalStepDuration, quarter);
            stepMs = clamp(stepMs, DURATION_MIN_MS, DURATION_MAX_MS);

            // Swing: even steps lean late, odd steps lean early
            double swingOffsetMs = 0.0;
            if (swing != 0.0) {
                swingOffsetMs = (i % 2 == 0) ? swing * stepMs : -swing * stepMs;
            }

            // Resolve pitch
            double  freq   = 0.0;
            boolean isRest = true;
            boolean explicitRest = containsKey(step, "rest")
                    && Boolean.TRUE.equals(step.get("rest"));
            if (!explicitRest && containsKey(step, "frequency")) {
                Double fv = parseFiniteDouble(step.get("frequency"));
                if (fv != null && fv >= MIN_FREQUENCY_HZ) {
                    freq   = clamp(fv, MIN_FREQUENCY_HZ, maxFreq);
                    isRest = false;
                }
            } else if (!explicitRest && containsKey(step, "note")) {
                Object noteObj = step.get("note");
                if (noteObj != null) {
                    double f = parseNoteToFrequency(noteObj.toString());
                    if (f >= MIN_FREQUENCY_HZ) {
                        freq   = clamp(f, MIN_FREQUENCY_HZ, maxFreq);
                        isRest = false;
                    }
                }
            }

            if (!isRest) {
                int dur = (int) Math.round(stepMs);
                int    attack  = readOptionalIntOption(step, "step", "attack",
                        globalAttack, ATTACK_MIN_MS, ATTACK_MAX_MS);
                int    release = readOptionalIntOption(step, "step", "release",
                        globalRelease, RELEASE_MIN_MS, RELEASE_MAX_MS);
                double vol = readOptionalDoubleOption(step, "step", "volume",
                        globalVolume, VOLUME_MIN, VOLUME_MAX);
                int wt = readOptionalIntOption(step, "step", "waveType", globalWaveType, 0,
                        SynthContract.MAX_WAVE_TYPE);
                double pan = readOptionalDoubleOption(step, "step", "pan",
                        globalPan, -1.0, 1.0);
                double panEnd = readOptionalDoubleOption(step, "step", "panEnd",
                        globalPanEnd, -1.0, 1.0);
                double frequencyEnd = readOptionalDoubleOption(step, "step", "frequencyEnd",
                        globalFrequencyEnd, 0.0, maxFreq);
                double lfoFreq = readOptionalDoubleOption(step, "step", "lfoFreq",
                        globalLfoFreq, 0.0, 20000.0);
                double lfoDepth = readOptionalDoubleOption(step, "step", "lfoDepth",
                        globalLfoDepth, 0.0, 20000.0);
                double noiseRate = readOptionalDoubleOption(step, "step", "noiseRate",
                        globalNoiseRate, 0.0, maxFreq);
                double noiseRateEnd = readOptionalDoubleOption(step, "step", "noiseRateEnd",
                        globalNoiseRateEnd, 0.0, maxFreq);
                double lowPassHz = readOptionalDoubleOption(step, "step", "lowPassHz",
                        globalLowPassHz, 0.0, maxFreq);
                double pulseWidth = readOptionalDoubleOption(step, "step", "pulseWidth",
                        globalPulseWidth, SynthContract.MIN_PULSE_WIDTH,
                        SynthContract.MAX_PULSE_WIDTH);
                double pulseWidthEnd = readOptionalDoubleOption(step, "step", "pulseWidthEnd",
                        globalPulseWidthEnd, 0.0, SynthContract.MAX_PULSE_WIDTH);
                int bitDepth = readOptionalIntOption(step, "step", "bitDepth",
                        globalBitDepth, 0, SynthContract.MAX_BIT_DEPTH);
                double[] waveTable = containsKey(step, "waveTable")
                        ? parseWaveTable(step) : globalWaveTable;

                if (attack >= dur) attack = Math.max(0, dur - 1);
                int maxRel = Math.max(0, dur - attack);
                if (release > maxRel) release = maxRel;

                final double  ff = freq;
                final int     fd = dur;
                final int     fa = attack;
                final int     fr = release;
                final double  fv = vol;
                final int     fw = wt;
                final double  fp = pan;
                final double  fpe = panEnd;
                final double  ffe = frequencyEnd;
                final double  flf = lfoFreq;
                final double  fld = lfoDepth;
                final double  fnr = noiseRate;
                final double  fnre = noiseRateEnd;
                final double  flp = lowPassHz;
                final double  fpw = pulseWidth;
                final double  fpwe = pulseWidthEnd;
                final int     fbd = bitDepth;
                final double[] fwt = waveTable;

                long schedMs = Math.round(cumulativeMs + swingOffsetMs);
                if (schedMs < 0) schedMs = 0;

                scheduler.schedule(() -> {
                    if (synthThread != null) {
                        boolean dropped = synthThread.enqueueNote(ff, fd, fa, fr, fv,
                                fw, fp, fpe, ffe, flf, fld, fnr, fnre, flp,
                                fpw, fpwe, fbd, fwt, null, 0, 100);
                        if (dropped) {
                            warnOnce("playPattern", "queue", "overflow",
                                    "command_queue_full", "drop_newest");
                        }
                    }
                }, schedMs, TimeUnit.MILLISECONDS);
            }

            cumulativeMs += stepMs;
        }

        long shutdownDelayMs = Math.max(1L, (long) Math.ceil(cumulativeMs) + 1L);
        scheduler.schedule(scheduler::shutdown, shutdownDelayMs, TimeUnit.MILLISECONDS);
        return true;
    }

    @Kroll.method
    public void stopAll() {
        if (synthThread == null) return;
        boolean dropped = synthThread.requestStopAll();
        if (dropped) {
            warnOnce("stopAll", "queue", "overflow", "command_queue_full", "fallback_control_flag");
        }
    }

    @Kroll.method
    public void shutdown() {
        if (patternScheduler != null) {
            patternScheduler.shutdownNow();
            patternScheduler = null;
        }
        if (synthThread == null) return;
        boolean dropped = synthThread.requestShutdown();
        if (dropped) {
            warnOnce("shutdown", "queue", "overflow", "command_queue_full", "fallback_control_flag");
        }
        synthThread = null;
    }

    @SuppressWarnings("unchecked")
    private KrollDict normalizePatternStep(Object rawStep) {
        if (rawStep instanceof KrollDict) return (KrollDict) rawStep;
        if (rawStep instanceof java.util.HashMap) {
            return new KrollDict((java.util.HashMap<String, Object>) rawStep);
        }

        return null;
    }

    private double resolvePatternStepMs(KrollDict step, Object globalNoteValue,
            Double globalStepDuration, double quarterMs) {
        if (containsKey(step, "duration")) {
            Double value = parseFiniteDouble(step.get("duration"));
            if (value != null && value > 0.0) return value;
        }
        if (containsKey(step, "noteValue") && step.get("noteValue") != null) {
            return computeNoteValueMs(step.get("noteValue").toString(), quarterMs);
        }
        if (globalNoteValue != null) {
            return computeNoteValueMs(globalNoteValue.toString(), quarterMs);
        }
        if (globalStepDuration != null && globalStepDuration > 0.0) {
            return globalStepDuration;
        }
        return 100.0;
    }

    // -------------------------------------------------------------------------
    // Note name parser
    // Supports: C4, F#4, Bb3, Do4, Fa#4, Sib3 (solfege with optional octave digit)
    // Returns -1.0 on parse failure.
    // -------------------------------------------------------------------------
    static double parseNoteToFrequency(String noteName) {
        if (noteName == null || noteName.isEmpty()) return -1.0;
        String s     = noteName.trim();
        String upper = s.toUpperCase();
        int    pos   = 0;
        int    semitone;

        // Try solfege names (longest match first to avoid "Si" matching "Sol")
        if (upper.startsWith("SOL"))     { semitone = 7;  pos = 3; }
        else if (upper.startsWith("DO")) { semitone = 0;  pos = 2; }
        else if (upper.startsWith("RE")) { semitone = 2;  pos = 2; }
        else if (upper.startsWith("MI")) { semitone = 4;  pos = 2; }
        else if (upper.startsWith("FA")) { semitone = 5;  pos = 2; }
        else if (upper.startsWith("LA")) { semitone = 9;  pos = 2; }
        else if (upper.startsWith("SI")) { semitone = 11; pos = 2; }
        else if (upper.startsWith("TI")) { semitone = 11; pos = 2; }
        else {
            // Single letter names
            pos = 1;
            if (s.isEmpty()) return -1.0;
            switch (upper.charAt(0)) {
                case 'C': semitone = 0;  break;
                case 'D': semitone = 2;  break;
                case 'E': semitone = 4;  break;
                case 'F': semitone = 5;  break;
                case 'G': semitone = 7;  break;
                case 'A': semitone = 9;  break;
                case 'B': semitone = 11; break;
                default:  return -1.0;
            }
        }

        // Accidental: use original string (lowercase 'b' = flat, '#' = sharp)
        if (pos < s.length()) {
            char acc = s.charAt(pos);
            if (acc == '#') { semitone++; pos++; }
            else if (acc == 'b') { semitone--; pos++; }
        }

        // Octave digit (0–9, default 4)
        int octave = 4;
        if (pos < s.length()) {
            char c = s.charAt(pos);
            if (c >= '0' && c <= '9') octave = c - '0';
        }

        int midi = (octave + 1) * 12 + semitone;
        return 440.0 * Math.pow(2.0, (midi - 69.0) / 12.0);
    }

    // -------------------------------------------------------------------------
    // Note value → duration in milliseconds
    // -------------------------------------------------------------------------
    private static double computeNoteValueMs(String nv, double quarterMs) {
        if (nv == null) return quarterMs;
        switch (nv) {
            case "1n":   return quarterMs * 4.0;
            case "2n":   return quarterMs * 2.0;
            case "2n.":  return quarterMs * 3.0;
            case "4n":   return quarterMs;
            case "4n.":  return quarterMs * 1.5;
            case "8n":   return quarterMs * 0.5;
            case "8n.":  return quarterMs * 0.75;
            case "8t":   return quarterMs / 3.0;
            case "16n":  return quarterMs * 0.25;
            case "16t":  return quarterMs / 6.0;
            default:     return quarterMs;
        }
    }

    // -------------------------------------------------------------------------
    // Frequency array parser (from "frequencies" or "notes" key in dict)
    // -------------------------------------------------------------------------
    private double[] parseFreqArray(KrollDict options, String method, double maxFreq) {
        if (containsKey(options, "frequencies")) {
            Object raw = options.get("frequencies");
            if (raw instanceof Object[]) {
                Object[] arr = (Object[]) raw;
                double[] result = new double[arr.length];
                for (int i = 0; i < arr.length; i++) {
                    Double v = parseFiniteDouble(arr[i]);
                    if (v != null) {
                        result[i] = clamp(v, MIN_FREQUENCY_HZ, maxFreq);
                    } else {
                        double f = parseNoteToFrequency(arr[i] != null ? arr[i].toString() : "");
                        result[i] = f >= MIN_FREQUENCY_HZ ? clamp(f, MIN_FREQUENCY_HZ, maxFreq)
                                                          : MIN_FREQUENCY_HZ;
                    }
                }
                return result;
            }
        }

        if (containsKey(options, "notes")) {
            Object raw = options.get("notes");
            if (raw instanceof Object[]) {
                Object[] arr = (Object[]) raw;
                double[] result = new double[arr.length];
                for (int i = 0; i < arr.length; i++) {
                    double f = parseNoteToFrequency(arr[i] != null ? arr[i].toString() : "");
                    result[i] = f >= MIN_FREQUENCY_HZ ? clamp(f, MIN_FREQUENCY_HZ, maxFreq)
                                                      : MIN_FREQUENCY_HZ;
                }
                return result;
            }
        }

        return null;
    }

    private double[] parseWaveTable(KrollDict options) {
        if (!containsKey(options, "waveTable")) return null;
        Object raw = options.get("waveTable");
        if (!(raw instanceof Object[])) return null;
        Object[] values = (Object[]) raw;
        int count = Math.min(values.length, SynthContract.MAX_WAVETABLE_SAMPLES);
        double[] result = new double[count];
        for (int i = 0; i < count; i++) {
            Double value = parseFiniteDouble(values[i]);
            result[i] = value == null ? 0.0 : clamp(value, -1.0, 1.0);
        }
        return result;
    }

    // -------------------------------------------------------------------------
    // Helpers
    // -------------------------------------------------------------------------

    private double maxFrequencyHz() {
        return synthThread != null ? synthThread.getSampleRate() * 0.45 : 44100.0 * 0.45;
    }

    private int readMixingProfile(KrollDict options) {
        if (!containsKey(options, "mixingProfile")) {
            return SynthDsp.MIXING_PROFILE_CONSERVATIVE;
        }

        Object rawValue = options.get("mixingProfile");
        if (!(rawValue instanceof String)) {
            warnOnce("startEngine", "mixingProfile", "not_string",
                    rawValue, "engine_not_started");
            return -1;
        }

        String profile = ((String) rawValue).trim();
        if ("raw".equalsIgnoreCase(profile)) return SynthDsp.MIXING_PROFILE_RAW;
        if ("conservative".equalsIgnoreCase(profile)) {
            return SynthDsp.MIXING_PROFILE_CONSERVATIVE;
        }
        if ("balanced".equalsIgnoreCase(profile)) {
            return SynthDsp.MIXING_PROFILE_BALANCED;
        }
        if ("speakerSafe".equalsIgnoreCase(profile)) {
            return SynthDsp.MIXING_PROFILE_SPEAKER_SAFE;
        }

        warnOnce("startEngine", "mixingProfile", "unsupported_value",
                rawValue, "engine_not_started");
        return -1;
    }

    private void warnOnce(String method, String key, String reason, Object value, String action) {
        String onceKey = method + ":" + key + ":" + reason;
        synchronized (warnedKeys) {
            if (warnedKeys.contains(onceKey)) return;
            warnedKeys.add(onceKey);
        }
        Log.w(TAG, "event=validation_warning method=" + method + " key=" + key
                + " reason=" + reason + " action=" + action + " value=" + value);
    }

    private boolean acceptValidation(String method, SynthContract.Validation validation) {
        if (validation.accepted) return true;
        warnOnce(method, validation.key, validation.reason, validation.value, "rejected");
        return false;
    }

    private KrollDict range(double min, double max) {
        KrollDict result = new KrollDict();
        result.put("min", min);
        result.put("max", max);
        return result;
    }

    private boolean containsKey(KrollDict options, String key) {
        return options != null && options.containsKey(key);
    }

    private Integer readRequiredIntOption(KrollDict options, String method, String key) {
        if (!containsKey(options, key)) {
            warnOnce(method, key, "missing_required", "null", "ignored");
            return null;
        }
        Double parsed = parseFiniteDouble(options.get(key));
        if (parsed == null) {
            warnOnce(method, key, "non_numeric_or_non_finite", options.get(key), "ignored");
            return null;
        }
        double rounded = Math.rint(parsed);
        if (Math.abs(parsed - rounded) > 1e-9) {
            warnOnce(method, key, "not_integer", options.get(key), "ignored");
            return null;
        }
        if (rounded < Integer.MIN_VALUE || rounded > Integer.MAX_VALUE) {
            warnOnce(method, key, "integer_overflow", options.get(key), "ignored");
            return null;
        }
        return (int) rounded;
    }

    private Double readRequiredDoubleOption(KrollDict options, String method, String key) {
        if (!containsKey(options, key)) {
            warnOnce(method, key, "missing_required", "null", "ignored");
            return null;
        }
        Double parsed = parseFiniteDouble(options.get(key));
        if (parsed == null) {
            warnOnce(method, key, "non_numeric_or_non_finite", options.get(key), "ignored");
            return null;
        }
        return parsed;
    }

    private int readOptionalIntOption(KrollDict options, String method, String key,
            int defaultValue, int minValue, int maxValue) {
        if (!containsKey(options, key)) return defaultValue;
        Double parsed = parseFiniteDouble(options.get(key));
        if (parsed == null) {
            warnOnce(method, key, "non_numeric_or_non_finite", options.get(key), "fallback_default");
            return defaultValue;
        }
        double rounded = Math.rint(parsed);
        if (Math.abs(parsed - rounded) > 1e-9) {
            warnOnce(method, key, "not_integer", options.get(key), "rounded");
        }
        if (rounded < Integer.MIN_VALUE || rounded > Integer.MAX_VALUE) {
            warnOnce(method, key, "integer_overflow", options.get(key), "fallback_default");
            return defaultValue;
        }
        int raw     = (int) rounded;
        int clamped = clamp(raw, minValue, maxValue);
        if (clamped != raw) {
            warnOnce(method, key, "out_of_range", options.get(key), "clamped");
        }
        return clamped;
    }

    private double readOptionalDoubleOption(KrollDict options, String method, String key,
            double defaultValue, double minValue, double maxValue) {
        if (!containsKey(options, key)) return defaultValue;
        Double parsed = parseFiniteDouble(options.get(key));
        if (parsed == null) {
            warnOnce(method, key, "non_numeric_or_non_finite", options.get(key), "fallback_default");
            return defaultValue;
        }
        double clamped = clamp(parsed, minValue, maxValue);
        if (clamped != parsed) {
            warnOnce(method, key, "out_of_range", options.get(key), "clamped");
        }
        return clamped;
    }

    private Double parseFiniteDouble(Object value) {
        if (value instanceof Number) {
            double parsed = ((Number) value).doubleValue();
            return Double.isFinite(parsed) ? parsed : null;
        }
        if (value instanceof String) {
            try {
                double parsed = Double.parseDouble((String) value);
                return Double.isFinite(parsed) ? parsed : null;
            } catch (NumberFormatException ignored) {
                return null;
            }
        }
        return null;
    }

    private static int clamp(int value, int minValue, int maxValue) {
        if (value < minValue) return minValue;
        if (value > maxValue) return maxValue;
        return value;
    }

    private static double clamp(double value, double minValue, double maxValue) {
        if (value < minValue) return minValue;
        if (value > maxValue) return maxValue;
        return value;
    }
}
