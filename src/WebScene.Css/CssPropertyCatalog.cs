using System.Collections.Frozen;
using System.Globalization;

namespace WebScene.Css;

/// <summary>
/// Browser-shaped CSSOM property exposure for the bounded WebScene component profile.
/// The catalog is deliberately independent of any JavaScript framework: it tells a
/// CSSStyleDeclaration proxy which IDL attributes are CSS properties and which names
/// must remain ordinary JavaScript expandos.
/// </summary>
public static class CssPropertyCatalog
{
    private static readonly FrozenSet<string> s_supported = CssGeneratedPropertyMetadata.SupportedNames
        .ToFrozenSet(StringComparer.OrdinalIgnoreCase);

    public static bool IsSupported(string? propertyName)
    {
        if (string.IsNullOrWhiteSpace(propertyName) || propertyName.StartsWith("--", StringComparison.Ordinal))
        {
            return false;
        }

        return s_supported.Contains(Normalize(propertyName));
    }

    /// <summary>
    /// Returns whether a property stores a CSS color value rather than another
    /// value whose name merely contains the word "color".
    /// </summary>
    public static bool IsColorValueProperty(string? propertyName)
    {
        if (string.IsNullOrWhiteSpace(propertyName)
            || propertyName.StartsWith("--", StringComparison.Ordinal))
        {
            return false;
        }

        var normalized = Normalize(propertyName);
        return normalized is "color" or "fill" or "stroke"
               || normalized.EndsWith("-color", StringComparison.Ordinal);
    }

    public static bool IsValidCssomValue(string propertyName, string value)
    {
        if (propertyName.StartsWith("--", StringComparison.Ordinal))
        {
            return true;
        }

        // An empty string removes an authored declaration. Other whitespace-only
        // strings are invalid CSS tokens and must leave the previous declaration.
        if (value.Length == 0)
        {
            return true;
        }
        if (string.IsNullOrWhiteSpace(value))
        {
            return false;
        }

        var normalized = Normalize(propertyName);
        var trimmed = value.Trim();
        var normalizedValue = trimmed.ToLowerInvariant();
        if (normalizedValue is "inherit" or "initial" or "revert" or "revert-layer" or "unset"
            || trimmed.Contains("var(", StringComparison.OrdinalIgnoreCase)
            || trimmed.Contains("calc(", StringComparison.OrdinalIgnoreCase)
            || trimmed.Contains("min(", StringComparison.OrdinalIgnoreCase)
            || trimmed.Contains("max(", StringComparison.OrdinalIgnoreCase)
            || trimmed.Contains("clamp(", StringComparison.OrdinalIgnoreCase))
        {
            return true;
        }

        return normalized switch
        {
            "position" => normalizedValue is "static" or "relative" or "absolute" or "fixed" or "sticky"
                or "-webkit-sticky",
            "font-size" => IsFontSize(normalizedValue),
            "color-scheme" => normalizedValue is "normal" or "light" or "dark"
                or "light dark" or "dark light" or "only light" or "only dark",
            "accent-color" => normalizedValue == "auto" || !string.IsNullOrWhiteSpace(trimmed),
            "filter" or "backdrop-filter" => HasOnlyFunctions(normalizedValue,
                "blur", "brightness", "contrast", "drop-shadow", "grayscale", "hue-rotate",
                "invert", "opacity", "saturate", "sepia", "url"),
            "clip-path" => HasOnlyFunctions(normalizedValue,
                "circle", "ellipse", "inset", "path", "polygon", "rect", "url", "xywh"),
            "mask-image" => normalizedValue == "none" || HasOnlyFunctions(normalizedValue,
                "image", "image-set", "linear-gradient", "radial-gradient",
                "repeating-linear-gradient", "repeating-radial-gradient", "url"),
            "mask-repeat" => HasOnlyKeywords(normalizedValue,
                "no-repeat", "repeat", "repeat-x", "repeat-y", "round", "space"),
            "mask-composite" => HasOnlyKeywords(normalizedValue,
                "add", "exclude", "intersect", "subtract"),
            "mask-size" => normalizedValue.IndexOfAny(['0', '1', '2', '3', '4', '5', '6', '7', '8', '9']) >= 0
                || HasOnlyKeywords(normalizedValue, "auto", "contain", "cover"),
            "mask-position" => normalizedValue.IndexOfAny(['0', '1', '2', '3', '4', '5', '6', '7', '8', '9']) >= 0
                || HasOnlyKeywords(normalizedValue, "bottom", "center", "left", "right", "top"),
            "letter-spacing" => normalizedValue == "normal"
                || !IsInvalidUnitlessLength(trimmed),
            _ => true
        };
    }

    private static string Normalize(string propertyName)
    {
        var normalized = propertyName.Trim();
        if (normalized.Equals("cssFloat", StringComparison.Ordinal))
        {
            return "float";
        }

        if (normalized.Contains('-'))
        {
            return normalized.ToLowerInvariant();
        }

        var builder = new System.Text.StringBuilder(normalized.Length + 4);
        for (var index = 0; index < normalized.Length; index++)
        {
            var character = normalized[index];
            if (char.IsUpper(character) && index > 0)
            {
                builder.Append('-');
            }
            builder.Append(char.ToLowerInvariant(character));
        }
        return builder.ToString();
    }

    private static bool IsFontSize(string value)
    {
        if (value is "xx-small" or "x-small" or "small" or "medium" or "large" or "x-large" or "xx-large"
            or "xxx-large" or "larger" or "smaller")
        {
            return true;
        }

        return !IsInvalidUnitlessLength(value);
    }

    private static bool IsInvalidUnitlessLength(string value)
        => double.TryParse(value, NumberStyles.Float, CultureInfo.InvariantCulture, out var numeric)
           && double.IsFinite(numeric)
           && numeric != 0;

    private static bool HasOnlyKeywords(string value, params string[] allowed)
    {
        var tokens = value.Split([',', ' ', '\t', '\r', '\n'],
            StringSplitOptions.RemoveEmptyEntries);
        return tokens.Length > 0 && tokens.All(token => allowed.Contains(token, StringComparer.Ordinal));
    }

    private static bool HasOnlyFunctions(string value, params string[] allowed)
    {
        if (value == "none")
        {
            return true;
        }

        var cursor = 0;
        var found = false;
        while (cursor < value.Length)
        {
            while (cursor < value.Length && (char.IsWhiteSpace(value[cursor]) || value[cursor] == ','))
            {
                cursor++;
            }
            if (cursor == value.Length)
            {
                break;
            }

            var start = cursor;
            while (cursor < value.Length && (char.IsLetterOrDigit(value[cursor]) || value[cursor] == '-'))
            {
                cursor++;
            }
            if (start == cursor || !allowed.Contains(value[start..cursor], StringComparer.Ordinal))
            {
                return false;
            }
            while (cursor < value.Length && char.IsWhiteSpace(value[cursor]))
            {
                cursor++;
            }
            if (cursor == value.Length || value[cursor] != '(')
            {
                return false;
            }

            var depth = 1;
            for (cursor++; cursor < value.Length && depth > 0; cursor++)
            {
                depth += value[cursor] == '(' ? 1 : value[cursor] == ')' ? -1 : 0;
            }
            if (depth != 0)
            {
                return false;
            }
            found = true;
        }
        return found;
    }
}
