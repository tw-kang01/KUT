# KHU UE Tutor - full editor build. Close TutorSandbox before using -EditorClosed.
param(
    [string]$EngineRoot = $env:UE_ROOT,
    [string]$Project = $env:KHU_PROJECT,
    [switch]$EditorClosed,
    [switch]$RefreshSourceFiles
)
$ErrorActionPreference = "Stop"
if (-not $EngineRoot) { $EngineRoot = Join-Path $env:ProgramFiles 'Epic Games\UE_5.8' }
if (-not $Project) { $Project = Join-Path (Split-Path -Parent $PSScriptRoot) 'TutorSandbox\TutorSandbox.uproject' }
if (-not (Test-Path -LiteralPath $Project)) { throw "Project not found: $Project" }
$Project = (Resolve-Path -LiteralPath $Project).Path
$proj = [System.IO.Path]::GetFileNameWithoutExtension($Project)
$extra = @()
if ($EditorClosed) {
    $editors = @(Get-CimInstance Win32_Process -Filter "Name = 'UnrealEditor.exe' OR Name = 'UnrealEditor-Cmd.exe'")
    foreach ($editor in $editors) {
        if (-not $editor.CommandLine -or $editor.CommandLine -match [regex]::Escape($proj)) {
            throw "Close the target editor, or verify its process identity before building."
        }
    }
    $extra += '-NoHotReloadFromIDE'
}
if ($RefreshSourceFiles) {
    if (-not $EditorClosed) { throw 'Refreshing source files requires -EditorClosed.' }
    # UE 5.8 TargetMakefile.GetLocation: only this project's generated target cache.
    # -NoUBTMakefiles does not prevent LoadMakefile in this installed BuildMode.
    $taskIntermediateDir = [System.IO.Path]::GetFullPath((Join-Path (Split-Path -Parent $Project) 'Intermediate'))
    $taskMakefilePath = [System.IO.Path]::GetFullPath((Join-Path $taskIntermediateDir "Build\Win64\x64\${proj}Editor\Development\Makefile.bin"))
    if (-not $taskMakefilePath.StartsWith($taskIntermediateDir + '\', [System.StringComparison]::OrdinalIgnoreCase)) { throw 'Makefile target escaped project Intermediate.' }
    if (Test-Path -LiteralPath $taskMakefilePath) { Remove-Item -LiteralPath $taskMakefilePath -Force }
}
$log = Join-Path (Split-Path -Parent $Project) 'Saved\Logs\KHU_Build.txt'
# Build only project outputs; never replace existing files in the shared installed engine.
# -EditorClosed disables IDE hot reload only after checking the target project's process.
& "$EngineRoot\Engine\Build\BatchFiles\Build.bat" "${proj}Editor" Win64 Development "-Project=$Project" -WaitMutex -NoXGE -NoEngineChanges "-Log=$log" @extra
exit $LASTEXITCODE
