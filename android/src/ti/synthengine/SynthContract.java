package ti.synthengine;

import java.util.Map;

/**
 * TiSynthEngine public contract validation.
 *
 * This class deliberately has no Titanium or Android dependencies so the API
 * contract can be regression-tested on the host without touching the DSP.
 */
final class SynthContract {

    static final String API_VERSION = "1.0.0";

    static final int DEFAULT_MAX_VOICES = 8;
    static final String DEFAULT_MIXING_PROFILE = "conservative";
    static final String DEFAULT_AUDIO_SESSION = "playback";
    static final double DEFAULT_FREQUENCY_HZ = 440.0;
    static final int DEFAULT_DURATION_MS = 500;
    static final int DEFAULT_ATTACK_MS = 50;
    static final int DEFAULT_RELEASE_MS = 50;
    static final double DEFAULT_VOLUME = 1.0;
    static final int DEFAULT_WAVE_TYPE = 0;
    static final double DEFAULT_PAN = 0.0;
    static final double DEFAULT_FREQUENCY_END_HZ = 0.0;
    static final double DEFAULT_LFO_FREQUENCY_HZ = 0.0;
    static final double DEFAULT_LFO_DEPTH = 0.0;
    static final double DEFAULT_NOISE_RATE_HZ = 0.0;
    static final double DEFAULT_LOW_PASS_HZ = 0.0;
    static final double DEFAULT_PULSE_WIDTH = 0.5;
    static final int DEFAULT_BIT_DEPTH = 0;
    static final double DEFAULT_PAN_SPREAD = 0.0;
    static final double DEFAULT_BPM = 120.0;
    static final double DEFAULT_SWING = 0.0;
    static final double DEFAULT_STEP_DURATION_MS = 100.0;

    static final int MIN_MAX_VOICES = 1;
    static final int MAX_MAX_VOICES = 32;
    static final double MIN_FREQUENCY_HZ = 1.0;
    static final int MIN_DURATION_MS = 1;
    static final int MAX_DURATION_MS = 60000;
    static final int MIN_ATTACK_MS = 0;
    static final int MAX_ATTACK_MS = 2000;
    static final int MIN_RELEASE_MS = 0;
    static final int MAX_RELEASE_MS = 4000;
    static final double MIN_VOLUME = 0.0;
    static final double MAX_VOLUME = 2.0;
    static final int MIN_WAVE_TYPE = 0;
    static final int MAX_WAVE_TYPE = 5;
    static final int MIN_WAVETABLE_SAMPLES = 2;
    static final int MAX_WAVETABLE_SAMPLES = 64;
    static final double MIN_PULSE_WIDTH = 0.05;
    static final double MAX_PULSE_WIDTH = 0.95;
    static final int MIN_BIT_DEPTH = 2;
    static final int MAX_BIT_DEPTH = 16;
    static final int MAX_PATTERN_STEPS = 512;
    static final double MIN_BPM = 20.0;
    static final double MAX_BPM = 300.0;
    static final double MIN_SWING = -0.49;
    static final double MAX_SWING = 0.49;
    static final double MAX_LFO = 20000.0;

    static final class Validation {
        final boolean accepted;
        final String key;
        final String reason;
        final Object value;

        private Validation(boolean accepted, String key, String reason, Object value) {
            this.accepted = accepted;
            this.key = key;
            this.reason = reason;
            this.value = value;
        }

        static Validation accepted() {
            return new Validation(true, null, null, null);
        }

        static Validation rejected(String key, String reason, Object value) {
            return new Validation(false, key, reason, value);
        }
    }

    private SynthContract() {}

    static Validation validateStartEngine(Map<String, Object> options) {
        Validation result = rejectUnknownOptions(options, "maxVoices", "mixingProfile",
                "audioSession");
        if (!result.accepted) return result;
        result = optionalInteger(options, "maxVoices",
                MIN_MAX_VOICES, MAX_MAX_VOICES);
        if (!result.accepted) return result;

        if (contains(options, "mixingProfile")) {
            Object raw = options.get("mixingProfile");
            if (!(raw instanceof String)) {
                return Validation.rejected("mixingProfile", "not_string", raw);
            }
            String profile = ((String) raw).trim();
            if (!("raw".equalsIgnoreCase(profile)
                    || "conservative".equalsIgnoreCase(profile)
                    || "balanced".equalsIgnoreCase(profile)
                    || "speakerSafe".equalsIgnoreCase(profile))) {
                return Validation.rejected("mixingProfile", "unsupported_value", raw);
            }
        }

        if (contains(options, "audioSession")) {
            Object raw = options.get("audioSession");
            if (!(raw instanceof String)) {
                return Validation.rejected("audioSession", "not_string", raw);
            }
            String session = ((String) raw).trim();
            if (!("playback".equalsIgnoreCase(session)
                    || "ambient".equalsIgnoreCase(session))) {
                return Validation.rejected("audioSession", "unsupported_value", raw);
            }
        }
        return Validation.accepted();
    }

