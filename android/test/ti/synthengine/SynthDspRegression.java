package ti.synthengine;

/** Host-side regression suite: javac/java only, no Android runtime required. */
public final class SynthDspRegression {
    private static final int SAMPLE_RATE = 48000;

    public static void main(String[] args) {
        testStaticEventGainLaw();
        testProtectedChordLoudnessIsStable();
        testPolyBlepReducesDiscontinuity();
        testLimiterIsLinearBelowCeiling();
        testLimiterDoesNotFlattenHotChord();
        testLimiterSmoothsDenseSineChords();
        testClassicAppStressChordIsContinuous();
        testProtectedProfileLeavesSingleVoiceUnchanged();
        testRawMixingProfileMatchesReferenceLimiter();
        testMasterVolumeChangesAreSmoothed();
        testDensePolyphonyNeverClips();
        testMasterBoostNeverClips();
        testEnvelopeEndpoints();
        System.out.println("SynthDspRegression: all tests passed");
    }

    private static void testStaticEventGainLaw() {
        double conservativeSingle = SynthDsp.voiceGainForEvent(
                SynthDsp.MIXING_PROFILE_CONSERVATIVE, 0, 1);
        require(Math.abs(conservativeSingle - 0.44) < 1.0e-12,
                "Conservative sine reference gain must preserve mix headroom");
        require(Math.abs(SynthDsp.voiceGainForEvent(
                        SynthDsp.MIXING_PROFILE_CONSERVATIVE, 0, 4)
                        - conservativeSingle / 2.0) < 1.0e-12,
                "Chord gain must use a fixed constant-power law");
        require(Math.abs(SynthDsp.voiceGainForEvent(
                        SynthDsp.MIXING_PROFILE_CONSERVATIVE, 1, 1)
                        - 0.44 / Math.sqrt(2.0)) < 1.0e-12,
                "Square waves must be RMS-calibrated before mixing");
        require(SynthDsp.voiceGainForEvent(
                        SynthDsp.MIXING_PROFILE_RAW, 0, 32) == 1.0,
                "Raw profile must preserve per-voice amplitude");
        require(Math.abs(SynthDsp.outputCeilingForProfile(
                        SynthDsp.MIXING_PROFILE_SPEAKER_SAFE) - 0.70) < 1.0e-12,
                "Speaker-safe profile must use its lower fixed ceiling");
    }

    private static void testProtectedChordLoudnessIsStable() {
        double referenceRms = renderProtectedSineChordRms(1);
        for (int voiceCount : new int[] { 4, 8, 16, 32 }) {
            double chordRms = renderProtectedSineChordRms(voiceCount);
            double deltaDb = 20.0 * Math.log10(chordRms / referenceRms);
            require(Math.abs(deltaDb) <= 1.5,
                    voiceCount + "-voice chord changed event loudness by "
                            + deltaDb + " dB");
        }
    }

    private static double renderProtectedSineChordRms(int voiceCount) {
        SynthDsp.PeakLimiter limiter = new SynthDsp.PeakLimiter(
                SAMPLE_RATE, SynthDsp.MIXING_PROFILE_CONSERVATIVE);
        double[] phases = new double[voiceCount];
        double[] increments = new double[voiceCount];
        double[] leftGains = new double[voiceCount];
        double[] rightGains = new double[voiceCount];
        double voiceGain = SynthDsp.voiceGainForEvent(
                SynthDsp.MIXING_PROFILE_CONSERVATIVE, 0, voiceCount);
        int[] intervals = {
                0, 4, 7, 12, 16, 19, 24, 28,
                31, 36, 40, 43, 48, 52, 55, 60
        };

        for (int voice = 0; voice < voiceCount; voice++) {
            double detuneCents = voiceCount > intervals.length
                    ? (voice < intervals.length ? -0.5 : 0.5) : 0.0;
            double frequency = 131.68 * Math.pow(2.0,
                    (intervals[voice % intervals.length] + detuneCents / 100.0) / 12.0);
            increments[voice] = frequency / SAMPLE_RATE;
            double pan = voiceCount == 1 ? 0.0
                    : -0.85 + 1.70 * voice / (voiceCount - 1.0);
            double angle = (pan + 1.0) * Math.PI * 0.25;
            leftGains[voice] = Math.cos(angle);
            rightGains[voice] = Math.sin(angle);
        }

        int measuredFrames = 0;
        double squaredSum = 0.0;
        int limitedFrames = 0;
        for (int frame = 0; frame < SAMPLE_RATE; frame++) {
            double mixL = 0.0;
            double mixR = 0.0;
            for (int voice = 0; voice < voiceCount; voice++) {
                double sample = Math.sin(phases[voice] * 2.0 * Math.PI) * voiceGain;
                mixL += sample * leftGains[voice];
                mixR += sample * rightGains[voice];
                phases[voice] += increments[voice];
                phases[voice] -= Math.floor(phases[voice]);
            }
            limiter.process(mixL, mixR, 1.0);
            if (frame >= SAMPLE_RATE / 4) {
                double outputL = limiter.getOutputL();
                double outputR = limiter.getOutputR();
                squaredSum += (outputL * outputL + outputR * outputR) * 0.5;
                if (limiter.getGain() < 0.99) limitedFrames++;
                measuredFrames++;
            }
        }
        require((double) limitedFrames / measuredFrames <= 0.01,
                voiceCount + "-voice conservative chord routinely drove the limiter: "
                        + limitedFrames + "/" + measuredFrames + " frames");
        return Math.sqrt(squaredSum / measuredFrames);
    }

