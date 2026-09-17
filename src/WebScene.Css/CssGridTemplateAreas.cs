namespace WebScene.Css;

public readonly record struct CssGridNamedArea(
    string Name,
    int Row,
    int Column,
    int RowSpan,
    int ColumnSpan);

/// <summary>
/// Parsed, rectangular named areas from a CSS grid-template-areas value.
/// </summary>
public sealed class CssGridTemplateAreas
{
    private readonly Dictionary<string, CssGridNamedArea> _areas;

    private CssGridTemplateAreas(
        int rowCount,
        int columnCount,
        string serialized,
        Dictionary<string, CssGridNamedArea> areas)
    {
        RowCount = rowCount;
        ColumnCount = columnCount;
        Serialized = serialized;
        _areas = areas;
    }

    public int RowCount { get; }

    public int ColumnCount { get; }

    public string Serialized { get; }

    public IReadOnlyCollection<CssGridNamedArea> Areas => _areas.Values;

    public bool TryGetArea(string? name, out CssGridNamedArea area)
        => _areas.TryGetValue(name?.Trim() ?? string.Empty, out area);

    public static bool TryParse(string? value, out CssGridTemplateAreas? template)
    {
        template = null;
        var source = (value ?? string.Empty).AsSpan().Trim();
        if (source.Equals("none", StringComparison.OrdinalIgnoreCase))
        {
            template = new CssGridTemplateAreas(0, 0, "none", new(StringComparer.Ordinal));
            return true;
        }
        if (source.IsEmpty) return false;

        var rows = new List<string[]>();
        var cursor = 0;
        while (cursor < source.Length)
        {
            while (cursor < source.Length && char.IsWhiteSpace(source[cursor])) cursor++;
            if (cursor == source.Length) break;
            var quote = source[cursor++];
            if (quote is not ('\'' or '"')) return false;
            var rowStart = cursor;
            while (cursor < source.Length && source[cursor] != quote)
            {
                if (source[cursor] is '\n' or '\r' or '\f' or '\\') return false;
                cursor++;
            }
            if (cursor == source.Length) return false;
            var cells = source[rowStart..cursor].ToString()
                .Split((char[]?)null, StringSplitOptions.RemoveEmptyEntries);
            cursor++;
            if (cells.Length == 0 || rows.Count > 0 && cells.Length != rows[0].Length) return false;
            for (var index = 0; index < cells.Length; index++)
            {
                var cell = cells[index];
                if (cell.All(static character => character == '.'))
                {
                    cells[index] = ".";
                    continue;
                }
                if (!IsCustomIdentifier(cell)) return false;
            }
            rows.Add(cells);
        }
        if (rows.Count == 0) return false;

        var bounds = new Dictionary<string, (int Top, int Left, int Bottom, int Right)>(StringComparer.Ordinal);
        for (var row = 0; row < rows.Count; row++)
        {
            for (var column = 0; column < rows[row].Length; column++)
            {
                var name = rows[row][column];
                if (name == ".") continue;
                if (!bounds.TryGetValue(name, out var area)) area = (row, column, row + 1, column + 1);
                else area = (Math.Min(area.Top, row), Math.Min(area.Left, column),
                    Math.Max(area.Bottom, row + 1), Math.Max(area.Right, column + 1));
                bounds[name] = area;
            }
        }

        var areas = new Dictionary<string, CssGridNamedArea>(StringComparer.Ordinal);
        foreach (var (name, area) in bounds)
        {
            for (var row = area.Top; row < area.Bottom; row++)
            for (var column = area.Left; column < area.Right; column++)
            {
                if (rows[row][column] != name) return false;
            }
            areas[name] = new CssGridNamedArea(
                name, area.Top, area.Left, area.Bottom - area.Top, area.Right - area.Left);
        }
        var serialized = string.Join(' ', rows.Select(static row => $"\"{string.Join(' ', row)}\""));
        template = new CssGridTemplateAreas(rows.Count, rows[0].Length, serialized, areas);
        return true;
    }

    private static bool IsCustomIdentifier(string value)
    {
        if (value is "auto" or "span" or "initial" or "inherit" or "unset" or "revert"
            or "revert-layer" or "default") return false;
        if (value.Length == 0 || !(char.IsLetter(value[0]) || value[0] is '_' or '-')) return false;
        return value.Skip(1).All(static character => char.IsLetterOrDigit(character)
            || character is '_' or '-');
    }
}