    static Validation validateSetVolume(Map<String, Object> options) {
        Validation result = rejectUnknownOptions(options, "volume");
        if (!result.accepted) return result;
        return optionalDouble(options, "volume", MIN_VOLUME, MAX_VOLUME);
    }

    static Validation validateTone(Map<String, Object> options, double maxFrequencyHz) {
        Validation result = rejectUnknownOptions(options,
                "frequency", "note", "duration", "attack", "release", "volume",
                "waveType", "pan", "panEnd", "frequencyEnd", "lfoFreq", "lfoDepth",
                "noiseRate", "noiseRateEnd", "lowPassHz", "pulseWidth", "pulseWidthEnd",
                "waveTable", "bitDepth");
        if (!result.accepted) return result;

        boolean hasFrequency = contains(options, "frequency");
        boolean hasNote = contains(options, "note");
        if (hasFrequency && hasNote) {
            return Validation.rejected("frequency", "conflicting_pitch_fields", options.get("frequency"));
        }
        if (hasFrequency) {
            result = optionalDouble(options, "frequency", MIN_FREQUENCY_HZ, maxFrequencyHz);
            if (!result.accepted) return result;
        }
        if (hasNote) {
            result = validNote(options.get("note"), "note");
            if (!result.accepted) return result;
        }

        result = validateVoiceOptions(options, maxFrequencyHz, true);
        if (!result.accepted) return result;
        result = validateWaveTableRequirement(options, DEFAULT_WAVE_TYPE, false);
        if (!result.accepted) return result;
        return validateExplicitEnvelope(options);
    }

    static Validation validateChord(Map<String, Object> options, double maxFrequencyHz) {
        Validation result = rejectUnknownOptions(options,
                "frequencies", "notes", "duration", "attack", "release", "volume",
                "waveType", "panSpread", "pans", "noiseRate", "lowPassHz",
                "pulseWidth", "waveTable", "bitDepth");
        if (!result.accepted) return result;

        boolean hasFrequencies = contains(options, "frequencies");
        boolean hasNotes = contains(options, "notes");
        if (hasFrequencies == hasNotes) {
            return Validation.rejected("frequencies", hasFrequencies
                    ? "conflicting_pitch_fields" : "missing_required", null);
        }

        Object rawPitches = options.get(hasFrequencies ? "frequencies" : "notes");
        if (!(rawPitches instanceof Object[])) {
            return Validation.rejected(hasFrequencies ? "frequencies" : "notes", "not_array", rawPitches);
        }
        Object[] pitches = (Object[]) rawPitches;
        if (pitches.length < 1 || pitches.length > MAX_MAX_VOICES) {
            return Validation.rejected(hasFrequencies ? "frequencies" : "notes", "invalid_voice_count", pitches.length);
        }
        for (Object pitch : pitches) {
            result = hasFrequencies
                    ? finiteDoubleInRange(pitch, "frequencies", MIN_FREQUENCY_HZ, maxFrequencyHz)
                    : validNote(pitch, "notes");
            if (!result.accepted) return result;
        }

        result = validateVoiceOptions(options, maxFrequencyHz, false);
        if (!result.accepted) return result;
        result = validateWaveTableRequirement(options, DEFAULT_WAVE_TYPE, false);
        if (!result.accepted) return result;
        result = optionalDouble(options, "panSpread", 0.0, 1.0);
        if (!result.accepted) return result;

        if (contains(options, "pans")) {
            Object rawPans = options.get("pans");
            if (!(rawPans instanceof Object[])) {
                return Validation.rejected("pans", "not_array", rawPans);
            }
            Object[] pans = (Object[]) rawPans;
            if (pans.length > pitches.length) {
                return Validation.rejected("pans", "too_many_values", pans.length);
            }
            for (Object pan : pans) {
                result = finiteDoubleInRange(pan, "pans", -1.0, 1.0);
                if (!result.accepted) return result;
            }
        }
        return validateExplicitEnvelope(options);
    }

