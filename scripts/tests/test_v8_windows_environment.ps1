$ErrorActionPreference = "Stop"
Set-StrictMode -Version Latest

Import-Module (Join-Path $PSScriptRoot "../V8WindowsEnvironment.psm1") -Force

function Assert-Equal([object] $Expected, [object] $Actual, [string] $Message) {
    if ($Expected -ne $Actual) {
        throw "$Message Expected '$Expected', got '$Actual'."
    }
}

function Assert-False([bool] $Value, [string] $Message) {
    if ($Value) { throw $Message }
}

$outer = [System.Collections.Generic.Dictionary[string,string]]::new(
    [System.StringComparer]::OrdinalIgnoreCase)
$outer["Path"] = "developer-tools;system-tools"
$outer["__VSCMD_PREINIT_PATH"] = "system-tools"
$outer["VSCMD_ARG_HOST_ARCH"] = "x64"
$outer["VSCMD_ARG_TGT_ARCH"] = "x64"
$outer["VSCMD_VER"] = "17.14"
$outer["VSINSTALLDIR"] = 'C:\VisualStudio'
$outer["INCLUDE"] = "duplicated-includes"
$outer["LIB"] = "duplicated-libraries"
$outer["WindowsSDKVersion"] = "10.0.28000.0"
$outer["RUNNER_TEMP"] = 'C:\runner-temp'
$outer["WEBSCENE_KEEP"] = "preserved"
$original = [System.Collections.Generic.Dictionary[string,string]]::new(
    $outer, [System.StringComparer]::OrdinalIgnoreCase)

$child = New-WebSceneV8ChildEnvironment -Environment $outer

Assert-Equal $original["Path"] $outer["Path"] "The caller environment was modified."
Assert-Equal $original["INCLUDE"] $outer["INCLUDE"] "The caller compiler environment was modified."
Assert-Equal "system-tools" $child["Path"] "The child PATH was not restored."
Assert-Equal 'C:\runner-temp' $child["RUNNER_TEMP"] "An unrelated runner value was removed."
Assert-Equal "preserved" $child["WEBSCENE_KEEP"] "An unrelated WebScene value was removed."
foreach ($key in $child.Keys) {
    Assert-False ($key -match '^(VSCMD_|__VSCMD_)') "A VSCMD variable leaked into the child: $key"
}
foreach ($key in @("INCLUDE", "LIB", "VSINSTALLDIR", "WindowsSDKVersion")) {
    Assert-False $child.ContainsKey($key) "A developer variable leaked into the child: $key"
}

foreach ($partialState in @(
    @{ Path = "developer-tools"; VSCMD_ARG_TGT_ARCH = "x64" },
    @{ Path = "developer-tools"; INCLUDE = "developer-includes" },
    @{ Path = "developer-tools"; LIB = "developer-libraries" }
)) {
    $missingOriginalPath = [System.Collections.Generic.Dictionary[string,string]]::new(
        [System.StringComparer]::OrdinalIgnoreCase)
    foreach ($entry in $partialState.GetEnumerator()) {
        $missingOriginalPath[$entry.Key] = $entry.Value
    }
    $failedClosed = $false
    try {
        $null = New-WebSceneV8ChildEnvironment -Environment $missingOriginalPath
    } catch {
        $failedClosed = $_.Exception.Message -match "__VSCMD_PREINIT_PATH"
    }
    if (-not $failedClosed) {
        throw "Partial developer state without __VSCMD_PREINIT_PATH did not fail closed: $($partialState.Keys -join ', ')"
    }
}

$temporary = Join-Path ([System.IO.Path]::GetTempPath()) ("webscene-v8-env-" + [guid]::NewGuid())
New-Item -ItemType Directory -Path $temporary | Out-Null
$childScript = Join-Path $temporary "capture.ps1"
$childOutput = Join-Path $temporary "child.json"
@'
param([string] $ArgumentValue)
@{
    Path = $env:PATH
    Vscmd = $env:VSCMD_VER
    Include = $env:INCLUDE
    Keep = $env:WEBSCENE_KEEP
    ArgumentValue = $ArgumentValue
} | ConvertTo-Json | Set-Content -Path $env:WEBSCENE_V8_TEST_OUTPUT
'@ | Set-Content -Path $childScript

$saved = @{}
foreach ($key in @("PATH", "__VSCMD_PREINIT_PATH", "VSCMD_VER", "INCLUDE", "WEBSCENE_KEEP", "WEBSCENE_V8_TEST_OUTPUT")) {
    $saved[$key] = [System.Environment]::GetEnvironmentVariable($key)
}
try {
    $basePath = $env:PATH
    $env:__VSCMD_PREINIT_PATH = $basePath
    $env:PATH = "developer-tools$([System.IO.Path]::PathSeparator)$basePath"
    $env:VSCMD_VER = "17.14"
    $env:INCLUDE = "developer-includes"
    $env:WEBSCENE_KEEP = "child-visible"
    $env:WEBSCENE_V8_TEST_OUTPUT = $childOutput
    $callerPath = $env:PATH
    $argumentValue = 'spaces ; ampersand & dollar $ quote " apostrophe '' unicode ż'

    Invoke-WebSceneV8ChildPowerShell `
        -ScriptPath $childScript `
        -ArgumentList @("-ArgumentValue", $argumentValue)
    $captured = Get-Content -Raw -Path $childOutput | ConvertFrom-Json

    Assert-Equal $basePath $captured.Path "The launched child did not receive the original PATH."
    Assert-Equal $null $captured.Vscmd "VSCMD state leaked into the launched child."
    Assert-Equal $null $captured.Include "Compiler include state leaked into the launched child."
    Assert-Equal "child-visible" $captured.Keep "An unrelated value was not passed to the child."
    Assert-Equal $argumentValue $captured.ArgumentValue "A child argument was re-parsed or corrupted."
    Assert-Equal $callerPath $env:PATH "Launching the child changed the caller PATH."
    Assert-Equal "17.14" $env:VSCMD_VER "Launching the child changed the caller VSCMD state."
    Assert-Equal "developer-includes" $env:INCLUDE "Launching the child changed the caller compiler state."
} finally {
    foreach ($key in $saved.Keys) {
        [System.Environment]::SetEnvironmentVariable($key, $saved[$key])
    }
    Remove-Item -Recurse -Force $temporary
}

Write-Host "V8 Windows child environment contracts passed."