    private static void testLimiterDoesNotFlattenHotChord() {
        SynthDsp.PeakLimiter limiter = new SynthDsp.PeakLimiter(SAMPLE_RATE);
        int consecutiveCeilingSamples = 0;
        int longestCeilingPlateau = 0;

        for (int i = 0; i < SAMPLE_RATE / 4; i++) {
            double input = 0.78 * Math.sin(2.0 * Math.PI * 261.63 * i / SAMPLE_RATE)
                    + 0.78 * Math.sin(2.0 * Math.PI * 329.63 * i / SAMPLE_RATE);
            limiter.process(input, input, 1.0);

            if (Math.abs(Math.abs(limiter.getOutputL()) - SynthDsp.OUTPUT_CEILING)
                    < 1.0e-10) {
                consecutiveCeilingSamples++;
                longestCeilingPlateau = Math.max(longestCeilingPlateau,
                        consecutiveCeilingSamples);
            } else {
                consecutiveCeilingSamples = 0;
            }
        }

        // Two adjacent samples can quantize to the same discrete-time peak. Longer
        // plateaus indicate sample-by-sample safety limiting (the old path produced 13).
        require(longestCeilingPlateau <= 2,
                "Limiter flattened a polyphonic peak for " + longestCeilingPlateau
                        + " consecutive samples");
    }

    private static void testPolyBlepReducesDiscontinuity() {
        double increment = 10000.0 / SAMPLE_RATE;
        double phase = 0.0;
        double previousBandLimited = SynthDsp.renderPeriodicWave(2, phase, increment);
        double previousNaive = 2.0 * phase - 1.0;
        double maxBandLimitedJump = 0.0;
        double maxNaiveJump = 0.0;

        for (int i = 0; i < SAMPLE_RATE; i++) {
            phase += increment;
            phase -= Math.floor(phase);
            double bandLimited = SynthDsp.renderPeriodicWave(2, phase, increment);
            double naive = 2.0 * phase - 1.0;
            maxBandLimitedJump = Math.max(maxBandLimitedJump,
                    Math.abs(bandLimited - previousBandLimited));
            maxNaiveJump = Math.max(maxNaiveJump, Math.abs(naive - previousNaive));
            previousBandLimited = bandLimited;
            previousNaive = naive;
        }

        require(maxBandLimitedJump < maxNaiveJump * 0.75,
                "PolyBLEP saw did not sufficiently reduce the wrap discontinuity");
    }

    private static void testLimiterSmoothsDenseSineChords() {
        requireDenseSineChordHasSmoothGain(16);
        requireDenseSineChordHasSmoothGain(32);
    }

    private static void testProtectedMixingProfilesHandleSimonChords() {
        int[] profiles = {
                SynthDsp.MIXING_PROFILE_CONSERVATIVE,
                SynthDsp.MIXING_PROFILE_BALANCED,
                SynthDsp.MIXING_PROFILE_SPEAKER_SAFE
        };
        double[][] callerSettings = {
                // volume, attack ms, release ms, duration ms
                { 0.40, 10.0, 100.0, 420.0 },
                { 1.00, 10.0, 340.0, 420.0 },
                { 0.85, 10.0, 200.0, 420.0 }
        };

        for (int profile : profiles) {
            for (double[] settings : callerSettings) {
                requireSimonChordIsProtected(profile, settings);
            }
        }
    }

    private static void testProtectedProfileLeavesSingleVoiceUnchanged() {
        SynthDsp.PeakLimiter raw = new SynthDsp.PeakLimiter(SAMPLE_RATE);
        SynthDsp.PeakLimiter protectedLimiter = new SynthDsp.PeakLimiter(
                SAMPLE_RATE, SynthDsp.MIXING_PROFILE_CONSERVATIVE);
        double centerGain = Math.sqrt(0.5);

        for (int frame = 0; frame < SAMPLE_RATE / 4; frame++) {
            double input = 0.85 * centerGain
                    * Math.sin(2.0 * Math.PI * 329.6 * frame / SAMPLE_RATE);
            raw.process(input, input, 1.0);
            protectedLimiter.process(input, input, 1.0,
                    0.85 * centerGain, 0.85 * centerGain, 1);
            require(Double.doubleToLongBits(raw.getOutputL())
                            == Double.doubleToLongBits(protectedLimiter.getOutputL())
                    && Double.doubleToLongBits(raw.getOutputR())
                            == Double.doubleToLongBits(protectedLimiter.getOutputR()),
                    "Protected profile reduced or altered a single voice");
        }
    }