    @SuppressWarnings("unchecked")
    static Validation validatePattern(Map<String, Object> options, double maxFrequencyHz) {
        Validation result = rejectUnknownOptions(options,
                "steps", "bpm", "swing", "stepDuration", "noteValue", "attack",
                "release", "volume", "waveType", "pan", "panEnd",
                "frequencyEnd", "lfoFreq", "lfoDepth", "noiseRate", "noiseRateEnd",
                "lowPassHz", "pulseWidth", "pulseWidthEnd", "waveTable", "bitDepth");
        if (!result.accepted) return result;

        Object rawSteps = options != null ? options.get("steps") : null;
        if (!(rawSteps instanceof Object[])) {
            return Validation.rejected("steps", "not_array", rawSteps);
        }
        Object[] steps = (Object[]) rawSteps;
        if (steps.length < 1 || steps.length > MAX_PATTERN_STEPS) {
            return Validation.rejected("steps", "invalid_step_count", steps.length);
        }

        result = optionalDouble(options, "bpm", MIN_BPM, MAX_BPM);
        if (!result.accepted) return result;
        result = optionalDouble(options, "swing", MIN_SWING, MAX_SWING);
        if (!result.accepted) return result;
        result = optionalDouble(options, "stepDuration", MIN_DURATION_MS, MAX_DURATION_MS);
        if (!result.accepted) return result;
        result = optionalNoteValue(options, "noteValue");
        if (!result.accepted) return result;
        result = validateVoiceOptions(options, maxFrequencyHz, true);
        if (!result.accepted) return result;

        int globalWaveType = integerValue(options, "waveType", DEFAULT_WAVE_TYPE);
        boolean globalHasWaveTable = contains(options, "waveTable");

        for (Object rawStep : steps) {
            if (!(rawStep instanceof Map)) {
                return Validation.rejected("steps", "invalid_step_type", rawStep);
            }
            Map<String, Object> step = (Map<String, Object>) rawStep;
            result = rejectUnknownOptions(step,
                    "rest", "frequency", "note", "duration", "noteValue", "attack",
                    "release", "volume", "waveType", "pan", "panEnd", "frequencyEnd",
                    "lfoFreq", "lfoDepth", "noiseRate", "noiseRateEnd", "lowPassHz",
                    "pulseWidth", "pulseWidthEnd", "waveTable", "bitDepth");
            if (!result.accepted) return result;

            boolean rest = Boolean.TRUE.equals(step.get("rest"));
            if (contains(step, "rest") && !(step.get("rest") instanceof Boolean)) {
                return Validation.rejected("rest", "not_boolean", step.get("rest"));
            }
            boolean hasFrequency = contains(step, "frequency");
            boolean hasNote = contains(step, "note");
            int pitchFields = (rest ? 1 : 0) + (hasFrequency ? 1 : 0) + (hasNote ? 1 : 0);
            if (pitchFields != 1) {
                return Validation.rejected("steps", pitchFields == 0
                        ? "missing_pitch_or_rest" : "conflicting_pitch_fields", rawStep);
            }
            if (hasFrequency) {
                result = optionalDouble(step, "frequency", MIN_FREQUENCY_HZ, maxFrequencyHz);
                if (!result.accepted) return result;
            }
            if (hasNote) {
                result = validNote(step.get("note"), "note");
                if (!result.accepted) return result;
            }
            result = optionalDouble(step, "duration", MIN_DURATION_MS, MAX_DURATION_MS);
            if (!result.accepted) return result;
            result = optionalNoteValue(step, "noteValue");
            if (!result.accepted) return result;
            result = validateVoiceOptions(step, maxFrequencyHz, true);
            if (!result.accepted) return result;
            if (!rest) {
                result = validateWaveTableRequirement(step, globalWaveType, globalHasWaveTable);
                if (!result.accepted) return result;
                result = validatePatternEnvelope(options, step);
                if (!result.accepted) return result;
            }
        }

        return Validation.accepted();
    }

