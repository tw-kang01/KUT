# KHU UE Tutor - headless automation tests (KHUTutor.*)
param([string]$EngineRoot = $env:UE_ROOT, [string]$Project = $env:KHU_PROJECT, [switch]$McpSmoke, [switch]$NoMcp, [switch]$D7Live, [switch]$D8Live, [switch]$ClaudeLive, [switch]$CoachLive, [string]$RealLive = '', [string]$BlindLive = '', [ValidateRange(0, 3)][int]$BlindStart = 0, [ValidateRange(0, 11)][int]$CoachStart = 0, [ValidateRange(0, 14)][int]$CoachEnd = 14, [string]$CoachTier = 'T1',[switch]$D7ConsoleSmoke, [ValidateSet('', 'C2065', 'C1083')][string]$ExpectedCppError = '')
$ErrorActionPreference = "Stop"
if (-not $EngineRoot) { $EngineRoot = Join-Path $env:ProgramFiles 'Epic Games\UE_5.8' }
if (-not $Project) { $Project = Join-Path (Split-Path -Parent $PSScriptRoot) 'TutorSandbox\TutorSandbox.uproject' }
if (-not (Test-Path -LiteralPath $Project)) { throw "Project not found: $Project" }
$Project = (Resolve-Path -LiteralPath $Project).Path
$projectDir = Split-Path -Parent $Project
$report = Join-Path $projectDir ('Saved\Automation\KHUTutor\' + (Get-Date -Format 'yyyyMMdd_HHmmss_fff'))
$log = Join-Path $projectDir 'Saved\Logs\KHU_D7_Tests.log'
$commands = 'Automation RunTests KHUTutor'
$extra = @()
if ($NoMcp) {
    if ($McpSmoke -or $D7Live) { throw 'NoMcp cannot be combined with development MCP smoke tests.' }
    $extra += '-DisablePlugins=ModelContextProtocol'
    $extra += '-KHUTutorNoMcp'
}
if ($McpSmoke) {
    if (Get-NetTCPConnection -LocalPort 8001 -State Listen -ErrorAction SilentlyContinue) { throw 'Port 8001 is occupied; close only TutorSandbox before the headless smoke test.' }
    $commands = 'ModelContextProtocol.StartServer 8001,Automation RunTests KHUTutor'
    $extra += '-KHUTutorMcpSmoke'
}
if ($ExpectedCppError) { $extra += "-KHUTutorExpectedCppError=$ExpectedCppError" }
if ($D7ConsoleSmoke) { $commands = $commands.Replace('Automation RunTests KHUTutor', 'KHUTutor.Ask smoke --mock scenario_a,Automation RunTests KHUTutor') }
if ($D7Live) {
    if (-not $McpSmoke) { throw 'D7Live requires -McpSmoke for this project only on 8001.' }
    $extra += '-KHUTutorD7Live'
}
if ($D8Live) {
    $extra += '-KHUTutorD8Live'
}
if ($ClaudeLive) { $extra += '-KHUTutorClaudeLive' }
if ($RealLive) { $extra += ('-KHUTutorRealLive=' + (Resolve-Path -LiteralPath $RealLive).Path) }
if ($BlindLive) { $extra += ('-KHUTutorBlindLive=' + (Resolve-Path -LiteralPath $BlindLive).Path); $extra += "-KHUTutorBlindStart=$BlindStart" }
# Paid E1 fixtures: cases 0-11 are independent, 12-14 continue case 11. A partial range re-checks only the affected cases.
if ($CoachLive) { $extra += '-KHUTutorCoachLive'; $extra += "-KHUTutorCoachStart=$CoachStart"; $extra += "-KHUTutorCoachEnd=$CoachEnd"; $extra += "-KHUTutorCoachTier=$CoachTier" }
# Wait for the automation queue; an immediate Quit can exit before tests finish.
$taskFixtureRoot = Join-Path $projectDir 'Saved\KHUTutor\D4_Source_Test'
$taskFixtureOutside = Join-Path $taskFixtureRoot 'Outside'
$taskFixtureSource = Join-Path $taskFixtureRoot 'Source'
$taskFixtureLink = Join-Path $taskFixtureSource 'D4_Link'
New-Item -ItemType Directory -Path $taskFixtureOutside,$taskFixtureSource -Force | Out-Null
if (Test-Path -LiteralPath $taskFixtureLink) { throw 'D4 boundary test link already exists; inspect before replacing it.' }
[System.IO.File]::WriteAllText((Join-Path $taskFixtureOutside 'Secret.cpp'), 'KHU_D4_BOUNDARY_SENTINEL', [System.Text.UTF8Encoding]::new($false))
New-Item -ItemType Junction -Path $taskFixtureLink -Target $taskFixtureOutside | Out-Null
try {
    & "$EngineRoot\Engine\Binaries\Win64\UnrealEditor-Cmd.exe" "$Project" "-ExecCmds=$commands" '-TestExit=Automation Test Queue Empty' "-ReportExportPath=$report" "-abslog=$log" -ModelContextProtocolPort=8001 -unattended -nopause -nullrhi -nosplash -nosound -stdout @extra
    $engineExitCode = $LASTEXITCODE
} finally {
    # Remove this test's junction only, without recursion or deleting its target.
    # Remove-Item asks about the link's children in non-interactive Windows PowerShell 5.1; a non-recursive delete drops just the link.
    [System.IO.Directory]::Delete((Resolve-Path -LiteralPath $taskFixtureLink).Path, $false)
}
if ($engineExitCode -ne 0) { exit $engineExitCode }
$index = Join-Path $report 'index.json'
if (-not (Test-Path -LiteralPath $index)) { throw 'Automation report missing; zero discovered tests is not a pass.' }
$summary = Get-Content -LiteralPath $index -Raw -Encoding utf8 | ConvertFrom-Json
# all discovered tests must pass; bump the count when tests are added or removed
$expectedTests = 177
$discovered = $summary.succeeded + $summary.succeededWithWarnings + $summary.failed + $summary.notRun + $summary.inProcess
Write-Output ("KHUTutor automation: discovered {0}, succeeded {1}, with warnings {2}, failed {3}, not run {4}, in process {5}" -f $discovered, $summary.succeeded, $summary.succeededWithWarnings, $summary.failed, $summary.notRun, $summary.inProcess)
if ($discovered -ne $expectedTests -or $summary.succeeded -ne $discovered) {
    throw "KHUTutor automation did not complete successfully. Inspect $index"
}
Write-Output "Verified KHUTutor automation report: $index"
exit 0