    private static void testProtectedProfileRecoversAfterOverlap() {
        SynthDsp.PeakLimiter limiter = new SynthDsp.PeakLimiter(
                SAMPLE_RATE, SynthDsp.MIXING_PROFILE_CONSERVATIVE);
        int overlapFrames = SAMPLE_RATE / 10;
        int recoveryFrames = (int)Math.round(SAMPLE_RATE * 0.175);
        double maximumGainDrop = 0.0;
        double previousGain = 1.0;

        for (int frame = 0; frame < overlapFrames + recoveryFrames; frame++) {
            boolean overlapping = frame < overlapFrames;
            double input = 0.20 * Math.sin(2.0 * Math.PI * 277.2 * frame / SAMPLE_RATE);
            double budget = overlapping ? 1.20 : 0.60;
            limiter.process(input, input, 1.0, budget, budget,
                    0.60, 0.60, overlapping ? 2 : 1);
            maximumGainDrop = Math.max(maximumGainDrop, previousGain - limiter.getGain());
            previousGain = limiter.getGain();
        }

        require(maximumGainDrop <= 0.01,
                "Protected overlap caused an abrupt gain drop: " + maximumGainDrop);
        require(limiter.getGain() >= 0.99,
                "Protected profile did not recover after overlap: " + limiter.getGain());
    }

    private static void testProtectedProfileRestoresSingleVoiceAfterDenseChord() {
        SynthDsp.PeakLimiter limiter = new SynthDsp.PeakLimiter(
                SAMPLE_RATE, SynthDsp.MIXING_PROFILE_CONSERVATIVE);
        int denseFrames = SAMPLE_RATE / 10;
        double protectedVoiceLevel = 0.82;

        for (int frame = 0; frame < denseFrames; frame++) {
            double input = protectedVoiceLevel * 32.0
                    * Math.sin(2.0 * Math.PI * 263.45 * frame / SAMPLE_RATE);
            limiter.process(input, input, 1.0,
                    protectedVoiceLevel * 32.0, protectedVoiceLevel * 32.0,
                    protectedVoiceLevel, protectedVoiceLevel, 32);
        }

        int latencyFrames = limiter.getLatencySamples();
        int measurementFrames = (int)Math.round(SAMPLE_RATE * 0.050);
        double gainSum = 0.0;
        double previousGain = limiter.getGain();
        double maximumGainRise = 0.0;

        for (int frame = 0; frame < latencyFrames + measurementFrames; frame++) {
            double input = 0.20 * Math.sin(2.0 * Math.PI * 523.25 * frame / SAMPLE_RATE);
            limiter.process(input, input, 1.0,
                    protectedVoiceLevel, protectedVoiceLevel,
                    protectedVoiceLevel, protectedVoiceLevel, 1);
            double gain = limiter.getGain();
            maximumGainRise = Math.max(maximumGainRise, gain - previousGain);
            if (frame >= latencyFrames) gainSum += gain;
            previousGain = gain;
        }

        double averageGain = gainSum / measurementFrames;
        require(averageGain >= 0.80,
                "A dense chord depressed the following single voice: " + averageGain);
        require(limiter.getGain() >= 0.99,
                "Single-voice gain remained depressed after 50 ms: " + limiter.getGain());
        require(maximumGainRise <= 0.01,
                "Dense-chord recovery introduced an abrupt gain rise: "
                        + maximumGainRise);
    }

    private static void testProfilesNormalizeOnlyContributingVoices() {
        requireProfileGain(SynthDsp.MIXING_PROFILE_CONSERVATIVE, 1, 1.0);
        requireProfileGain(SynthDsp.MIXING_PROFILE_CONSERVATIVE, 2, 0.5);
        requireProfileGain(SynthDsp.MIXING_PROFILE_CONSERVATIVE, 4, 0.25);
        requireProfileGain(SynthDsp.MIXING_PROFILE_BALANCED, 1, 1.0);
        requireProfileGain(SynthDsp.MIXING_PROFILE_BALANCED, 2, Math.sqrt(0.5));
        requireProfileGain(SynthDsp.MIXING_PROFILE_BALANCED, 4, 0.5);
        requireProfileGain(SynthDsp.MIXING_PROFILE_SPEAKER_SAFE, 1, 1.0);
        requireProfileGain(SynthDsp.MIXING_PROFILE_SPEAKER_SAFE, 2, 0.35);
        requireProfileGain(SynthDsp.MIXING_PROFILE_SPEAKER_SAFE, 4, 0.175);
    }