    private static Validation validateWaveTableRequirement(Map<String, Object> options,
            int fallbackWaveType, boolean fallbackHasWaveTable) {
        int waveType = integerValue(options, "waveType", fallbackWaveType);
        boolean hasWaveTable = contains(options, "waveTable") || fallbackHasWaveTable;
        return waveType == 5 && !hasWaveTable
                ? Validation.rejected("waveTable", "missing_required", null)
                : Validation.accepted();
    }

    private static int integerValue(Map<String, Object> options, String key, int fallback) {
        if (!contains(options, key)) return fallback;
        return ((Number) options.get(key)).intValue();
    }

    private static Validation validatePatternEnvelope(Map<String, Object> pattern,
            Map<String, Object> step) {
        int duration = (int) Math.round(resolvePatternStepDuration(pattern, step));
        boolean attackExplicit = contains(step, "attack") || contains(pattern, "attack");
        Object attackValue = contains(step, "attack")
                ? step.get("attack") : pattern.get("attack");
        int attack = contains(step, "attack")
                ? readInteger(step, "attack", DEFAULT_ATTACK_MS)
                : readInteger(pattern, "attack", DEFAULT_ATTACK_MS);
        if (attackExplicit && attack >= duration) {
            return Validation.rejected("attack", "attack_greater_or_equal_duration",
                    attackValue);
        }

        boolean releaseExplicit = contains(step, "release") || contains(pattern, "release");
        Object releaseValue = contains(step, "release")
                ? step.get("release") : pattern.get("release");
        int release = contains(step, "release")
                ? readInteger(step, "release", DEFAULT_RELEASE_MS)
                : readInteger(pattern, "release", DEFAULT_RELEASE_MS);
        if (releaseExplicit && release > Math.max(0, duration - attack)) {
            return Validation.rejected("release", "release_exceeds_remaining_duration",
                    releaseValue);
        }
        return Validation.accepted();
    }

    private static double resolvePatternStepDuration(Map<String, Object> pattern,
            Map<String, Object> step) {
        if (contains(step, "duration")) {
            return finiteDouble(step.get("duration"));
        }
        double bpm = contains(pattern, "bpm") ? finiteDouble(pattern.get("bpm")) : 120.0;
        double quarterMs = 60000.0 / bpm;
        if (contains(step, "noteValue")) {
            return patternNoteValueMs((String) step.get("noteValue"), quarterMs);
        }
        if (contains(pattern, "noteValue")) {
            return patternNoteValueMs((String) pattern.get("noteValue"), quarterMs);
        }
        if (contains(pattern, "stepDuration")) {
            return finiteDouble(pattern.get("stepDuration"));
        }
        return 100.0;
    }

    private static double patternNoteValueMs(String noteValue, double quarterMs) {
        switch (noteValue) {
            case "1n": return quarterMs * 4.0;
            case "2n": return quarterMs * 2.0;
            case "2n.": return quarterMs * 3.0;
            case "4n": return quarterMs;
            case "4n.": return quarterMs * 1.5;
            case "8n": return quarterMs * 0.5;
            case "8n.": return quarterMs * 0.75;
            case "8t": return quarterMs / 3.0;
            case "16n": return quarterMs * 0.25;
            case "16t": return quarterMs / 6.0;
            default: return quarterMs;
        }
    }

    static double parseNoteToFrequency(String noteName) {
        if (noteName == null || noteName.trim().isEmpty()) return -1.0;
        String value = noteName.trim();
        String upper = value.toUpperCase();
        int position;
        int semitone;

        if (upper.startsWith("SOL")) { semitone = 7; position = 3; }
        else if (upper.startsWith("DO")) { semitone = 0; position = 2; }
        else if (upper.startsWith("RE")) { semitone = 2; position = 2; }
        else if (upper.startsWith("MI")) { semitone = 4; position = 2; }
        else if (upper.startsWith("FA")) { semitone = 5; position = 2; }
        else if (upper.startsWith("LA")) { semitone = 9; position = 2; }
        else if (upper.startsWith("SI") || upper.startsWith("TI")) { semitone = 11; position = 2; }
        else {
            position = 1;
            switch (upper.charAt(0)) {
                case 'C': semitone = 0; break;
                case 'D': semitone = 2; break;
                case 'E': semitone = 4; break;
                case 'F': semitone = 5; break;
                case 'G': semitone = 7; break;
                case 'A': semitone = 9; break;
                case 'B': semitone = 11; break;
                default: return -1.0;
            }
        }

        if (position < value.length()) {
            char accidental = value.charAt(position);
            if (accidental == '#') { semitone += 1; position += 1; }
            else if (accidental == 'b') { semitone -= 1; position += 1; }
        }

        int octave = 4;
        if (position < value.length()) {
            char digit = value.charAt(position);
            if (digit < '0' || digit > '9' || position + 1 != value.length()) return -1.0;
            octave = digit - '0';
        }
        int midi = (octave + 1) * 12 + semitone;
        return 440.0 * Math.pow(2.0, (midi - 69.0) / 12.0);
    }

