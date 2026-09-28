package ti.synthengine;

/**
 * Thin Java owner for the native Oboe engine. All synthesis and mixing runs in
 * Oboe's real-time float callback; Java only publishes control commands.
 */
public final class SynthThread extends Thread {
    private long nativeHandle;

    public SynthThread(int requestedMaxVoices, int mixingProfile) {
        nativeHandle = nativeCreate(requestedMaxVoices, mixingProfile);
    }

    public int getSampleRate() {
        long handle = nativeHandle;
        return handle != 0 ? nativeGetSampleRate(handle) : 44100;
    }

    public void setMasterVolume(double volume) {
        long handle = nativeHandle;
        if (handle != 0) nativeSetMasterVolume(handle, volume);
    }

    public boolean enqueueNote(
            double frequency, int durationMs, int attackMs, int releaseMs, double volume,
            int waveType, double pan, double panEnd, double freqEnd,
            double lfoFreq, double lfoDepth,
            double noiseRate, double noiseRateEnd, double lowPassHz,
            double pulseWidth, double pulseWidthEnd, int bitDepth, double[] waveTable,
            double[] seqFreqs, int seqStepCount, int seqStepDurMs) {
        long handle = nativeHandle;
        return handle == 0 || nativeEnqueueNote(handle, frequency, durationMs,
                attackMs, releaseMs, volume, waveType, pan, panEnd, freqEnd,
                lfoFreq, lfoDepth, noiseRate, noiseRateEnd, lowPassHz,
                pulseWidth, pulseWidthEnd, bitDepth, waveTable,
                seqFreqs, seqStepCount, seqStepDurMs);
    }

    public boolean enqueueChord(
            double[] frequencies, int durationMs, int attackMs, int releaseMs,
            double volume, int waveType, double[] pans, double noiseRate,
            double lowPassHz, double pulseWidth, int bitDepth, double[] waveTable) {
        long handle = nativeHandle;
        return handle == 0 || nativeEnqueueChord(handle, frequencies, durationMs,
                attackMs, releaseMs, volume, waveType, pans, noiseRate,
                lowPassHz, pulseWidth, bitDepth, waveTable);
    }

    public boolean requestStopAll() {
        long handle = nativeHandle;
        return handle == 0 || nativeStopAll(handle);
    }

    public boolean requestShutdown() {
        long handle = nativeHandle;
        if (handle == 0) return false;
        nativeHandle = 0;
        nativeDestroy(handle);
        return false;
    }

    @Override
    public void run() {
        long handle = nativeHandle;
        if (handle != 0) nativeStart(handle);
    }

    private static native long nativeCreate(int maxVoices, int mixingProfile);
    private static native int nativeGetSampleRate(long handle);
    private static native boolean nativeStart(long handle);
    private static native void nativeSetMasterVolume(long handle, double volume);
    private static native boolean nativeEnqueueNote(long handle, double frequency,
            int durationMs, int attackMs, int releaseMs, double volume, int waveType,
            double pan, double panEnd, double freqEnd, double lfoFreq, double lfoDepth,
            double noiseRate, double noiseRateEnd, double lowPassHz,
            double pulseWidth, double pulseWidthEnd, int bitDepth, double[] waveTable,
            double[] seqFreqs, int seqStepCount, int seqStepDurMs);
    private static native boolean nativeEnqueueChord(long handle, double[] frequencies,
            int durationMs, int attackMs, int releaseMs, double volume, int waveType,
            double[] pans, double noiseRate, double lowPassHz, double pulseWidth,
            int bitDepth, double[] waveTable);
    private static native boolean nativeStopAll(long handle);
    private static native void nativeDestroy(long handle);
}