    private static void testConservativeProfileBoundsStaggeredSimonPads() {
        SynthDsp.PeakLimiter limiter = new SynthDsp.PeakLimiter(
                SAMPLE_RATE, SynthDsp.MIXING_PROFILE_CONSERVATIVE);
        double[] frequencies = { 164.7, 219.9, 277.2, 329.6 };
        double[] phases = new double[frequencies.length];
        int[] starts = {
                0,
                (int)Math.round(0.015 * SAMPLE_RATE),
                (int)Math.round(0.030 * SAMPLE_RATE),
                (int)Math.round(0.045 * SAMPLE_RATE)
        };
        int totalSamples = (int)Math.round(0.420 * SAMPLE_RATE);
        int attackSamples = (int)Math.round(0.010 * SAMPLE_RATE);
        int releaseSamples = (int)Math.round(0.200 * SAMPLE_RATE);
        double volume = 0.85;
        double centerGain = Math.sqrt(0.5);
        double singleVoicePeak = volume * centerGain;
        double maximumOutput = 0.0;
        double maximumGainDrop = 0.0;
        double previousGain = 1.0;
        int frames = starts[starts.length - 1] + totalSamples + limiter.getLatencySamples();

        for (int frame = 0; frame < frames; frame++) {
            double mix = 0.0;
            double channelBudget = 0.0;
            double maxVoiceBudget = 0.0;
            int contributingVoices = 0;

            for (int voice = 0; voice < frequencies.length; voice++) {
                int localFrame = frame - starts[voice];
                if (localFrame < 0 || localFrame >= totalSamples) continue;
                int remaining = totalSamples - localFrame;
                double envelope = 1.0;
                if (localFrame < attackSamples) {
                    envelope = SynthDsp.smoothStep01(
                            (double)localFrame / Math.max(1, attackSamples - 1));
                } else if (remaining <= releaseSamples) {
                    envelope = SynthDsp.smoothStep01(
                            (double)(remaining - 1) / Math.max(1, releaseSamples - 1));
                }
                double voiceBudget = volume * envelope * centerGain;
                mix += Math.sin(phases[voice] * 2.0 * Math.PI) * voiceBudget;
                phases[voice] += frequencies[voice] / SAMPLE_RATE;
                phases[voice] -= Math.floor(phases[voice]);
                channelBudget += voiceBudget;
                maxVoiceBudget = Math.max(maxVoiceBudget, voiceBudget);
                if (voiceBudget > 1.0e-4) contributingVoices++;
            }

            limiter.process(mix, mix, 1.0,
                    channelBudget, channelBudget,
                    maxVoiceBudget, maxVoiceBudget, contributingVoices);
            maximumOutput = Math.max(maximumOutput, Math.abs(limiter.getOutputL()));
            maximumGainDrop = Math.max(maximumGainDrop, previousGain - limiter.getGain());
            previousGain = limiter.getGain();
        }

        require(maximumOutput <= singleVoicePeak + 1.0e-12,
                "Staggered Simon pads exceeded one-voice level: " + maximumOutput
                        + " > " + singleVoicePeak);
        require(maximumGainDrop <= 0.01,
                "Staggered Simon pads caused an abrupt gain drop: " + maximumGainDrop);
    }

    private static void requireProfileGain(int profile, int voiceCount, double expectedGain) {
        SynthDsp.PeakLimiter limiter = new SynthDsp.PeakLimiter(SAMPLE_RATE, profile);
        double perVoiceBudget = 0.20;
        for (int frame = 0; frame < SAMPLE_RATE / 20; frame++) {
            double input = 0.10 * Math.sin(2.0 * Math.PI * 220.0 * frame / SAMPLE_RATE);
            limiter.process(input, input, 1.0,
                    perVoiceBudget * voiceCount, perVoiceBudget * voiceCount,
                    perVoiceBudget, perVoiceBudget, voiceCount);
        }
        require(Math.abs(limiter.getGain() - expectedGain) <= 1.0e-9,
                "Unexpected profile gain for " + voiceCount + " voices: "
                        + limiter.getGain() + " expected " + expectedGain);
    }

