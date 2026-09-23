using System.Text.Json;
using Avalonia.Controls;
using SkiaSharp;
using WebScene.Backends.Avalonia.Native;

namespace NativeTradingViewTerminal;

internal static partial class HeadlessProof
{
    private static int CaptureSubmenuEvidence(
        NativeWebSceneView view, Window window, string output, int width, int height)
    {
        var surface = (NativeSceneSurface)view.Content!;
        var axis = ReadGeometry(view, """
            (() => {
              const frame = [...document.querySelectorAll('iframe')].find(
                f => f.contentDocument?.querySelectorAll('canvas').length >= 8);
              const target = frame?.contentDocument?.querySelector('.price-axis');
              if (!target) return null;
              const host = frame.getBoundingClientRect();
              const rect = target.getBoundingClientRect();
              return {x:host.x + rect.x + rect.width / 2,
                y:host.y + rect.y + rect.height / 2};
            })()
            """, "price axis");
        var axisX = axis.GetProperty("x").GetDouble();
        var axisY = axis.GetProperty("y").GetDouble();
        surface.SubmitAvaloniaPointerMove(axisX, axisY);
        PumpFrames(view, window, TimeSpan.FromMilliseconds(100));
        surface.SubmitPointerButton(2, axisX, axisY, 2, pressed: true);
        PumpFrames(view, window, TimeSpan.FromMilliseconds(50));
        surface.SubmitPointerButton(3, axisX, axisY, 2, pressed: false);
        PumpFrames(view, window, TimeSpan.FromMilliseconds(500));

        var row = ReadGeometry(view, """
            (() => {
              const frame = [...document.querySelectorAll('iframe')].find(
                f => f.contentDocument?.querySelectorAll('canvas').length >= 8);
              const target = [...frame.contentDocument.querySelectorAll('span')]
                .find(e => e.textContent?.trim() === 'Labels'
                  && e.getBoundingClientRect().width > 0);
              if (!target) return null;
              const host = frame.getBoundingClientRect();
              const rect = target.getBoundingClientRect();
              return {x:host.x + rect.x + rect.width / 2,
                y:host.y + rect.y + rect.height / 2};
            })()
            """, "Labels row");
        var before = surface.CaptureRetainedScenePng();
        File.WriteAllBytes(Path.Combine(output, "submenu-before.png"), before);
        surface.SubmitAvaloniaPointerMove(
            row.GetProperty("x").GetDouble(), row.GetProperty("y").GetDouble());
        PumpFrames(view, window, TimeSpan.FromMilliseconds(750));

        var evidenceTask = view.EvaluateTextAsync("""
            (() => {
              const frame = [...document.querySelectorAll('iframe')].find(
                f => f.contentDocument?.querySelectorAll('canvas').length >= 8);
              const doc = frame.contentDocument;
              const leaf = [...doc.querySelectorAll('span')]
                .find(e => e.textContent?.trim() === 'Symbol name label');
              const child = leaf?.closest('[class*="context-menu"]');
              const parentLeaf = [...doc.querySelectorAll('span')]
                .find(e => e.textContent?.trim() === 'Labels');
              const parent = parentLeaf?.closest('[class*="context-menu"]');
              if (!child || !parent || child === parent) return null;
              const host = frame.getBoundingClientRect();
              const a = child.getBoundingClientRect();
              const b = parent.getBoundingClientRect();
              return {child:{x:host.x+a.x,y:host.y+a.y,width:a.width,height:a.height},
                parent:{x:host.x+b.x,y:host.y+b.y,width:b.width,height:b.height},
                hitInsideChild:child.contains(doc.elementFromPoint(a.x+a.width/2,a.y+20))};
            })()
            """);
        PumpUntil(evidenceTask, TimeSpan.FromSeconds(10));
        File.WriteAllText(Path.Combine(output, "submenu-evidence.json"), evidenceTask.Result);
        using var evidence = JsonDocument.Parse(evidenceTask.Result);
        if (evidence.RootElement.ValueKind != JsonValueKind.Object
            || !evidence.RootElement.GetProperty("hitInsideChild").GetBoolean())
        {
            throw new InvalidOperationException(
                "TradingView did not open a hit-testable Labels submenu: "
                + evidenceTask.Result);
        }

        var after = surface.CaptureRetainedScenePng();
        File.WriteAllBytes(Path.Combine(output, "submenu-after.png"), after);
        using var beforeBitmap = SKBitmap.Decode(before);
        using var afterBitmap = SKBitmap.Decode(after);
        var child = evidence.RootElement.GetProperty("child");
        var parent = evidence.RootElement.GetProperty("parent");
        static SKColor Sample(SKBitmap bitmap, JsonElement rect)
            => bitmap.GetPixel(
                (int)Math.Round(rect.GetProperty("x").GetDouble()) + 20,
                (int)Math.Round(rect.GetProperty("y").GetDouble()) + 3);
        var beforeColor = Sample(beforeBitmap, child);
        var childColor = Sample(afterBitmap, child);
        var parentColor = Sample(afterBitmap, parent);
        static int Distance(SKColor a, SKColor b)
            => Math.Abs(a.Red - b.Red) + Math.Abs(a.Green - b.Green)
                + Math.Abs(a.Blue - b.Blue);
        if (Distance(childColor, parentColor) > 6
            || Distance(beforeColor, childColor) < 12)
        {
            throw new InvalidOperationException(
                "TradingView created a hit-testable child menu but did not paint it "
                + $"(before={beforeColor}, child={childColor}, parent={parentColor}).");
        }

        var lines = ReadGeometry(view, """
            (() => {
              const frame = [...document.querySelectorAll('iframe')].find(
                f => f.contentDocument?.querySelectorAll('canvas').length >= 8);
              const target = [...frame.contentDocument.querySelectorAll('span')]
                .find(e => e.textContent?.trim() === 'Lines'
                  && e.getBoundingClientRect().width > 0);
              if (!target) return null;
              const host = frame.getBoundingClientRect();
              const rect = target.getBoundingClientRect();
              return {x:host.x + rect.x + rect.width / 2,
                y:host.y + rect.y + rect.height / 2};
            })()
            """, "Lines row");
        surface.SubmitAvaloniaPointerMove(
            lines.GetProperty("x").GetDouble(),
            lines.GetProperty("y").GetDouble());
        PumpFrames(view, window, TimeSpan.FromMilliseconds(750));
        var linesTask = view.EvaluateTextAsync("""
            (() => {
              const frame = [...document.querySelectorAll('iframe')].find(
                f => f.contentDocument?.querySelectorAll('canvas').length >= 8);
              const doc = frame.contentDocument;
              const label = [...doc.querySelectorAll('span')]
                .find(e => e.textContent?.trim() === 'Lines');
              const parent = label?.closest('[class*="context-menu"]');
              const child = [...(parent?.querySelectorAll('[class*="context-menu"]') ?? [])]
                .find(e => e.getBoundingClientRect().width > 0
                  && e.getBoundingClientRect().height > 0);
              if (!child || !parent) return null;
              const host = frame.getBoundingClientRect();
              const a = child.getBoundingClientRect();
              const b = parent.getBoundingClientRect();
              return {child:{x:host.x+a.x,y:host.y+a.y,width:a.width,height:a.height},
                parent:{x:host.x+b.x,y:host.y+b.y,width:b.width,height:b.height},
                text:child.textContent?.slice(0,250),
                hitInsideChild:child.contains(doc.elementFromPoint(a.x+a.width/2,a.y+20))};
            })()
            """);
        PumpUntil(linesTask, TimeSpan.FromSeconds(10));
        File.WriteAllText(Path.Combine(output, "submenu-lines-evidence.json"), linesTask.Result);
        using var linesEvidence = JsonDocument.Parse(linesTask.Result);
        if (linesEvidence.RootElement.ValueKind != JsonValueKind.Object
            || !linesEvidence.RootElement.GetProperty("hitInsideChild").GetBoolean()
            || linesEvidence.RootElement.GetProperty("text").GetString()!
                .Contains("Symbol name label", StringComparison.Ordinal))
        {
            throw new InvalidOperationException(
                "TradingView Lines did not open its own child submenu: "
                + linesTask.Result);
        }
        var linesPng = surface.CaptureRetainedScenePng();
        File.WriteAllBytes(Path.Combine(output, "submenu-lines.png"), linesPng);
        using var linesBitmap = SKBitmap.Decode(linesPng);
        if (Distance(
                Sample(linesBitmap, linesEvidence.RootElement.GetProperty("child")),
                Sample(linesBitmap, linesEvidence.RootElement.GetProperty("parent"))) > 6)
        {
            throw new InvalidOperationException(
                "TradingView Lines child menu exists but is not painted: "
                + linesTask.Result);
        }

        Console.WriteLine("TradingView Labels and Lines submenus opened and painted.");
        return 0;
    }

    private static JsonElement ReadGeometry(
        NativeWebSceneView view, string source, string target)
    {
        var evaluation = view.EvaluateTextAsync(source);
        PumpUntil(evaluation, TimeSpan.FromSeconds(10));
        using var document = JsonDocument.Parse(evaluation.Result);
        if (document.RootElement.ValueKind != JsonValueKind.Object)
            throw new InvalidOperationException($"TradingView {target} was unavailable.");
        return document.RootElement.Clone();
    }
}
