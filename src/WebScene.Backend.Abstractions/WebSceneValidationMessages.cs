namespace WebScene.Backends.Native;

/// <summary>Stable reasons for browser-owned constraint-validation messages.</summary>
public enum WebSceneValidationMessageReason
{
    ValueMissing = 1,
    TypeMismatch = 2,
    PatternMismatch = 3,
    TooLong = 4,
    TooShort = 5,
    RangeUnderflow = 6,
    RangeOverflow = 7,
    StepMismatch = 8,
    BadInput = 9
}

/// <summary>Identifies a bounded substitution supplied with a validation reason.</summary>
public enum WebSceneValidationMessageArgumentKind
{
    ControlType = 1,
    Pattern = 2,
    MinimumLength = 3,
    MaximumLength = 4,
    Minimum = 5,
    Maximum = 6,
    Step = 7
}

public readonly record struct WebSceneValidationMessageArgument(
    WebSceneValidationMessageArgumentKind Kind,
    string Value);

/// <summary>
/// Formats browser-owned constraint-validation text. Return null or an empty
/// string to use WebScene's English fallback. The formatter is invoked on the
/// native engine worker and must not call back into the engine.
/// </summary>
public delegate string? WebSceneValidationMessageFormatter(
    WebSceneValidationMessageReason reason,
    IReadOnlyList<WebSceneValidationMessageArgument> arguments);