    private static void requireSimonChordIsProtected(int profile, double[] settings) {
        SynthDsp.PeakLimiter limiter = new SynthDsp.PeakLimiter(SAMPLE_RATE, profile);
        double[] frequencies = { 164.7, 219.9, 277.2, 329.6 };
        double[] phases = new double[frequencies.length];
        double centerGain = Math.sqrt(0.5);
        double volume = settings[0];
        int attackSamples = (int) Math.round(settings[1] * 0.001 * SAMPLE_RATE);
        int releaseSamples = (int) Math.round(settings[2] * 0.001 * SAMPLE_RATE);
        int totalSamples = (int) Math.round(settings[3] * 0.001 * SAMPLE_RATE);
        double channelBudget = frequencies.length * volume * centerGain;
        double maximumOutput = 0.0;
        double maximumGainDrop = 0.0;
        double previousGain = 1.0;

        for (int frame = 0; frame < totalSamples + limiter.getLatencySamples(); frame++) {
            double mix = 0.0;
            if (frame < totalSamples) {
                int remaining = totalSamples - frame;
                double envelope = 1.0;
                if (frame < attackSamples) {
                    envelope = SynthDsp.smoothStep01(
                            (double) frame / Math.max(1, attackSamples - 1));
                } else if (remaining <= releaseSamples) {
                    envelope = SynthDsp.smoothStep01(
                            (double)(remaining - 1) / Math.max(1, releaseSamples - 1));
                }
                for (int voice = 0; voice < frequencies.length; voice++) {
                    mix += Math.sin(phases[voice] * 2.0 * Math.PI)
                            * envelope * volume * centerGain;
                    phases[voice] += frequencies[voice] / SAMPLE_RATE;
                    phases[voice] -= Math.floor(phases[voice]);
                }
            }

            double envelopeBudget = 0.0;
            if (frame < totalSamples) {
                int remaining = totalSamples - frame;
                double envelope = 1.0;
                if (frame < attackSamples) {
                    envelope = SynthDsp.smoothStep01(
                            (double) frame / Math.max(1, attackSamples - 1));
                } else if (remaining <= releaseSamples) {
                    envelope = SynthDsp.smoothStep01(
                            (double)(remaining - 1) / Math.max(1, releaseSamples - 1));
                }
                envelopeBudget = channelBudget * envelope;
            }
            limiter.process(mix, mix, 1.0, envelopeBudget, envelopeBudget,
                    envelopeBudget / frequencies.length,
                    envelopeBudget / frequencies.length,
                    envelopeBudget > 1.0e-4 ? frequencies.length : 0);
            maximumOutput = Math.max(maximumOutput, Math.abs(limiter.getOutputL()));
            maximumGainDrop = Math.max(maximumGainDrop, previousGain - limiter.getGain());
            previousGain = limiter.getGain();
        }

        double singleVoiceLevel = volume * centerGain;
        double normalizedUpperBound;
        if (profile == SynthDsp.MIXING_PROFILE_CONSERVATIVE) {
            normalizedUpperBound = singleVoiceLevel;
        } else if (profile == SynthDsp.MIXING_PROFILE_SPEAKER_SAFE) {
            normalizedUpperBound = singleVoiceLevel / Math.sqrt(2.0);
        } else {
            normalizedUpperBound = Math.min(SynthDsp.OUTPUT_CEILING,
                    singleVoiceLevel * Math.sqrt(frequencies.length));
        }
        require(maximumOutput <= normalizedUpperBound + 1.0e-12,
                "Simon chord exceeded normalized upper bound "
                        + normalizedUpperBound + ": " + maximumOutput);
        require(maximumGainDrop <= 0.01,
                "Simon chord caused an abrupt protected-gain drop: " + maximumGainDrop);
    }

    private static void testRawMixingProfileMatchesReferenceLimiter() {
        SynthDsp.PeakLimiter reference = new SynthDsp.PeakLimiter(SAMPLE_RATE);
        SynthDsp.PeakLimiter raw = new SynthDsp.PeakLimiter(
                SAMPLE_RATE, SynthDsp.MIXING_PROFILE_RAW);

        for (int frame = 0; frame < SAMPLE_RATE / 4; frame++) {
            double left = 1.2 * Math.sin(2.0 * Math.PI * 261.63 * frame / SAMPLE_RATE);
            double right = 0.9 * Math.sin(2.0 * Math.PI * 329.63 * frame / SAMPLE_RATE);
            double master = frame < SAMPLE_RATE / 8 ? 1.0 : 0.65;
            reference.process(left, right, master);
            raw.process(left, right, master, 100.0, 100.0, 32);
            require(Double.doubleToLongBits(reference.getOutputL())
                            == Double.doubleToLongBits(raw.getOutputL())
                    && Double.doubleToLongBits(reference.getOutputR())
                            == Double.doubleToLongBits(raw.getOutputR())
                    && Double.doubleToLongBits(reference.getGain())
                            == Double.doubleToLongBits(raw.getGain()),
                    "Raw mixing profile changed reference limiter output");
        }
    }

    private static void testProtectedSimonChordHasNoAddedOffToneEnergy() {
        SynthDsp.PeakLimiter limiter = new SynthDsp.PeakLimiter(
                SAMPLE_RATE, SynthDsp.MIXING_PROFILE_CONSERVATIVE);
        double[] frequencies = { 164.7, 219.9, 277.2, 329.6 };
        double[] increments = new double[frequencies.length];
        double[] phases = new double[frequencies.length];
        double[] spectrumWindow = new double[4096];
        double volume = 0.85;
        double centerGain = Math.sqrt(0.5);
        double channelBudget = frequencies.length * volume * centerGain;
        int attackSamples = (int) Math.round(0.010 * SAMPLE_RATE);
        int frames = SAMPLE_RATE / 2;

        for (int voice = 0; voice < frequencies.length; voice++) {
            increments[voice] = frequencies[voice] / SAMPLE_RATE;
        }

        for (int frame = 0; frame < frames; frame++) {
            double envelope = frame < attackSamples
                    ? SynthDsp.smoothStep01((double) frame / (attackSamples - 1)) : 1.0;
            double mix = 0.0;
            for (int voice = 0; voice < frequencies.length; voice++) {
                mix += Math.sin(phases[voice] * 2.0 * Math.PI)
                        * envelope * volume * centerGain;
                phases[voice] += increments[voice];
                phases[voice] -= Math.floor(phases[voice]);
            }
            double envelopeBudget = channelBudget * envelope;
            limiter.process(mix, mix, 1.0, envelopeBudget, envelopeBudget,
                    envelopeBudget / frequencies.length,
                    envelopeBudget / frequencies.length,
                    envelopeBudget > 1.0e-4 ? frequencies.length : 0);
            if (frame >= frames - spectrumWindow.length) {
                spectrumWindow[frame - (frames - spectrumWindow.length)] =
                        limiter.getOutputL();
            }
        }

        double offToneDb = calculateOffToneEnergyDb(spectrumWindow, increments);
        require(offToneDb <= -25.0,
                "Protected Simon chord produced excessive off-tone energy: "
                        + offToneDb + " dBc");
    }

