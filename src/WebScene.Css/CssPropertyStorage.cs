using System;
using System.Collections;
using System.Collections.Frozen;
using System.Collections.Generic;
using System.Linq;

namespace WebScene.Css;

/// <summary>
/// Stable IDs for ordinary properties used by the retained CSS/layout/presentation
/// subset. Unknown properties remain supported by the fallback collections.
/// </summary>
internal static class CssKnownProperties
{
    internal static readonly string[] Names = CssGeneratedPropertyMetadata.KnownNames;

    private static readonly FrozenDictionary<string, int> s_ids = Names
        .Select(static (name, index) => new KeyValuePair<string, int>(name, index))
        .ToFrozenDictionary(StringComparer.OrdinalIgnoreCase);

    internal static int Count => Names.Length;

    internal static bool TryGetId(string name, out int id)
    {
        if (CssGeneratedPropertyMetadata.TryGetKnownId(name, out id))
        {
            return true;
        }

        return s_ids.TryGetValue(name, out id);
    }
}

/// <summary>
/// Mutable while cascading, then frozen before becoming element-owned or shared.
/// Known properties use direct indexed storage; uncommon properties use a fallback map.
/// </summary>
internal sealed class CssPropertyValueStore : IReadOnlyDictionary<string, string>
{
    private readonly string?[] _knownValues;
    private Dictionary<string, string>? _otherValues;
    private int _count;
    private bool _frozen;

    internal CssPropertyValueStore(int fallbackCapacity = 0)
    {
        _knownValues = new string?[CssKnownProperties.Count];
        if (fallbackCapacity > 0)
        {
            _otherValues = new Dictionary<string, string>(fallbackCapacity, StringComparer.OrdinalIgnoreCase);
        }
    }

    internal bool IsFrozen => _frozen;

    public int Count => _count;

    public IEnumerable<string> Keys
    {
        get
        {
            for (var index = 0; index < _knownValues.Length; index++)
            {
                if (_knownValues[index] is not null)
                {
                    yield return CssKnownProperties.Names[index];
                }
            }

            if (_otherValues is not null)
            {
                foreach (var key in _otherValues.Keys)
                {
                    yield return key;
                }
            }
        }
    }

    public IEnumerable<string> Values
    {
        get
        {
            foreach (var pair in this)
            {
                yield return pair.Value;
            }
        }
    }

    public string this[string key]
    {
        get => TryGetValue(key, out var value) ? value : throw new KeyNotFoundException();
        set
        {
            ThrowIfFrozen();
            if (CssKnownProperties.TryGetId(key, out var id))
            {
                if (_knownValues[id] is null)
                {
                    _count++;
                }
                _knownValues[id] = value;
                return;
            }

            _otherValues ??= new Dictionary<string, string>(StringComparer.OrdinalIgnoreCase);
            if (_otherValues.TryAdd(key, value))
            {
                _count++;
            }
            else
            {
                _otherValues[key] = value;
            }
        }
    }

    public bool ContainsKey(string key)
        => CssKnownProperties.TryGetId(key, out var id)
            ? _knownValues[id] is not null
            : _otherValues?.ContainsKey(key) == true;

    public bool TryGetValue(string key, out string value)
    {
        if (CssKnownProperties.TryGetId(key, out var id))
        {
            value = _knownValues[id]!;
            return value is not null;
        }

        if (_otherValues is not null && _otherValues.TryGetValue(key, out value!))
        {
            return true;
        }

        value = null!;
        return false;
    }

    internal bool Remove(string key)
    {
        ThrowIfFrozen();
        if (CssKnownProperties.TryGetId(key, out var id))
        {
            if (_knownValues[id] is null)
            {
                return false;
            }

            _knownValues[id] = null;
            _count--;
            return true;
        }

        if (_otherValues?.Remove(key) != true)
        {
            return false;
        }

        _count--;
        return true;
    }

    internal void Clear()
    {
        ThrowIfFrozen();
        Array.Clear(_knownValues);
        _otherValues?.Clear();
        _count = 0;
    }

    internal void Freeze() => _frozen = true;

