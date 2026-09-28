package ti.synthengine;

/**
 * Allocation-free DSP primitives shared by the Android render thread and the
 * host-side regression tests. This class deliberately has no Android imports.
 */
final class SynthDsp {
    static final double OUTPUT_CEILING = 0.85;
    static final int MIXING_PROFILE_RAW = 0;
    static final int MIXING_PROFILE_CONSERVATIVE = 1;
    static final int MIXING_PROFILE_BALANCED = 2;
    static final int MIXING_PROFILE_SPEAKER_SAFE = 3;
    static final double SPEAKER_SAFE_OUTPUT_CEILING = 0.70;

    private static final double CONSERVATIVE_SINE_GAIN = 0.44;
    private static final double CONSERVATIVE_SQUARE_GAIN = 0.44 / Math.sqrt(2.0);
    private static final double CONSERVATIVE_RAMP_GAIN = 0.44 * Math.sqrt(1.5);

    private SynthDsp() {}

    static double renderPeriodicWave(int waveType, double phase, double phaseIncrement) {
        double t = phase - Math.floor(phase);
        double dt = clamp(Math.abs(phaseIncrement), 1.0e-9, 0.5);

        switch (waveType) {
            case 1: { // Band-limited square (PolyBLEP at both discontinuities)
                double value = t < 0.5 ? 1.0 : -1.0;
                value += polyBlep(t, dt);
                double shifted = t + 0.5;
                if (shifted >= 1.0) shifted -= 1.0;
                value -= polyBlep(shifted, dt);
                return value;
            }
            case 2: // Band-limited rising sawtooth
                return 2.0 * t - 1.0 - polyBlep(t, dt);
            case 3: // Continuous triangle; no value discontinuity to de-click
                return 1.0 - 4.0 * Math.abs(t - 0.5);
            default:
                return Math.sin(t * 2.0 * Math.PI);
        }
    }

    static double smoothStep01(double x) {
        double t = clamp(x, 0.0, 1.0);
        return t * t * (3.0 - 2.0 * t);
    }

    /**
     * Returns a gain which is fixed for the complete lifetime of one event.
     * Protected profiles treat volume as the level of the event, not as a
     * full-scale amplitude repeated independently by every note in a chord.
     */
    static double voiceGainForEvent(int mixingProfile, int waveType, int voiceCount) {
        if (mixingProfile == MIXING_PROFILE_RAW) return 1.0;

        double waveformGain;
        if (waveType == 1) {
            waveformGain = CONSERVATIVE_SQUARE_GAIN;
        } else if (waveType == 0) {
            waveformGain = CONSERVATIVE_SINE_GAIN;
        } else {
            // Saw, triangle and uniform white noise have an RMS near 1/sqrt(3).
            waveformGain = CONSERVATIVE_RAMP_GAIN;
        }

        if (mixingProfile == MIXING_PROFILE_BALANCED) {
            waveformGain *= Math.sqrt(2.0);
        } else if (mixingProfile == MIXING_PROFILE_SPEAKER_SAFE) {
            waveformGain /= Math.sqrt(2.0);
        }
        return waveformGain / Math.sqrt(Math.max(1, voiceCount));
    }

    static double outputCeilingForProfile(int mixingProfile) {
        return mixingProfile == MIXING_PROFILE_SPEAKER_SAFE
                ? SPEAKER_SAFE_OUTPUT_CEILING : OUTPUT_CEILING;
    }

    private static double polyBlep(double t, double dt) {
        if (t < dt) {
            double x = t / dt;
            return x + x - x * x - 1.0;
        }
        if (t > 1.0 - dt) {
            double x = (t - 1.0) / dt;
            return x * x + x + x + 1.0;
        }
        return 0.0;
    }

    static final class PeakLimiter {
        private static final double GAIN_RELEASE_MS = 100.0;
        private static final double LOOKAHEAD_MS = 10.0;

        private final double[] delayL;
        private final double[] delayR;
        private final double[] peakValues;
        private final long[] peakIndices;
        private final double gainReleaseCoeff;
        private final double outputCeiling;

        private int delayIndex;
        private int peakHead;
        private int peakCount;
        private long sampleIndex;
        private double smoothedMasterGain = 1.0;
        private double masterGainTarget = 1.0;
        private double masterGainStep;
        private int masterGainFramesRemaining;
        private double gain = 1.0;
        private double attackTarget = 1.0;
        private double attackStep;
        private double outputL;
        private double outputR;

        PeakLimiter(int sampleRate) {
            this(sampleRate, MIXING_PROFILE_RAW);
        }

        PeakLimiter(int sampleRate, int mixingProfile) {
            int safeSampleRate = Math.max(1, sampleRate);
            int lookaheadSamples = Math.max(1,
                    (int) Math.round(LOOKAHEAD_MS * 0.001 * safeSampleRate));
            delayL = new double[lookaheadSamples];
            delayR = new double[lookaheadSamples];
            peakValues = new double[lookaheadSamples + 1];
            peakIndices = new long[lookaheadSamples + 1];
            int safeMixingProfile = mixingProfile == MIXING_PROFILE_CONSERVATIVE
                    || mixingProfile == MIXING_PROFILE_BALANCED
                    || mixingProfile == MIXING_PROFILE_SPEAKER_SAFE
                    ? mixingProfile : MIXING_PROFILE_RAW;
            outputCeiling = outputCeilingForProfile(safeMixingProfile);
            gainReleaseCoeff = 1.0 - Math.exp(-1.0 /
                    (GAIN_RELEASE_MS * 0.001 * safeSampleRate));
        }

