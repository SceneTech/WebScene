using WebScene.Core;

namespace WebScene.Backends.Native;

/// <summary>
/// A JavaScript program that runs during document start, before authored
/// scripts in the selected browsing contexts.
/// </summary>
public sealed record WebSceneDocumentScript(
    string Source,
    string Name,
    bool AllFrames = true);

/// <summary>Options for loading a document in a native WebScene backend.</summary>
public sealed record NativeWebSceneLoadOptions
{
    public required string Source { get; init; }

    public required string NativeLibraryPath { get; init; }

    public string? CompilationCacheDirectory { get; init; }

    /// <summary>
    /// Gets the host-controlled root for durable browser storage. IndexedDB is
    /// unavailable when this or <see cref="PersistentStoragePartitionKey"/> is omitted.
    /// </summary>
    public string? PersistentStorageDirectory { get; init; }

    /// <summary>
    /// Gets the stable application/profile identity used above origin isolation.
    /// Loopback applications may keep this value stable when their port changes.
    /// </summary>
    public string? PersistentStoragePartitionKey { get; init; }

    /// <summary>Gets the profile quota in bytes, or zero for the native default.</summary>
    public ulong PersistentStorageQuotaBytes { get; init; }

    public IReadOnlyList<WebSceneDocumentScript> DocumentStartScripts { get; init; } = [];

    /// <summary>
    /// Gets the resource policy for this document. When omitted, the platform
    /// backend's default resource loader is used.
    /// </summary>
    public IWebSceneResourceLoader? ResourceLoader { get; init; }
}
