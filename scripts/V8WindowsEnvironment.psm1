Set-StrictMode -Version Latest

$script:WindowsDeveloperEnvironmentKeys = [System.Collections.Generic.HashSet[string]]::new(
    [System.StringComparer]::OrdinalIgnoreCase)
@(
    "CommandPromptType", "DevEnvDir", "ExtensionSdkDir", "EXTERNAL_INCLUDE",
    "Framework40Version", "FrameworkDir", "FrameworkDir64", "FrameworkVersion",
    "FrameworkVersion64", "IFCPATH", "INCLUDE", "LIB", "LIBPATH", "NETFXSDKDir",
    "Platform", "UCRTVersion", "UniversalCRTSdkDir", "VCIDEInstallDir",
    "VCINSTALLDIR", "VCToolsInstallDir", "VCToolsRedistDir", "VCToolsVersion",
    "VisualStudioVersion", "VS170COMNTOOLS", "VSINSTALLDIR", "VSSDK150INSTALL",
    "VSSDKINSTALL", "WindowsLibPath", "WindowsSDK_ExecutablePath_x64",
    "WindowsSDK_ExecutablePath_x86", "WindowsSdkBinPath", "WindowsSdkDir",
    "WindowsSDKLibVersion", "WindowsSdkVerBinPath", "WindowsSDKVersion"
) | ForEach-Object { [void] $script:WindowsDeveloperEnvironmentKeys.Add($_) }

function New-WebSceneV8ChildEnvironment {
    [CmdletBinding()]
    param(
        [Parameter(Mandatory = $true)]
        [System.Collections.IDictionary] $Environment
    )

    $child = [System.Collections.Generic.Dictionary[string,string]]::new(
        [System.StringComparer]::OrdinalIgnoreCase)
    foreach ($entry in $Environment.GetEnumerator()) {
        if ($null -ne $entry.Value) {
            $child[[string] $entry.Key] = [string] $entry.Value
        }
    }

    $contaminated = $false
    foreach ($key in $child.Keys) {
        if ($script:WindowsDeveloperEnvironmentKeys.Contains($key) `
            -or $key.StartsWith("VSCMD_", [System.StringComparison]::OrdinalIgnoreCase) `
            -or $key.StartsWith("__VSCMD_", [System.StringComparison]::OrdinalIgnoreCase)) {
            $contaminated = $true
            break
        }
    }
    if (-not $contaminated) {
        return $child
    }
    if (-not $child.ContainsKey("__VSCMD_PREINIT_PATH") `
        -or [string]::IsNullOrWhiteSpace($child["__VSCMD_PREINIT_PATH"])) {
        throw "An initialized Visual Studio environment must expose __VSCMD_PREINIT_PATH before V8 toolchain discovery."
    }

    $originalPath = $child["__VSCMD_PREINIT_PATH"]
    foreach ($key in @($child.Keys)) {
        if ($script:WindowsDeveloperEnvironmentKeys.Contains($key) `
            -or $key.StartsWith("VSCMD_", [System.StringComparison]::OrdinalIgnoreCase) `
            -or $key.StartsWith("__VSCMD_", [System.StringComparison]::OrdinalIgnoreCase)) {
            [void] $child.Remove($key)
        }
    }
    $child["Path"] = $originalPath
    return $child
}

function Invoke-WebSceneV8ChildPowerShell {
    [CmdletBinding()]
    param(
        [Parameter(Mandatory = $true)]
        [string] $ScriptPath,

        [string[]] $ArgumentList = @(),

        [System.Collections.IDictionary] $Environment
    )

    if ($null -eq $Environment) {
        $Environment = [System.Collections.Generic.Dictionary[string,string]]::new(
            [System.StringComparer]::OrdinalIgnoreCase)
        foreach ($entry in Get-ChildItem Env:) {
            $Environment[$entry.Name] = $entry.Value
        }
    }
    $childEnvironment = New-WebSceneV8ChildEnvironment -Environment $Environment

    $startInfo = [System.Diagnostics.ProcessStartInfo]::new()
    $startInfo.FileName = (Get-Process -Id $PID).Path
    $startInfo.UseShellExecute = $false
    [void] $startInfo.ArgumentList.Add("-NoLogo")
    [void] $startInfo.ArgumentList.Add("-NoProfile")
    [void] $startInfo.ArgumentList.Add("-File")
    [void] $startInfo.ArgumentList.Add((Resolve-Path $ScriptPath).Path)
    foreach ($argument in $ArgumentList) {
        [void] $startInfo.ArgumentList.Add($argument)
    }
    $startInfo.Environment.Clear()
    foreach ($entry in $childEnvironment.GetEnumerator()) {
        $startInfo.Environment[$entry.Key] = $entry.Value
    }

    $process = [System.Diagnostics.Process]::Start($startInfo)
    $process.WaitForExit()
    if ($process.ExitCode -ne 0) {
        throw "The clean V8 child process exited with code $($process.ExitCode)."
    }
}

Export-ModuleMember -Function New-WebSceneV8ChildEnvironment, Invoke-WebSceneV8ChildPowerShell