    private static void requireDenseSineChordHasSmoothGain(int voiceCount) {
        SynthDsp.PeakLimiter limiter = new SynthDsp.PeakLimiter(SAMPLE_RATE);
        int[] intervals = {
                0, 4, 7, 12, 16, 19, 24, 28,
                31, 36, 40, 43, 48, 52, 55, 60
        };
        double[] phases = new double[voiceCount];
        double[] increments = new double[voiceCount];
        double[] leftGains = new double[voiceCount];
        double[] rightGains = new double[voiceCount];
        double perVoiceGain = 0.95 / Math.sqrt(voiceCount);

        for (int voice = 0; voice < voiceCount; voice++) {
            int interval = intervals[voice % intervals.length]
                    + 12 * (voice / intervals.length);
            double frequency = 440.0 * Math.pow(2.0, (48 + interval - 69) / 12.0);
            increments[voice] = frequency / SAMPLE_RATE;
            double pan = -0.8 + 1.6 * voice / (voiceCount - 1);
            double angle = (pan + 1.0) * Math.PI * 0.25;
            leftGains[voice] = Math.cos(angle);
            rightGains[voice] = Math.sin(angle);
        }

        int attackSamples = (int) Math.round(0.024 * SAMPLE_RATE);
        int frames = (int) Math.round(0.9 * SAMPLE_RATE);
        double[] spectrumWindow = new double[4096];
        double previousGain = 1.0;
        double minimumGain = 1.0;
        double maximumGainDrop = 0.0;
        double maximumOutput = 0.0;

        for (int frame = 0; frame < frames; frame++) {
            double envelope = frame < attackSamples
                    ? SynthDsp.smoothStep01((double) frame / (attackSamples - 1))
                    : 1.0;
            double mixL = 0.0;
            double mixR = 0.0;
            for (int voice = 0; voice < voiceCount; voice++) {
                double sample = Math.sin(phases[voice] * 2.0 * Math.PI)
                        * envelope * perVoiceGain;
                mixL += sample * leftGains[voice];
                mixR += sample * rightGains[voice];
                phases[voice] += increments[voice];
                phases[voice] -= Math.floor(phases[voice]);
            }

            limiter.process(mixL, mixR, 1.0);
            double gain = limiter.getGain();
            maximumGainDrop = Math.max(maximumGainDrop, previousGain - gain);
            minimumGain = Math.min(minimumGain, gain);
            maximumOutput = Math.max(maximumOutput, Math.max(
                    Math.abs(limiter.getOutputL()), Math.abs(limiter.getOutputR())));
            if (frame >= frames - spectrumWindow.length) {
                spectrumWindow[frame - (frames - spectrumWindow.length)] =
                        limiter.getOutputL();
            }
            previousGain = gain;
        }

        require(minimumGain < 0.75,
                voiceCount + "-voice chord did not exercise the limiter");
        require(maximumOutput <= SynthDsp.OUTPUT_CEILING + 1.0e-12,
                voiceCount + "-voice chord exceeded the output ceiling: " + maximumOutput);
        require(maximumGainDrop <= 0.01,
                voiceCount + "-voice chord caused an abrupt limiter gain drop: "
                        + maximumGainDrop);
        double offToneDb = calculateOffToneEnergyDb(spectrumWindow, increments);
        require(offToneDb <= -25.0,
                voiceCount + "-voice chord produced excessive off-tone energy: "
                        + offToneDb + " dBc");
    }