    private static Validation validateVoiceOptions(Map<String, Object> options,
            double maxFrequencyHz, boolean allowSweep) {
        Validation result = optionalInteger(options, "duration", MIN_DURATION_MS, MAX_DURATION_MS);
        if (!result.accepted) return result;
        result = optionalInteger(options, "attack", MIN_ATTACK_MS, MAX_ATTACK_MS);
        if (!result.accepted) return result;
        result = optionalInteger(options, "release", MIN_RELEASE_MS, MAX_RELEASE_MS);
        if (!result.accepted) return result;
        result = optionalDouble(options, "volume", MIN_VOLUME, MAX_VOLUME);
        if (!result.accepted) return result;
        result = optionalInteger(options, "waveType", MIN_WAVE_TYPE, MAX_WAVE_TYPE);
        if (!result.accepted) return result;
        result = optionalDouble(options, "pan", -1.0, 1.0);
        if (!result.accepted) return result;
        result = optionalDouble(options, "noiseRate", 0.0, maxFrequencyHz);
        if (!result.accepted) return result;
        result = optionalDouble(options, "lowPassHz", 0.0, maxFrequencyHz);
        if (!result.accepted) return result;
        result = optionalDouble(options, "pulseWidth", MIN_PULSE_WIDTH, MAX_PULSE_WIDTH);
        if (!result.accepted) return result;
        result = optionalWaveTable(options, "waveTable");
        if (!result.accepted) return result;
        result = optionalBitDepth(options, "bitDepth");
        if (!result.accepted) return result;

        if (allowSweep) {
            result = optionalDouble(options, "panEnd", -1.0, 1.0);
            if (!result.accepted) return result;
            result = optionalDouble(options, "frequencyEnd", 0.0, maxFrequencyHz);
            if (!result.accepted) return result;
            result = optionalDouble(options, "lfoFreq", 0.0, MAX_LFO);
            if (!result.accepted) return result;
            result = optionalDouble(options, "lfoDepth", 0.0, MAX_LFO);
            if (!result.accepted) return result;
            result = optionalDouble(options, "noiseRateEnd", 0.0, maxFrequencyHz);
            if (!result.accepted) return result;
            result = optionalPulseWidthEnd(options, "pulseWidthEnd");
            if (!result.accepted) return result;
        }
        return Validation.accepted();
    }

    private static Validation optionalWaveTable(Map<String, Object> options, String key) {
        if (!contains(options, key)) return Validation.accepted();
        Object raw = options.get(key);
        if (!(raw instanceof Object[])) return Validation.rejected(key, "not_array", raw);
        Object[] values = (Object[]) raw;
        if (values.length < MIN_WAVETABLE_SAMPLES || values.length > MAX_WAVETABLE_SAMPLES) {
            return Validation.rejected(key, "invalid_sample_count", values.length);
        }
        for (Object value : values) {
            Validation result = finiteDoubleInRange(value, key, -1.0, 1.0);
            if (!result.accepted) return result;
        }
        return Validation.accepted();
    }

    private static Validation optionalBitDepth(Map<String, Object> options, String key) {
        if (!contains(options, key)) return Validation.accepted();
        Object raw = options.get(key);
        Double parsed = finiteDouble(raw);
        if (parsed == null) return Validation.rejected(key, "non_numeric_or_non_finite", raw);
        double rounded = Math.rint(parsed);
        if (Math.abs(parsed - rounded) > 1e-9) return Validation.rejected(key, "not_integer", raw);
        if (rounded != 0.0 && (rounded < MIN_BIT_DEPTH || rounded > MAX_BIT_DEPTH)) {
            return Validation.rejected(key, "out_of_range", raw);
        }
        return Validation.accepted();
    }