        void process(double inputL, double inputR, double masterVolume) {
            process(inputL, inputR, masterVolume, 0.0, 0.0);
        }

        void process(double inputL, double inputR, double masterVolume,
                     double peakBudgetL, double peakBudgetR) {
            process(inputL, inputR, masterVolume, peakBudgetL, peakBudgetR, 2);
        }

        void process(double inputL, double inputR, double masterVolume,
                     double peakBudgetL, double peakBudgetR, int contributingVoices) {
            double divisor = Math.max(1, contributingVoices);
            process(inputL, inputR, masterVolume, peakBudgetL, peakBudgetR,
                    peakBudgetL / divisor, peakBudgetR / divisor, contributingVoices);
        }

        void process(double inputL, double inputR, double masterVolume,
                     double peakBudgetL, double peakBudgetR,
                     double maxVoiceBudgetL, double maxVoiceBudgetR,
                     int contributingVoices) {
            double master = clamp(masterVolume, 0.0, 2.0);
            if (master != masterGainTarget) {
                masterGainTarget = master;
                masterGainFramesRemaining = delayL.length;
                masterGainStep = (masterGainTarget - smoothedMasterGain)
                        / masterGainFramesRemaining;
            }
            if (masterGainFramesRemaining > 0) {
                smoothedMasterGain += masterGainStep;
                masterGainFramesRemaining--;
                if (masterGainFramesRemaining == 0) {
                    smoothedMasterGain = masterGainTarget;
                    masterGainStep = 0.0;
                }
            }

            double delayedL = delayL[delayIndex];
            double delayedR = delayR[delayIndex];
            double scaledL = inputL * smoothedMasterGain;
            double scaledR = inputR * smoothedMasterGain;
            delayL[delayIndex] = scaledL;
            delayR[delayIndex] = scaledR;
            delayIndex = (delayIndex + 1) % delayL.length;

            // Maintain the maximum peak over the delayed sample's complete future
            // window with an allocation-free monotonic queue (amortized O(1)).
            long oldestIndex = sampleIndex - delayL.length;
            while (peakCount > 0 && peakIndices[peakHead] < oldestIndex) {
                peakHead = (peakHead + 1) % peakValues.length;
                peakCount--;
            }

            double currentPeak = Math.max(Math.abs(scaledL), Math.abs(scaledR));
            while (peakCount > 0) {
                int last = (peakHead + peakCount - 1) % peakValues.length;
                if (peakValues[last] > currentPeak) break;
                peakCount--;
            }
            int insert = (peakHead + peakCount) % peakValues.length;
            peakValues[insert] = currentPeak;
            peakIndices[insert] = sampleIndex;
            peakCount++;

            sampleIndex++;

            // A shared window gain preserves waveform shape; unlike a per-sample
            // safety gain it cannot flatten each individual peak.
            double windowPeak = peakValues[peakHead];

            double targetGain = windowPeak > outputCeiling
                    ? outputCeiling / windowPeak : 1.0;

            // A new window maximum always enters at the far end of the lookahead
            // buffer. Use that full deadline to reach its required gain instead of
            // stepping the gain immediately. Never slow an attack already in flight:
            // an earlier peak may have a nearer deadline.
            if (targetGain < gain) {
                attackTarget = Math.min(attackTarget, targetGain);
                double requiredStep = (attackTarget - gain) / delayL.length;
                attackStep = Math.min(attackStep, requiredStep);
            }

            if (attackStep < 0.0 && gain > attackTarget) {
                gain = Math.max(attackTarget, gain + attackStep);
            } else {
                attackTarget = 1.0;
                attackStep = 0.0;
                gain = Math.min(targetGain,
                        gain + (1.0 - gain) * gainReleaseCoeff);
            }

            // Once the entire lookahead window is silent there is no waveform
            // left to protect. Reset only the dynamic bus state so a completed
            // dense chord cannot attenuate the next unrelated sound. Waiting for
            // both the current and delayed samples keeps release tails continuous.
            boolean drainedWindow = contributingVoices == 0
                    && currentPeak <= 1.0e-12
                    && windowPeak <= 1.0e-12
                    && Math.abs(delayedL) <= 1.0e-12
                    && Math.abs(delayedR) <= 1.0e-12;
            if (drainedWindow) {
                gain = 1.0;
                attackTarget = 1.0;
                attackStep = 0.0;
                peakHead = 0;
                peakCount = 0;
            }

            outputL = delayedL * gain;
            outputR = delayedR * gain;
        }

        double getOutputL() {
            return outputL;
        }

        double getOutputR() {
            return outputR;
        }

        double getGain() {
            return gain;
        }

        int getLatencySamples() {
            return delayL.length;
        }

        void reset() {
            for (int i = 0; i < delayL.length; i++) {
                delayL[i] = 0.0;
                delayR[i] = 0.0;
            }
            delayIndex = 0;
            peakHead = 0;
            peakCount = 0;
            sampleIndex = 0;
            smoothedMasterGain = 1.0;
            masterGainTarget = 1.0;
            masterGainStep = 0.0;
            masterGainFramesRemaining = 0;
            gain = 1.0;
            attackTarget = 1.0;
            attackStep = 0.0;
            outputL = 0.0;
            outputR = 0.0;
        }
    }

    private static double clamp(double value, double minValue, double maxValue) {
        if (value < minValue) return minValue;
        if (value > maxValue) return maxValue;
        return value;
    }
}
