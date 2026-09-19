using System.Security.Cryptography;
using System.Text;
using SkiaSharp;
using WebScene.Backends.Avalonia.Native;
using Xunit;

namespace WebScene.Backend.Avalonia.Tests;

public sealed class RasterMaskRenderingTests
{
    private const string Png =
        "iVBORw0KGgoAAAANSUhEUgAAAAEAAAABCAYAAAAfFcSJAAAADUlEQVR42mOQ3RnyHwAESwIqgesNvAAAAABJRU5ErkJggg==";
    private const string PngIdentity =
        "df3985a7b0f470cc915f32583f5fc79318ff2e3ebb0510057725f14f0fcc02bd";
    private const string Webp =
        "UklGRiIAAABXRUJQVlA4IBYAAAAwAQCdASoBAAEADsD+JaQAA3AAAA==";
    private const string WebpIdentity =
        "f568bc89e7899aa082d6a7c4aa1bac15dfeca5153bf51fee2f6198cf35fe6085";

    [Theory]
    [InlineData(Png, PngIdentity)]
    [InlineData(Webp, WebpIdentity)]
    public void RasterEnvelopeDecodesPngAndWebp(string payload, string identity)
    {
        using var bitmap = new SKBitmap(6, 4, SKColorType.Bgra8888, SKAlphaType.Premul);
        using var canvas = new SKCanvas(bitmap);
        var renderer = new NativeCanvasSceneRenderer();

        Assert.True(renderer.DrawDomRasterMaskForTest(
            canvas,
            Envelope(1, 1, identity, payload),
            "repeat", "1px 0px", "2px 2px", "0 0 1 1",
            new SceneCommand { Width = 6, Height = 4 }));
        canvas.Flush();

        Assert.NotEqual(SKColors.Transparent, bitmap.GetPixel(1, 1));
        Assert.NotEqual(SKColors.Transparent, bitmap.GetPixel(3, 1));
        Assert.Equal(1, renderer.RasterMaskCacheCountForTest);
        renderer.Reset();
        Assert.Equal(0, renderer.RasterMaskCacheCountForTest);
    }

    [Fact]
    public void BackgroundAndMaskShareOneDecodedIdentity()
    {
        var markup = Envelope(1, 1, PngIdentity, Png);
        var renderer = new NativeCanvasSceneRenderer();
        using var bitmap = new SKBitmap(8, 4, SKColorType.Bgra8888, SKAlphaType.Premul);
        using var canvas = new SKCanvas(bitmap);
        var command = new SceneCommand { Width = 8, Height = 4 };

        renderer.DrawDomSvgBackgroundForTest(
            canvas,
            $"webscene-bg-svg-v1\t0 0 1 1\tno-repeat\t2px 0px\t2px 4px\t{markup}",
            command);
        Assert.True(renderer.DrawDomRasterMaskForTest(
            canvas, markup, "no-repeat", "4px 0px", "2px 4px", "0 0 1 1",
            command));

        Assert.Equal(1, renderer.RasterMaskCacheCountForTest);
    }

    [Fact]
    public void AddAndExcludeUseTheDecodedRasterAlpha()
    {
        var markup = Envelope(1, 1, PngIdentity, Png);
        var renderer = new NativeCanvasSceneRenderer();
        using var bitmap = new SKBitmap(2, 2, SKColorType.Bgra8888, SKAlphaType.Premul);
        using var canvas = new SKCanvas(bitmap);
        var command = new SceneCommand { Width = 2, Height = 2 };

        Assert.True(renderer.DrawDomRasterMaskForTest(
            canvas, markup, "no-repeat", "0% 0%", "2px 2px", "0 0 1 1",
            command, SKBlendMode.SrcOver));
        Assert.NotEqual(SKColors.Transparent, bitmap.GetPixel(1, 1));
        Assert.True(renderer.DrawDomRasterMaskForTest(
            canvas, markup, "no-repeat", "0% 0%", "2px 2px", "0 0 1 1",
            command, SKBlendMode.Xor));
        Assert.Equal(SKColors.Transparent, bitmap.GetPixel(1, 1));
    }

