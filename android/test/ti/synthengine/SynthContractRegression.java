package ti.synthengine;

import java.util.HashMap;
import java.util.Map;

public final class SynthContractRegression {
    private static final double MAX_FREQUENCY = 19845.0;

    public static void main(String[] args) {
        assertEquals("1.0.0", SynthContract.API_VERSION, "public API version");
        assertTrue(SynthContract.validateStartEngine(null).accepted, "omitted start options");
        assertEquals(8, SynthContract.DEFAULT_MAX_VOICES, "default voices");
        assertEquals("conservative", SynthContract.DEFAULT_MIXING_PROFILE, "default profile");
        assertEquals(32, SynthContract.MAX_MAX_VOICES, "voice capacity");

        assertAccepted(SynthContract.validateTone(map(), MAX_FREQUENCY), "default tone");
        assertAccepted(SynthContract.validateTone(map("frequency", 440.0), MAX_FREQUENCY), "frequency tone");
        assertAccepted(SynthContract.validateTone(map("note", "C#4"), MAX_FREQUENCY), "note tone");
        assertRejected(SynthContract.validateTone(map("frequencies", new Object[]{440.0}), MAX_FREQUENCY),
                "playTone sequence removed");
        assertRejected(SynthContract.validateTone(map("volume", 3.0), MAX_FREQUENCY), "strict volume");
        assertRejected(SynthContract.validateTone(map("volume", true), MAX_FREQUENCY), "boolean is not numeric");
        assertRejected(SynthContract.validateTone(map("duration", "500"), MAX_FREQUENCY), "numeric string rejected");
        assertRejected(SynthContract.validateTone(map("waveType", 1.5), MAX_FREQUENCY), "strict integer");
        assertRejectedWith(SynthContract.validateTone(map("frequency", null), MAX_FREQUENCY),
                "frequency", "non_numeric_or_non_finite", "present null is not omitted");
        assertRejectedWith(SynthContract.validateTone(map("frequencies", new Object[]{440.0}), MAX_FREQUENCY),
                "frequencies", "unsupported_option", "tone rejects unsupported sequence option neutrally");
        assertRejectedWith(SynthContract.validateTone(map("mystery", true), MAX_FREQUENCY),
                "mystery", "unsupported_option", "tone rejects unknown option");
        assertAccepted(SynthContract.validateTone(map(
                "frequency", 440.0,
                "noiseRate", 900.0,
                "noiseRateEnd", 7200.0,
                "lowPassHz", 2800.0,
                "pulseWidth", 0.25,
                "pulseWidthEnd", 0.75,
                "bitDepth", 8), MAX_FREQUENCY), "character controls");
        assertAccepted(SynthContract.validateTone(map(
                "frequency", 440.0,
                "waveType", 5,
                "waveTable", new Object[]{-1.0, -0.2, 0.7, 1.0, 0.1}),
                MAX_FREQUENCY), "custom wavetable");
        assertRejectedWith(SynthContract.validateTone(map("waveType", 5), MAX_FREQUENCY),
                "waveTable", "missing_required", "wavetable waveform needs samples");
        assertRejectedWith(SynthContract.validateTone(map(
                "waveTable", new Object[]{0.0}), MAX_FREQUENCY),
                "waveTable", "invalid_sample_count", "wavetable minimum");
        assertRejectedWith(SynthContract.validateTone(map(
                "waveTable", new Object[]{-1.0, 1.1}), MAX_FREQUENCY),
                "waveTable", "out_of_range", "wavetable sample range");
        assertRejectedWith(SynthContract.validateTone(map("bitDepth", 1), MAX_FREQUENCY),
                "bitDepth", "out_of_range", "bit depth excludes one bit");
        assertRejectedWith(SynthContract.validateTone(map("pulseWidthEnd", 0.99), MAX_FREQUENCY),
                "pulseWidthEnd", "out_of_range", "pulse-width sweep range");

        assertAccepted(SynthContract.validateChord(
                map("frequencies", new Object[]{440.0, 660.0}), MAX_FREQUENCY), "frequency chord");
        assertAccepted(SynthContract.validateChord(
                map("notes", new Object[]{"C4", "E4", "G4"}), MAX_FREQUENCY), "note chord");
        assertAccepted(SynthContract.validateChord(
                map("frequencies", new Object[]{220.0, 330.0}, "pulseWidth", 0.3,
                        "lowPassHz", 4000.0, "bitDepth", 8), MAX_FREQUENCY),
                "character chord");
        assertRejected(SynthContract.validateChord(
                map("frequencies", new Object[]{}), MAX_FREQUENCY), "empty chord");
        assertRejected(SynthContract.validateChord(
                map("frequencies", new Object[]{440.0}, "notes", new Object[]{"A4"}), MAX_FREQUENCY),
                "ambiguous chord");
        assertRejectedWith(SynthContract.validateChord(
                map("frequencies", new Object[]{440.0}, "frequencyEnd", 880.0), MAX_FREQUENCY),
                "frequencyEnd", "unsupported_option", "chord rejects sweep option");
        assertRejectedWith(SynthContract.validateChord(
                map("frequencies", new Object[]{440.0}, "pan", 0.5), MAX_FREQUENCY),
                "pan", "unsupported_option", "chord rejects ignored pan option");

        Object[] structuredSteps = new Object[]{
                map("note", "C4", "duration", 120),
                map("rest", true, "duration", 60),
                map("frequency", 660.0, "noteValue", "8n")
        };
        assertAccepted(SynthContract.validatePattern(map("steps", structuredSteps), MAX_FREQUENCY),
                "structured pattern");
        assertAccepted(SynthContract.validatePattern(map(
                "steps", new Object[]{map("frequency", 200.0, "noiseRate", 500.0,
                        "noiseRateEnd", 8000.0, "lowPassHz", 3000.0, "bitDepth", 8)}),
                MAX_FREQUENCY), "character pattern step");
        assertAccepted(SynthContract.validatePattern(map(
                "attack", 2,
                "release", 80,
                "steps", new Object[]{map("frequency", 120.0, "duration", 180,
                        "release", 150)}), MAX_FREQUENCY),
                "pattern envelope uses inherited attack");
        assertRejectedWith(SynthContract.validatePattern(map(
                "attack", 40,
                "release", 150,
                "steps", new Object[]{map("frequency", 120.0, "duration", 180)}),
                MAX_FREQUENCY), "release", "release_exceeds_remaining_duration",
                "inherited pattern envelope must fit step duration");
        assertAccepted(SynthContract.validatePattern(map(
                "bpm", 120.0,
                "noteValue", "8n",
                "attack", 20,
                "release", 100,
                "steps", new Object[]{map("note", "A3")}), MAX_FREQUENCY),
                "pattern envelope resolves inherited note-value timing");
        assertAccepted(SynthContract.validatePattern(map(
                "waveType", 5,
                "waveTable", new Object[]{-1.0, 0.0, 1.0, 0.0},
                "steps", new Object[]{map("note", "A4")}), MAX_FREQUENCY),
                "pattern inherits wavetable");
        assertRejectedWith(SynthContract.validatePattern(map(
                "waveType", 5,
                "steps", new Object[]{map("note", "A4")}), MAX_FREQUENCY),
                "waveTable", "missing_required", "pattern wavetable needs samples");
        assertRejectedWith(SynthContract.validatePattern(
                map("steps", new Object[]{"C4"}), MAX_FREQUENCY),
                "steps", "invalid_step_type", "primitive pattern step");
        assertRejectedWith(SynthContract.validatePattern(
                map("steps", new Object[]{map("note", "C4", "value", "8n")}), MAX_FREQUENCY),
                "value", "unsupported_option", "value alias is unsupported");
        assertRejectedWith(SynthContract.validatePattern(
                map("steps", new Object[]{map("note", "C4", "velocity", 0.5)}), MAX_FREQUENCY),
                "velocity", "unsupported_option", "velocity alias is unsupported");
        assertRejectedWith(SynthContract.validatePattern(
                map("steps", structuredSteps, "mystery", true), MAX_FREQUENCY),
                "mystery", "unsupported_option", "pattern rejects unknown root option");
        assertRejectedWith(SynthContract.validatePattern(
                map("steps", structuredSteps, "duration", 120), MAX_FREQUENCY),
                "duration", "unsupported_option", "pattern rejects ignored root duration");
        assertRejectedWith(SynthContract.validatePattern(
                map("steps", new Object[]{map("note", "C4", "mystery", true)}), MAX_FREQUENCY),
                "mystery", "unsupported_option", "pattern rejects unknown step option");
        assertRejected(SynthContract.validatePattern(
                map("steps", steps(513)), MAX_FREQUENCY), "pattern step limit is strict");

        assertRejected(SynthContract.validateStartEngine(map("maxVoices", 33)), "voice upper bound");
        assertRejectedWith(SynthContract.validateStartEngine(map("latency", "low")),
                "latency", "unsupported_option", "start rejects unknown option");
        assertAccepted(SynthContract.validateStartEngine(
                map("maxVoices", 32, "mixingProfile", "speakerSafe")), "stress capacity");
        assertAccepted(SynthContract.validateStartEngine(map("maxVoices", 8)),
                "start without an audio session");
        assertAccepted(SynthContract.validateStartEngine(map("audioSession", "playback")),
                "playback audio session");
        assertAccepted(SynthContract.validateStartEngine(map("audioSession", " Ambient ")),
                "ambient audio session ignores case and whitespace");
        assertRejectedWith(SynthContract.validateStartEngine(map("audioSession", "record")),
                "audioSession", "unsupported_value", "start rejects an unknown audio session");
        assertRejectedWith(SynthContract.validateStartEngine(map("audioSession", 1)),
                "audioSession", "not_string", "start rejects a non-string audio session");
        assertRejectedWith(SynthContract.validateSetVolume(map("gain", 0.5)),
                "gain", "unsupported_option", "volume rejects unknown option");

        System.out.println("SynthContractRegression: all tests passed");
    }

    private static Map<String, Object> map(Object... entries) {
        Map<String, Object> result = new HashMap<>();
        for (int i = 0; i < entries.length; i += 2) {
            result.put((String) entries[i], entries[i + 1]);
        }
        return result;
    }

    private static Object[] steps(int count) {
        Object[] result = new Object[count];
        for (int i = 0; i < count; i++) result[i] = map("frequency", 440.0);
        return result;
    }

    private static void assertAccepted(SynthContract.Validation result, String message) {
        assertTrue(result.accepted, message + " rejected: " + result.key + "/" + result.reason);
    }

    private static void assertRejected(SynthContract.Validation result, String message) {
        assertTrue(!result.accepted, message + " accepted unexpectedly");
    }

    private static void assertRejectedWith(SynthContract.Validation result, String key,
            String reason, String message) {
        assertRejected(result, message);
        assertEquals(key, result.key, message + " key");
        assertEquals(reason, result.reason, message + " reason");
    }

    private static void assertTrue(boolean condition, String message) {
        if (!condition) throw new AssertionError(message);
    }

    private static void assertEquals(Object expected, Object actual, String message) {
        if (!expected.equals(actual)) {
            throw new AssertionError(message + ": expected " + expected + ", got " + actual);
        }
    }
}