    internal CssPropertyValueStore Clone(bool frozen = false)
    {
        var clone = new CssPropertyValueStore(_otherValues?.Count ?? 0);
        Array.Copy(_knownValues, clone._knownValues, _knownValues.Length);
        if (_otherValues is not null)
        {
            foreach (var pair in _otherValues)
            {
                clone._otherValues![pair.Key] = pair.Value;
            }
        }
        clone._count = _count;
        clone._frozen = frozen;
        return clone;
    }

    internal bool ContentEquals(CssPropertyValueStore other)
    {
        if (ReferenceEquals(this, other))
        {
            return true;
        }
        if (_count != other._count)
        {
            return false;
        }
        for (var index = 0; index < _knownValues.Length; index++)
        {
            if (!string.Equals(_knownValues[index], other._knownValues[index], StringComparison.Ordinal))
            {
                return false;
            }
        }
        if (_otherValues is null || _otherValues.Count == 0)
        {
            return other._otherValues is null || other._otherValues.Count == 0;
        }
        if (other._otherValues is null || _otherValues.Count != other._otherValues.Count)
        {
            return false;
        }
        foreach (var pair in _otherValues)
        {
            if (!other._otherValues.TryGetValue(pair.Key, out var value)
                || !string.Equals(pair.Value, value, StringComparison.Ordinal))
            {
                return false;
            }
        }
        return true;
    }

    internal int GetContentHashCode()
    {
        var hash = new HashCode();
        for (var index = 0; index < _knownValues.Length; index++)
        {
            if (_knownValues[index] is { } value)
            {
                hash.Add(index);
                hash.Add(value, StringComparer.Ordinal);
            }
        }
        if (_otherValues is not null)
        {
            var otherHash = 0;
            foreach (var pair in _otherValues)
            {
                otherHash ^= HashCode.Combine(
                    StringComparer.OrdinalIgnoreCase.GetHashCode(pair.Key),
                    StringComparer.Ordinal.GetHashCode(pair.Value));
            }
            hash.Add(otherHash);
        }
        return hash.ToHashCode();
    }

    public Enumerator GetEnumerator() => new(this);

    IEnumerator<KeyValuePair<string, string>> IEnumerable<KeyValuePair<string, string>>.GetEnumerator()
        => GetEnumerator();

    IEnumerator IEnumerable.GetEnumerator() => GetEnumerator();

    private void ThrowIfFrozen()
    {
        if (_frozen)
        {
            throw new InvalidOperationException("A committed CSS property store is immutable.");
        }
    }

    internal struct Enumerator : IEnumerator<KeyValuePair<string, string>>
    {
        private readonly CssPropertyValueStore _store;
        private int _knownIndex;
        private Dictionary<string, string>.Enumerator _otherEnumerator;
        private bool _enumeratingOther;

        internal Enumerator(CssPropertyValueStore store)
        {
            _store = store;
            _knownIndex = -1;
            _otherEnumerator = default;
            _enumeratingOther = false;
            Current = default;
        }

        public KeyValuePair<string, string> Current { get; private set; }

        object IEnumerator.Current => Current;

        public bool MoveNext()
        {
            if (!_enumeratingOther)
            {
                while (++_knownIndex < _store._knownValues.Length)
                {
                    if (_store._knownValues[_knownIndex] is { } value)
                    {
                        Current = new KeyValuePair<string, string>(CssKnownProperties.Names[_knownIndex], value);
                        return true;
                    }
                }

                _enumeratingOther = true;
                if (_store._otherValues is not null)
                {
                    _otherEnumerator = _store._otherValues.GetEnumerator();
                }
            }

            if (_store._otherValues is not null && _otherEnumerator.MoveNext())
            {
                Current = _otherEnumerator.Current;
                return true;
            }
            return false;
        }

        public void Reset() => throw new NotSupportedException();

        public void Dispose() => _otherEnumerator.Dispose();
    }
}

/// <summary>
/// Bitset-backed declared-property collection with a fallback set for unknown names.
/// </summary>
internal sealed class CssPropertyNameSet : IReadOnlySet<string>
{
    private readonly ulong[] _knownBits = new ulong[(CssKnownProperties.Count + 63) / 64];
    private HashSet<string>? _otherNames;
    private int _count;
    private bool _frozen;

