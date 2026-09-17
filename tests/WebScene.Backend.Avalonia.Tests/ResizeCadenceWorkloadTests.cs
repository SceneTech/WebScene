using WebScene.NativeEngine.Benchmarks;
using Xunit;

namespace WebScene.Backend.Avalonia.Tests;

public sealed class ResizeCadenceWorkloadTests
{
    [Fact]
    public void TriangleIncludesPeakAndReturnsWithoutSawtoothJump()
    {
        var actual = Enumerable.Range(0, 9)
            .Select(index => ResizeCadenceWorkload.Dimension(1180, 4, index)).ToArray();
        Assert.Equal(new double[] {1180, 1181, 1182, 1183, 1184, 1183, 1182, 1181, 1180}, actual);
    }

    [Fact]
    public void DifferentAxesRetainTheirIndependentPeriods()
    {
        Assert.Equal(1180, ResizeCadenceWorkload.Dimension(1180, 24, 48));
        Assert.Equal(732, ResizeCadenceWorkload.Dimension(720, 30, 48));
        Assert.Equal(1181, ResizeCadenceWorkload.Dimension(1180, 1, 1));
        Assert.Equal(1180, ResizeCadenceWorkload.Dimension(1180, 1, 2));
        Assert.Equal(1180d + int.MaxValue,
            ResizeCadenceWorkload.Dimension(1180, int.MaxValue, int.MaxValue));
    }

    [Fact]
    public void HeadlessCallbacksCannotQualifyPhysicalPresentation()
    {
        ResizeCadenceWorkload.RejectPhysicalPresentationGate(false);
        var failure = Assert.Throws<InvalidOperationException>(() =>
            ResizeCadenceWorkload.RejectPhysicalPresentationGate(true));
        Assert.Contains("not displayed frames", failure.Message);
    }
}