    private static void testClassicAppStressChordIsContinuous() {
        SynthDsp.PeakLimiter limiter = new SynthDsp.PeakLimiter(
                SAMPLE_RATE, SynthDsp.MIXING_PROFILE_CONSERVATIVE);
        int[] intervals = {
                0, 4, 7, 12, 16, 19, 24, 28,
                31, 36, 40, 43, 48, 52, 55, 60
        };
        double[] detuneCents = {-0.5, 0.5};
        int voiceCount = intervals.length * detuneCents.length;
        double[] phases = new double[voiceCount];
        double[] increments = new double[voiceCount];
        double[] leftGains = new double[voiceCount];
        double[] rightGains = new double[voiceCount];
        double volume = 0.78;

        for (int voice = 0; voice < voiceCount; voice++) {
            int intervalIndex = voice % intervals.length;
            int layer = voice / intervals.length;
            double midi = 48.12 + intervals[intervalIndex]
                    + detuneCents[layer] / 100.0;
            double frequency = 440.0 * Math.pow(2.0, (midi - 69.0) / 12.0);
            increments[voice] = frequency / SAMPLE_RATE;
            double pan = -0.85 + 1.7 * voice / (voiceCount - 1);
            double angle = (pan + 1.0) * Math.PI * 0.25;
            leftGains[voice] = Math.cos(angle);
            rightGains[voice] = Math.sin(angle);
        }

        int totalSamples = (int) Math.round(0.600 * SAMPLE_RATE);
        int attackSamples = (int) Math.round(0.040 * SAMPLE_RATE);
        int releaseSamples = (int) Math.round(0.160 * SAMPLE_RATE);
        int drainSamples = (int) Math.round(0.010 * SAMPLE_RATE);
        double previousL = 0.0;
        double previousR = 0.0;
        double previousGain = 1.0;
        double maximumOutput = 0.0;
        double maximumSampleStep = 0.0;
        double maximumGainStep = 0.0;
        double finalOutput = 0.0;

        for (int frame = 0; frame < totalSamples + drainSamples; frame++) {
            double envelope = 0.0;
            if (frame < totalSamples) {
                if (frame < attackSamples) {
                    envelope = SynthDsp.smoothStep01(
                            (double) frame / Math.max(1, attackSamples - 1));
                } else if (frame >= totalSamples - releaseSamples) {
                    int samplesRemaining = totalSamples - frame;
                    envelope = SynthDsp.smoothStep01(
                            (double) (samplesRemaining - 1)
                                    / Math.max(1, releaseSamples - 1));
                } else {
                    envelope = 1.0;
                }
            }

            double mixL = 0.0;
            double mixR = 0.0;
            double peakBudgetL = 0.0;
            double peakBudgetR = 0.0;
            double maxVoiceBudgetL = 0.0;
            double maxVoiceBudgetR = 0.0;
            int contributingVoices = envelope > 1.0e-4 ? voiceCount : 0;
            for (int voice = 0; voice < voiceCount; voice++) {
                double sample = Math.sin(phases[voice] * 2.0 * Math.PI)
                        * envelope * volume;
                mixL += sample * leftGains[voice];
                mixR += sample * rightGains[voice];
                double voiceBudget = volume * envelope;
                double voiceBudgetL = voiceBudget * leftGains[voice];
                double voiceBudgetR = voiceBudget * rightGains[voice];
                peakBudgetL += voiceBudgetL;
                peakBudgetR += voiceBudgetR;
                maxVoiceBudgetL = Math.max(maxVoiceBudgetL, voiceBudgetL);
                maxVoiceBudgetR = Math.max(maxVoiceBudgetR, voiceBudgetR);
                phases[voice] += increments[voice];
                phases[voice] -= Math.floor(phases[voice]);
            }

            limiter.process(mixL, mixR, 1.0,
                    peakBudgetL, peakBudgetR,
                    maxVoiceBudgetL, maxVoiceBudgetR, contributingVoices);
            double outputL = limiter.getOutputL();
            double outputR = limiter.getOutputR();
            double gain = limiter.getGain();
            maximumOutput = Math.max(maximumOutput,
                    Math.max(Math.abs(outputL), Math.abs(outputR)));
            maximumSampleStep = Math.max(maximumSampleStep,
                    Math.max(Math.abs(outputL - previousL), Math.abs(outputR - previousR)));
            double audibleMagnitude = Math.max(Math.abs(outputL), Math.abs(outputR));
            if (audibleMagnitude > 1.0e-9) {
                maximumGainStep = Math.max(maximumGainStep,
                        Math.abs(gain - previousGain));
            }
            previousL = outputL;
            previousR = outputR;
            previousGain = gain;
            finalOutput = Math.max(Math.abs(outputL), Math.abs(outputR));
        }

        require(maximumOutput <= SynthDsp.OUTPUT_CEILING + 1.0e-12,
                "Classic 32-voice stress chord exceeded the output ceiling: "
                        + maximumOutput);
        require(maximumOutput >= 0.20,
                "Classic 32-voice stress chord was reduced toward silence: "
                        + maximumOutput);
        require(maximumGainStep <= 0.01,
                "Classic 32-voice stress chord caused an abrupt gain change: "
                        + maximumGainStep);
        require(maximumSampleStep <= 0.30,
                "Classic 32-voice stress chord contained a sample discontinuity: "
                        + maximumSampleStep);
        require(finalOutput <= 1.0e-9,
                "Classic 32-voice stress chord did not finish at silence: "
                        + finalOutput);
        require(limiter.getGain() >= 0.99,
                "Classic 32-voice stress chord left the following bus depressed: "
                        + limiter.getGain());
    }

    private static double calculateOffToneEnergyDb(
            double[] samples, double[] requestedIncrements) {
        double requestedPower = 0.0;
        double offTonePower = 0.0;
        double binWidth = (double) SAMPLE_RATE / samples.length;

        for (int bin = 0; bin <= samples.length / 2; bin++) {
            double real = 0.0;
            double imaginary = 0.0;
            for (int sample = 0; sample < samples.length; sample++) {
                double hann = 0.5 - 0.5 * Math.cos(
                        2.0 * Math.PI * sample / (samples.length - 1));
                double angle = 2.0 * Math.PI * bin * sample / samples.length;
                double windowed = samples[sample] * hann;
                real += windowed * Math.cos(angle);
                imaginary -= windowed * Math.sin(angle);
            }
            double power = real * real + imaginary * imaginary;
            double binFrequency = bin * binWidth;
            boolean requested = bin == 0;
            for (double increment : requestedIncrements) {
                if (Math.abs(binFrequency - increment * SAMPLE_RATE)
                        <= 3.0 * binWidth) {
                    requested = true;
                    break;
                }
            }
            if (requested) requestedPower += power;
            else offTonePower += power;
        }

        return 10.0 * Math.log10(offTonePower / requestedPower);
    }

