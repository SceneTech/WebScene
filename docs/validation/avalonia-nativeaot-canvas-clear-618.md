# Avalonia NativeAOT canvas-clear compatibility

Issue: [#618](https://github.com/SceneTech/WebScene/issues/618)

## Failure

CI run `35428781215` failed both macOS ARM64 NativeAOT profiles while
compiling `NativeCanvasSceneRenderer.cs`:

```text
CS1501: No overload for method 'Clear' takes 2 arguments
```

The failing call was in the raster CSS-mask rejection path. A malformed or
unsupported raster mask must clear the current bounded mask layer with source
replacement. Leaving earlier mask pixels in that layer would turn a rejected
layer into a partially accepted mask.

## Compatibility fix

`SKCanvas.Clear(SKColor, SKBlendMode)` is not part of the SkiaSharp API used by
either supported profile:

| Avalonia profile | SkiaSharp | compatible operation |
| --- | --- | --- |
| 11.3.4 | 2.88.9 | `DrawColor(SKColor, SKBlendMode)` |
| 12.1.1 sample | 3.119.4 | `DrawColor(SKColor, SKBlendMode)` |

`ClearFailedMaskLayer` now calls
`DrawColor(SKColors.Transparent, SKBlendMode.Src)`. Source blending replaces
the pixels inside the current clip with transparent pixels, preserving the
original fail-closed mask semantics without a version conditional or
reflection.

## Contract coverage

`RasterMaskRenderingTests.FailedMaskClearUsesSourceAndRespectsCurrentClip`
starts with an opaque surface, applies a bounded clip, invokes the exact helper
used by the rejection path, and requires transparent pixels inside the clip
while pixels outside remain unchanged.

The release gates for this change are the existing CI matrix entries:

```sh
dotnet publish experiments/WebScene.GpuHost.Probe -c Release -r osx-arm64 \
  -p:PublishAot=true -p:JsonSerializerIsReflectionEnabledByDefault=false \
  -p:WebSceneAvalonia12Sample=false -o artifacts/aot-contracts-avalonia11

dotnet publish experiments/WebScene.GpuHost.Probe -c Release -r osx-arm64 \
  -p:PublishAot=true -p:JsonSerializerIsReflectionEnabledByDefault=false \
  -p:WebSceneAvalonia12Sample=true -o artifacts/aot-contracts-avalonia12
```

Per the active throughput directive, these builds and the focused test were
authored but not executed in this worktree. Only whitespace/error checks were
run locally; CI must provide the two-profile NativeAOT evidence before the
issue is closed.