    internal CssPropertyNameSet(int fallbackCapacity = 0)
    {
        if (fallbackCapacity > 0)
        {
            _otherNames = new HashSet<string>(fallbackCapacity, StringComparer.OrdinalIgnoreCase);
        }
    }

    internal bool IsFrozen => _frozen;

    public int Count => _count;

    internal bool Add(string name)
    {
        ThrowIfFrozen();
        if (CssKnownProperties.TryGetId(name, out var id))
        {
            var slot = id >> 6;
            var bit = 1UL << (id & 63);
            if ((_knownBits[slot] & bit) != 0)
            {
                return false;
            }
            _knownBits[slot] |= bit;
            _count++;
            return true;
        }

        _otherNames ??= new HashSet<string>(StringComparer.OrdinalIgnoreCase);
        if (!_otherNames.Add(name))
        {
            return false;
        }
        _count++;
        return true;
    }

    public bool Contains(string name)
    {
        if (CssKnownProperties.TryGetId(name, out var id))
        {
            return (_knownBits[id >> 6] & (1UL << (id & 63))) != 0;
        }
        return _otherNames?.Contains(name) == true;
    }

    internal void Clear()
    {
        ThrowIfFrozen();
        Array.Clear(_knownBits);
        _otherNames?.Clear();
        _count = 0;
    }

    internal void UnionWith(IEnumerable<string> names)
    {
        foreach (var name in names)
        {
            Add(name);
        }
    }

    internal void Freeze() => _frozen = true;

    internal CssPropertyNameSet Clone(bool frozen = false)
    {
        var clone = new CssPropertyNameSet(_otherNames?.Count ?? 0);
        Array.Copy(_knownBits, clone._knownBits, _knownBits.Length);
        clone._otherNames?.UnionWith(_otherNames ?? []);
        clone._count = _count;
        clone._frozen = frozen;
        return clone;
    }

    internal bool SetEquals(CssPropertyNameSet other)
    {
        if (ReferenceEquals(this, other))
        {
            return true;
        }
        if (_count != other._count || !_knownBits.AsSpan().SequenceEqual(other._knownBits))
        {
            return false;
        }
        if (_otherNames is null || _otherNames.Count == 0)
        {
            return other._otherNames is null || other._otherNames.Count == 0;
        }
        return other._otherNames is not null && _otherNames.SetEquals(other._otherNames);
    }

    internal int GetContentHashCode()
    {
        var hash = new HashCode();
        foreach (var bits in _knownBits)
        {
            hash.Add(bits);
        }
        if (_otherNames is not null)
        {
            var otherHash = 0;
            foreach (var name in _otherNames)
            {
                otherHash ^= StringComparer.OrdinalIgnoreCase.GetHashCode(name);
            }
            hash.Add(otherHash);
        }
        return hash.ToHashCode();
    }

    public bool SetEquals(IEnumerable<string> other)
        => other is CssPropertyNameSet set
            ? SetEquals(set)
            : new HashSet<string>(other, StringComparer.OrdinalIgnoreCase).SetEquals(this);

    public bool IsSubsetOf(IEnumerable<string> other) => AsHashSet().IsSubsetOf(other);
    public bool IsSupersetOf(IEnumerable<string> other) => AsHashSet().IsSupersetOf(other);
    public bool IsProperSubsetOf(IEnumerable<string> other) => AsHashSet().IsProperSubsetOf(other);
    public bool IsProperSupersetOf(IEnumerable<string> other) => AsHashSet().IsProperSupersetOf(other);
    public bool Overlaps(IEnumerable<string> other) => other.Any(Contains);

    public IEnumerator<string> GetEnumerator()
    {
        for (var id = 0; id < CssKnownProperties.Count; id++)
        {
            if ((_knownBits[id >> 6] & (1UL << (id & 63))) != 0)
            {
                yield return CssKnownProperties.Names[id];
            }
        }
        if (_otherNames is not null)
        {
            foreach (var name in _otherNames)
            {
                yield return name;
            }
        }
    }

    IEnumerator IEnumerable.GetEnumerator() => GetEnumerator();

    private HashSet<string> AsHashSet() => new(this, StringComparer.OrdinalIgnoreCase);

    private void ThrowIfFrozen()
    {
        if (_frozen)
        {
            throw new InvalidOperationException("A committed CSS property set is immutable.");
        }
    }
}