    private static void testLimiterIsLinearBelowCeiling() {
        SynthDsp.PeakLimiter limiter = new SynthDsp.PeakLimiter(SAMPLE_RATE);
        int latency = limiter.getLatencySamples();
        double[] history = new double[latency + 1];

        for (int i = 0; i < SAMPLE_RATE / 10; i++) {
            double input = 0.10 * Math.sin(2.0 * Math.PI * 440.0 * i / SAMPLE_RATE);
            history[i % history.length] = input;
            limiter.process(input, -input, 1.0);
            if (i >= latency) {
                int delayedIndex = (i - latency) % history.length;
                double expected = history[delayedIndex];
                require(Math.abs(limiter.getOutputL() - expected) < 1.0e-10,
                        "Limiter altered a signal below the ceiling");
            }
        }
    }

    private static void testMasterVolumeChangesAreSmoothed() {
        SynthDsp.PeakLimiter limiter = new SynthDsp.PeakLimiter(SAMPLE_RATE);
        int latency = limiter.getLatencySamples();
        double previousOutput = 0.0;
        double maximumStep = 0.0;

        for (int frame = 0; frame < latency * 5; frame++) {
            double master = frame < latency * 2 ? 1.0 : 0.2;
            limiter.process(0.1, 0.1, master);
            if (frame > latency) {
                maximumStep = Math.max(maximumStep,
                        Math.abs(limiter.getOutputL() - previousOutput));
            }
            previousOutput = limiter.getOutputL();
        }

        require(maximumStep <= 0.01,
                "Master volume introduced an abrupt output step: " + maximumStep);
        require(Math.abs(limiter.getOutputL() - 0.02) < 1.0e-10,
                "Master volume ramp did not settle on its requested value");
    }

    private static void testDensePolyphonyNeverClips() {
        SynthDsp.PeakLimiter limiter = new SynthDsp.PeakLimiter(SAMPLE_RATE);
        double[] frequencies = {
                261.63, 293.66, 329.63, 349.23, 392.00, 440.00, 493.88, 523.25,
                587.33, 659.25, 698.46, 783.99, 880.00, 987.77, 1046.50, 1174.66,
                1318.51, 1396.91, 1567.98, 1760.00, 1975.53, 2093.00, 2349.32, 2637.02,
                2793.83, 3135.96, 3520.00, 3951.07, 4186.01, 4698.64, 5274.04, 5587.65
        };

        double maxOutput = 0.0;
        for (int frame = 0; frame < SAMPLE_RATE / 2; frame++) {
            double mix = 0.0;
            for (int voice = 0; voice < frequencies.length; voice++) {
                double phase = (frequencies[voice] * frame / SAMPLE_RATE) % 1.0;
                mix += SynthDsp.renderPeriodicWave(voice % 4, phase,
                        frequencies[voice] / SAMPLE_RATE) * 0.9;
            }
            limiter.process(mix * Math.sqrt(0.5), mix * Math.sqrt(0.5), 0.9);
            maxOutput = Math.max(maxOutput, Math.max(
                    Math.abs(limiter.getOutputL()), Math.abs(limiter.getOutputR())));
            require(Double.isFinite(limiter.getOutputL()) && Double.isFinite(limiter.getOutputR()),
                    "Limiter produced a non-finite sample");
        }

        require(maxOutput <= SynthDsp.OUTPUT_CEILING + 1.0e-12,
                "Dense polyphony exceeded the output ceiling: " + maxOutput);
        require(maxOutput > 0.70, "Dense polyphony test did not exercise the limiter");
    }

    private static void testMasterBoostNeverClips() {
        SynthDsp.PeakLimiter limiter = new SynthDsp.PeakLimiter(SAMPLE_RATE);
        double maxOutput = 0.0;
        for (int i = 0; i < SAMPLE_RATE / 10; i++) {
            double sample = 0.8 * Math.sin(2.0 * Math.PI * 1000.0 * i / SAMPLE_RATE);
            limiter.process(sample, sample, 2.0);
            maxOutput = Math.max(maxOutput, Math.abs(limiter.getOutputL()));
        }
        require(maxOutput <= SynthDsp.OUTPUT_CEILING + 1.0e-12,
                "masterVolume=2 exceeded the output ceiling");
    }

    private static void testEnvelopeEndpoints() {
        require(SynthDsp.smoothStep01(0.0) == 0.0, "Envelope must start at zero");
        require(SynthDsp.smoothStep01(1.0) == 1.0, "Envelope must finish at one");
        require(SynthDsp.smoothStep01(-1.0) == 0.0, "Envelope must clamp below zero");
        require(SynthDsp.smoothStep01(2.0) == 1.0, "Envelope must clamp above one");
    }

    private static void require(boolean condition, String message) {
        if (!condition) throw new AssertionError(message);
    }
}
