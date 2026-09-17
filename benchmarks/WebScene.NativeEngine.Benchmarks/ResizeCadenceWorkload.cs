namespace WebScene.NativeEngine.Benchmarks;

internal static class ResizeCadenceWorkload
{
    internal const string Waveform = "triangle-v1";

    internal static double Dimension(double baseline, int span, int index)
    {
        // Use a wide period to avoid overflow for a valid positive Int32 span.
        var phase = index % (2L * span);
        return baseline + (phase < span ? phase : 2L * span - phase);
    }

    internal static void RejectPhysicalPresentationGate(bool requested)
    {
        if (requested)
            throw new InvalidOperationException(
                "--enforce-chrome-reference cannot qualify physical presentation: "
                + "this probe measures headless draw callbacks, not displayed frames. "
                + "Use --chrome-reference only for an informational comparison.");
    }
}