    private static Validation optionalPulseWidthEnd(Map<String, Object> options, String key) {
        if (!contains(options, key)) return Validation.accepted();
        Object raw = options.get(key);
        Double parsed = finiteDouble(raw);
        if (parsed == null) return Validation.rejected(key, "non_numeric_or_non_finite", raw);
        if (parsed != 0.0 && (parsed < MIN_PULSE_WIDTH || parsed > MAX_PULSE_WIDTH)) {
            return Validation.rejected(key, "out_of_range", raw);
        }
        return Validation.accepted();
    }

    private static Validation validateExplicitEnvelope(Map<String, Object> options) {
        if (options == null) return Validation.accepted();
        int duration = readInteger(options, "duration", DEFAULT_DURATION_MS);
        int attack = readInteger(options, "attack", DEFAULT_ATTACK_MS);
        if (contains(options, "attack") && attack >= duration) {
            return Validation.rejected("attack", "attack_greater_or_equal_duration", attack);
        }
        if (contains(options, "release") && readInteger(options, "release", DEFAULT_RELEASE_MS)
                > Math.max(0, duration - attack)) {
            return Validation.rejected("release", "release_exceeds_remaining_duration",
                    options.get("release"));
        }
        return Validation.accepted();
    }

    private static Validation optionalNoteValue(Map<String, Object> options, String key) {
        if (!contains(options, key)) return Validation.accepted();
        Object raw = options.get(key);
        if (!(raw instanceof String)) return Validation.rejected(key, "not_string", raw);
        String value = ((String) raw).trim();
        if ("1n".equals(value) || "2n".equals(value) || "2n.".equals(value)
                || "4n".equals(value) || "4n.".equals(value)
                || "8n".equals(value) || "8n.".equals(value) || "8t".equals(value)
                || "16n".equals(value) || "16t".equals(value)) {
            return Validation.accepted();
        }
        return Validation.rejected(key, "unsupported_note_value", raw);
    }

    private static Validation validNote(Object raw, String key) {
        if (!(raw instanceof String)) return Validation.rejected(key, "not_string", raw);
        return parseNoteToFrequency((String) raw) >= MIN_FREQUENCY_HZ
                ? Validation.accepted()
                : Validation.rejected(key, "invalid_note", raw);
    }

    private static Validation optionalInteger(Map<String, Object> options, String key,
            int min, int max) {
        if (!contains(options, key)) return Validation.accepted();
        Double parsed = finiteDouble(options.get(key));
        if (parsed == null) return Validation.rejected(key, "non_numeric_or_non_finite", options.get(key));
        double rounded = Math.rint(parsed);
        if (Math.abs(parsed - rounded) > 1e-9) {
            return Validation.rejected(key, "not_integer", options.get(key));
        }
        if (rounded < min || rounded > max) {
            return Validation.rejected(key, "out_of_range", options.get(key));
        }
        return Validation.accepted();
    }

    private static Validation optionalDouble(Map<String, Object> options, String key,
            double min, double max) {
        if (!contains(options, key)) return Validation.accepted();
        return finiteDoubleInRange(options.get(key), key, min, max);
    }

    private static Validation finiteDoubleInRange(Object raw, String key, double min, double max) {
        Double parsed = finiteDouble(raw);
        if (parsed == null) return Validation.rejected(key, "non_numeric_or_non_finite", raw);
        if (parsed < min || parsed > max) return Validation.rejected(key, "out_of_range", raw);
        return Validation.accepted();
    }

    private static Validation rejectUnknownOptions(Map<String, Object> options,
            String... supportedKeys) {
        if (options == null) return Validation.accepted();
        for (String key : options.keySet()) {
            boolean supported = false;
            for (String supportedKey : supportedKeys) {
                if (supportedKey.equals(key)) {
                    supported = true;
                    break;
                }
            }
            if (!supported) {
                return Validation.rejected(key, "unsupported_option", options.get(key));
            }
        }
        return Validation.accepted();
    }

    private static boolean contains(Map<String, Object> options, String key) {
        return options != null && options.containsKey(key);
    }

    private static int readInteger(Map<String, Object> options, String key, int defaultValue) {
        if (!contains(options, key)) return defaultValue;
        return (int) Math.rint(finiteDouble(options.get(key)));
    }

    private static Double finiteDouble(Object value) {
        if (value instanceof Number) {
            double parsed = ((Number) value).doubleValue();
            return Double.isFinite(parsed) ? parsed : null;
        }
        return null;
    }
}
