using WebScene.Css;
using Xunit;

namespace WebScene.Css.Tests;

public sealed class CssPropertyCatalogTests
{
    [Theory]
    [InlineData("color")]
    [InlineData("backgroundColor")]
    [InlineData("border-left-color")]
    [InlineData("fill")]
    [InlineData("stroke")]
    public void IdentifiesCssColorValueProperties(string propertyName)
        => Assert.True(CssPropertyCatalog.IsColorValueProperty(propertyName));

    [Theory]
    [InlineData("--theme-color")]
    [InlineData("color-scheme")]
    [InlineData("print-color-adjust")]
    [InlineData("background")]
    public void DoesNotMisclassifyNonColorValueProperties(string propertyName)
        => Assert.False(CssPropertyCatalog.IsColorValueProperty(propertyName));

    [Theory]
    [InlineData("position")]
    [InlineData("backgroundAttachment")]
    [InlineData("grid-area")]
    [InlineData("fillOpacity")]
    [InlineData("order")]
    [InlineData("-moz-transform")]
    [InlineData("WebkitTransform")]
    [InlineData("grid-gap")]
    [InlineData("insetInlineStart")]
    [InlineData("insetBlockEnd")]
    [InlineData("borderBlockWidth")]
    [InlineData("border-inline-color")]
    [InlineData("paddingBlockStart")]
    [InlineData("margin-block-end")]
    [InlineData("padding-inline-end")]
    [InlineData("borderStartEndRadius")]
    [InlineData("transitionTimingFunction")]
    [InlineData("transform-origin")]
    [InlineData("color-scheme")]
    [InlineData("accentColor")]
    [InlineData("maskImage")]
    [InlineData("mask-size")]
    [InlineData("clipPath")]
    [InlineData("filter")]
    [InlineData("backdropFilter")]
    [InlineData("wordBreak")]
    [InlineData("overflowWrap")]
    [InlineData("wordWrap")]
    [InlineData("objectFit")]
    [InlineData("object-position")]
    [InlineData("userSelect")]
    [InlineData("webkitUserSelect")]
    [InlineData("overscrollBehavior")]
    [InlineData("overscrollBehaviorX")]
    [InlineData("overscrollBehaviorY")]
    public void ExposesSupportedCssomProperties(string name)
        => Assert.True(CssPropertyCatalog.IsSupported(name));

    [Fact]
    public void ExposesEveryGeneratedSupportedProperty()
    {
        Assert.Equal(
            CssGeneratedPropertyMetadata.SupportedNames.Length,
            CssGeneratedPropertyMetadata.SupportedNames.Distinct(StringComparer.OrdinalIgnoreCase).Count());

        foreach (var name in CssGeneratedPropertyMetadata.SupportedNames)
        {
            Assert.True(CssPropertyCatalog.IsSupported(name), name);
        }
    }

    [Theory]
    [InlineData("fakeProperty")]
    [InlineData("WebkitFakeProperty")]
    [InlineData("--custom-token")]
    public void DoesNotExposeUnknownOrCustomPropertiesAsIdlAttributes(string name)
        => Assert.False(CssPropertyCatalog.IsSupported(name));

    [Theory]
    [InlineData("position", "absolute", true)]
    [InlineData("position", "ABSOLUTE", true)]
    [InlineData("position", "fake value", false)]
    [InlineData("position", " ", false)]
    [InlineData("font-size", "27px", true)]
    [InlineData("font-size", "2", false)]
    [InlineData("font-size", "0", true)]
    [InlineData("letter-spacing", "normal", true)]
    [InlineData("letter-spacing", "3", false)]
    [InlineData("color-scheme", "light dark", true)]
    [InlineData("color-scheme", "sepia", false)]
    [InlineData("accent-color", "auto", true)]
    [InlineData("mask-image", "linear-gradient(black, transparent)", true)]
    [InlineData("mask", "url(icon.svg) no-repeat center / 16px 16px", true)]
    [InlineData("mask-mode", "match-source", true)]
    [InlineData("mask-mode", "luminance", true)]
    [InlineData("mask-mode", "none", false)]
    [InlineData("mask-repeat", "no-repeat", true)]
    [InlineData("mask-repeat", "bounce", false)]
    [InlineData("mask-composite", "exclude, add", true)]
    [InlineData("-webkit-mask-composite", "xor, xor", true)]
    [InlineData("clip-path", "inset(1px 2px)", true)]
    [InlineData("clip-path", "star(1px)", false)]
    [InlineData("filter", "brightness(0.5) blur(2px)", true)]
    [InlineData("filter", "unknown(1)", false)]
    [InlineData("backdrop-filter", "none", true)]
    [InlineData("backdrop-filter", "blur(8px) saturate(1.08)", true)]
    [InlineData("object-fit", "cover", true)]
    [InlineData("object-fit", "stretch", false)]
    [InlineData("object-position", "right 25%", true)]
    [InlineData("object-position", "left right", false)]
    [InlineData("object-position", "top bottom", false)]
    [InlineData("object-position", "left middle", false)]
    [InlineData("user-select", "none", true)]
    [InlineData("-webkit-user-select", "text", true)]
    [InlineData("user-select", "toggle", false)]
    [InlineData("overscroll-behavior", "contain none", true)]
    [InlineData("overscroll-behavior-x", "contain", true)]
    [InlineData("overscroll-behavior", "contain auto none", false)]
    [InlineData("overscroll-behavior-y", "bounce", false)]
    public void ValidatesCssomValuesWithoutFrameworkKnowledge(string name, string value, bool expected)
        => Assert.Equal(expected, CssPropertyCatalog.IsValidCssomValue(name, value));
}
