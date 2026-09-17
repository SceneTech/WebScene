using System.Globalization;

namespace WebScene.Css;

internal readonly record struct CssGridTrack(
    double BaseSize,
    bool AcceptsIntrinsicContribution,
    bool MaximumIsAuto,
    double Fraction = 0);

internal static class CssGridTrackList
{
    public static bool TryParseRows(
        string value,
        double percentageBasis,
        out CssGridTrack[] tracks)
    {
        var tokens = Tokenize(value);
        tracks = new CssGridTrack[tokens.Count];
        if (tokens.Count == 0) return false;
        for (var index = 0; index < tokens.Count; index++)
        {
            if (!TryParseTrack(tokens[index], percentageBasis, out tracks[index]))
            {
                tracks = [];
                return false;
            }
        }
        return true;
    }

    public static void DistributeRemainingSpace(
        double[] sizes,
        IReadOnlyList<CssGridTrack> tracks,
        double availableHeight,
        double totalGap,
        bool stretches)
    {
        if (!stretches || !double.IsFinite(availableHeight)) return;
        var remaining = availableHeight - totalGap - sizes.Sum();
        if (remaining <= 0) return;
        var fractionalWeight = tracks.Sum(static track => track.Fraction);
        if (fractionalWeight > 0)
        {
            for (var index = 0; index < Math.Min(sizes.Length, tracks.Count); index++)
            {
                if (tracks[index].Fraction > 0)
                    sizes[index] += remaining * tracks[index].Fraction / fractionalWeight;
            }
            return;
        }
        var stretchable = Enumerable.Range(0, sizes.Length)
            .Where(index => index >= tracks.Count || tracks[index].MaximumIsAuto)
            .ToArray();
        if (stretchable.Length == 0) return;
        var share = remaining / stretchable.Length;
        foreach (var index in stretchable) sizes[index] += share;
    }

    private static bool TryParseTrack(
        string token,
        double percentageBasis,
        out CssGridTrack track)
    {
        token = token.Trim();
        if (token.Equals("auto", StringComparison.OrdinalIgnoreCase))
        {
            track = new(0, true, true);
            return true;
        }
        if (token.Equals("min-content", StringComparison.OrdinalIgnoreCase)
            || token.Equals("max-content", StringComparison.OrdinalIgnoreCase))
        {
            track = new(0, true, false);
            return true;
        }
        if (TryParseFraction(token, out var fraction))
        {
            track = new(0, false, false, fraction);
            return true;
        }
        if (token.StartsWith("minmax(", StringComparison.OrdinalIgnoreCase)
            && token.EndsWith(')'))
        {
            var arguments = TokenizeArguments(token[7..^1]);
            if (arguments is null
                || !TryParseMinimum(arguments.Value.Minimum, percentageBasis, out var minimum))
            {
                track = default;
                return false;
            }
            var maximumIsAuto = arguments.Value.Maximum.Equals(
                "auto", StringComparison.OrdinalIgnoreCase);
            if (!maximumIsAuto
                && !arguments.Value.Maximum.Equals("min-content", StringComparison.OrdinalIgnoreCase)
                && !arguments.Value.Maximum.Equals("max-content", StringComparison.OrdinalIgnoreCase)
                && !TryParseLength(arguments.Value.Maximum, percentageBasis, out _))
            {
                track = default;
                return false;
            }
            track = new(minimum, true, maximumIsAuto);
            return true;
        }
        if (TryParseLength(token, percentageBasis, out var size))
        {
            track = new(size, false, false);
            return true;
        }
        track = default;
        return false;
    }

    private static bool TryParseMinimum(string value, double basis, out double minimum)
    {
        if (value.Equals("auto", StringComparison.OrdinalIgnoreCase)
            || value.Equals("min-content", StringComparison.OrdinalIgnoreCase)
            || value.Equals("max-content", StringComparison.OrdinalIgnoreCase))
        {
            minimum = 0;
            return true;
        }
        return TryParseLength(value, basis, out minimum);
    }

    private static bool TryParseLength(string value, double basis, out double result)
    {
        value = value.Trim();
        if (value == "0")
        {
            result = 0;
            return true;
        }
        var percentage = value.EndsWith('%');
        var pixels = value.EndsWith("px", StringComparison.OrdinalIgnoreCase);
        var suffix = percentage ? 1 : pixels ? 2 : 0;
        if (suffix == 0
            || !double.TryParse(value.AsSpan(0, value.Length - suffix),
                NumberStyles.Float, CultureInfo.InvariantCulture, out var number)
            || !double.IsFinite(number) || number < 0)
        {
            result = 0;
            return false;
        }
        result = percentage ? basis * number / 100 : number;
        return true;
    }

    private static bool TryParseFraction(string value, out double result)
    {
        value = value.Trim();
        if (!value.EndsWith("fr", StringComparison.OrdinalIgnoreCase)
            || !double.TryParse(value.AsSpan(0, value.Length - 2),
                NumberStyles.Float, CultureInfo.InvariantCulture, out result)
            || !double.IsFinite(result) || result <= 0)
        {
            result = 0;
            return false;
        }
        return true;
    }

    private static List<string> Tokenize(string value)
    {
        var result = new List<string>();
        var start = -1;
        var depth = 0;
        for (var index = 0; index <= value.Length; index++)
        {
            var character = index < value.Length ? value[index] : ' ';
            if (character == '(') depth++;
            else if (character == ')' && depth > 0) depth--;
            var separator = depth == 0 && char.IsWhiteSpace(character);
            if (!separator && start < 0) start = index;
            if (separator && start >= 0)
            {
                result.Add(value[start..index]);
                start = -1;
            }
        }
        return result;
    }

    private static (string Minimum, string Maximum)? TokenizeArguments(string value)
    {
        var depth = 0;
        for (var index = 0; index < value.Length; index++)
        {
            if (value[index] == '(') depth++;
            else if (value[index] == ')') depth--;
            else if (value[index] == ',' && depth == 0)
            {
                return (value[..index].Trim(), value[(index + 1)..].Trim());
            }
        }
        return null;
    }
}