    [Fact]
    public void InvalidRasterEnvelopesFailClosedWithoutCaching()
    {
        var renderer = new NativeCanvasSceneRenderer();
        using var bitmap = new SKBitmap(2, 2);
        using var canvas = new SKCanvas(bitmap);
        var command = new SceneCommand { Width = 2, Height = 2 };
        var textPayload = Convert.ToBase64String(Encoding.ASCII.GetBytes("not an image"));
        var textIdentity = Convert.ToHexString(
            SHA256.HashData(Encoding.ASCII.GetBytes("not an image"))).ToLowerInvariant();
        var oversizePayload = Convert.ToBase64String(new byte[2_621_441]);
        Assert.True(oversizePayload.Length < 4 * 1024 * 1024);

        foreach (var markup in new[]
        {
            Envelope(1, 1, new string('0', 64), Png),
            Envelope(2, 1, PngIdentity, Png),
            Envelope(1, 1, PngIdentity, "%%%"),
            Envelope(1, 1, textIdentity, textPayload),
            Envelope(1, 1, Convert.ToHexString(
                SHA256.HashData(new byte[2_621_441])).ToLowerInvariant(), oversizePayload),
        })
        {
            Assert.False(renderer.DrawDomRasterMaskForTest(
                canvas, markup, "no-repeat", "0% 0%", "2px 2px", "0 0 1 1",
                command));
        }
        Assert.Equal(0, renderer.RasterMaskCacheCountForTest);
    }

    [Fact]
    public void FailedMaskClearUsesSourceAndRespectsCurrentClip()
    {
        var foreground = new SKColor(20, 80, 160, 255);
        using var bitmap = new SKBitmap(
            4, 2, SKColorType.Bgra8888, SKAlphaType.Premul);
        using var canvas = new SKCanvas(bitmap);
        canvas.Clear(foreground);
        var restore = canvas.Save();
        canvas.ClipRect(new SKRect(1, 0, 3, 2), antialias: false);

        NativeCanvasSceneRenderer.ClearFailedMaskLayer(canvas);

        canvas.RestoreToCount(restore);
        canvas.Flush();
        Assert.Equal(foreground, bitmap.GetPixel(0, 0));
        Assert.Equal(SKColors.Transparent, bitmap.GetPixel(1, 0));
        Assert.Equal(SKColors.Transparent, bitmap.GetPixel(2, 1));
        Assert.Equal(foreground, bitmap.GetPixel(3, 1));
    }

    [Fact]
    public void EmbeddedRasterDataInsideSvgRemainsAnSvgResource()
    {
        var markup = $"<svg xmlns=\"http://www.w3.org/2000/svg\" viewBox=\"0 0 1 1\">"
            + $"<image width=\"1\" height=\"1\" href=\"data:image/png;base64,{Png}\"/></svg>";
        using var bitmap = new SKBitmap(2, 2);
        using var canvas = new SKCanvas(bitmap);
        var renderer = new NativeCanvasSceneRenderer();

        renderer.DrawDomSvgBackgroundForTest(
            canvas,
            $"webscene-bg-svg-v1\t0 0 1 1\tno-repeat\t0% 0%\t2px 2px\t{markup}",
            new SceneCommand { Width = 2, Height = 2 });

        Assert.Equal(0, renderer.RasterMaskCacheCountForTest);
    }

    [Fact]
    public void RepeatedUseStaysWithinTheBoundedIdentityCache()
    {
        var renderer = new NativeCanvasSceneRenderer();
        using var bitmap = new SKBitmap(1, 1);
        using var canvas = new SKCanvas(bitmap);
        var markup = Envelope(1, 1, PngIdentity, Png);
        for (var index = 0; index < 4096; index++)
        {
            Assert.True(renderer.DrawDomRasterMaskForTest(
                canvas, markup, "no-repeat", "0% 0%", "1px 1px", "0 0 1 1",
                new SceneCommand { Width = 1, Height = 1 }));
        }
        Assert.InRange(renderer.RasterMaskCacheCountForTest, 1, 256);
        Assert.InRange(renderer.RasterMaskDecodedBytesForTest, 4, 64L * 1024 * 1024);
    }

    private static string Envelope(
        int width,
        int height,
        string identity,
        string payload)
        => $"webscene-raster-v2\t{width}\t{height}\t{identity}\t{payload}";
}
