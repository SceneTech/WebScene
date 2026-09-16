using WebScene.Css;
using Xunit;

namespace WebScene.Css.Tests;

public sealed class CssGridTemplateAreasTests
{
    [Fact]
    public void ParsesSparseRectangularNamedAreas()
    {
        Assert.True(CssGridTemplateAreas.TryParse(
            "'. header header header .' '. left . right .' '. footer footer footer .'",
            out var template));

        Assert.NotNull(template);
        Assert.Equal(3, template.RowCount);
        Assert.Equal(5, template.ColumnCount);
        Assert.Equal("\". header header header .\" \". left . right .\" \". footer footer footer .\"",
            template.Serialized);
        Assert.True(template.TryGetArea("header", out var header));
        Assert.Equal(new CssGridNamedArea("header", 0, 1, 1, 3), header);
        Assert.True(template.TryGetArea("right", out var right));
        Assert.Equal(new CssGridNamedArea("right", 1, 3, 1, 1), right);
    }

    [Theory]
    [InlineData("\"a a\" \"a .\"")]
    [InlineData("\"a a\" \"a\"")]
    [InlineData("\"a .\" \". a\"")]
    [InlineData("\"auto\"")]
    public void RejectsNonRectangularOrMalformedTemplates(string value)
    {
        Assert.False(CssGridTemplateAreas.TryParse(value, out _));
    }

    [Fact]
    public void ParsesNoneAsAnEmptyTemplate()
    {
        Assert.True(CssGridTemplateAreas.TryParse("none", out var template));
        Assert.NotNull(template);
        Assert.Empty(template.Areas);
        Assert.Equal("none", template.Serialized);
    }
}
