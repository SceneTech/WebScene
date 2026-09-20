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
    private static readonly string[] CssPositionUnits =
    [
        "cqmin", "cqmax", "rem", "cqw", "cqh", "cqi", "cqb",
        "px", "em", "vw", "vh", "in", "cm", "mm", "pt", "pc", "q", "%"
    ];

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
            "object-fit" => normalizedValue is "fill" or "contain" or "cover" or "none" or "scale-down",
            "object-position" => IsObjectPosition(normalizedValue),
            "user-select" or "-webkit-user-select" or "webkit-user-select"
                or "-ms-user-select" or "ms-user-select"
                => normalizedValue is "auto" or "text" or "none" or "all",
            "overscroll-behavior-x" or "overscroll-behavior-y"
                => normalizedValue is "auto" or "contain" or "none",
            "overscroll-behavior" => IsOverscrollBehavior(normalizedValue),
            "isolation" => normalizedValue is "auto" or "isolate",
            "will-change" => IsWillChange(normalizedValue),
            "text-wrap" => normalizedValue is "wrap" or "nowrap",
            "touch-action" => normalizedValue is "auto" or "none"
                or "manipulation" or "pan-x" or "pan-y"
                or "pan-x pan-y" or "pan-y pan-x",
            "caret-color" => normalizedValue is "auto" or "currentcolor"
                || CssColorParser.TryParseColor(trimmed, out _),
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
                "add", "exclude", "intersect", "subtract", "xor"),
            "mask-mode" => HasOnlyKeywords(normalizedValue,
                "alpha", "luminance", "match-source"),
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

    private static bool IsOverscrollBehavior(string value)
    {
        var tokens = value.Split([' ', '\t', '\r', '\n'],
            StringSplitOptions.RemoveEmptyEntries);
        return tokens.Length is 1 or 2
            && tokens.All(static token => token is "auto" or "contain" or "none");
    }

    private static bool IsWillChange(string value)
    {
        if (value == "auto") return true;
        if (value.Length is 0 or > 256) return false;
        var tokens = value.Split(',', StringSplitOptions.TrimEntries);
        if (tokens.Length is 0 or > 8) return false;
        foreach (var token in tokens)
        {
            if (token.Length is 0 or > 64
                || token is "auto" or "none" or "default" or "initial" or "inherit"
                    or "unset" or "revert" or "revert-layer"
                || !(char.IsLetter(token[0]) || token[0] is '-' or '_')
                || token.Any(static character =>
                    !(char.IsLetterOrDigit(character) || character is '-' or '_')))
            {
                return false;
            }
        }
        return true;
    }

    private static bool IsObjectPosition(string value)
    {
        var tokens = value.Split([' ', '\t', '\r', '\n'],
            StringSplitOptions.RemoveEmptyEntries);
        if (tokens.Length is not (1 or 2) || !tokens.All(static token =>
            token is "left" or "right" or "top" or "bottom" or "center"
            || token.StartsWith("calc(", StringComparison.Ordinal)
            || token.StartsWith("min(", StringComparison.Ordinal)
            || token.StartsWith("max(", StringComparison.Ordinal)
            || IsCssPositionLength(token)))
        {
            return false;
        }
        return tokens.Length == 1
            || !((IsHorizontalPositionKeyword(tokens[0]) && IsHorizontalPositionKeyword(tokens[1]))
                || (IsVerticalPositionKeyword(tokens[0]) && IsVerticalPositionKeyword(tokens[1])));
    }

    private static bool IsHorizontalPositionKeyword(string token) => token is "left" or "right";

    private static bool IsVerticalPositionKeyword(string token) => token is "top" or "bottom";

    private static bool IsCssPositionLength(string token)
    {
        if (double.TryParse(token, NumberStyles.Float, CultureInfo.InvariantCulture, out var unitless))
        {
            return double.IsFinite(unitless) && unitless == 0;
        }
        foreach (var unit in CssPositionUnits)
        {
            if (!token.EndsWith(unit, StringComparison.Ordinal)) continue;
            return double.TryParse(token[..^unit.Length], NumberStyles.Float,
                CultureInfo.InvariantCulture, out var number) && double.IsFinite(number);
        }
        return false;
    }

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
