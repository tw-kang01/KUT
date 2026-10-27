# Build a fresh Win64 release candidate from a minimal staging tree.
param(
    [string]$EngineRoot = $env:UE_ROOT,
    [string]$OutputName = ('KHUTutor-UE5.8-Win64-' + (Get-Date -Format 'yyyyMMdd-HHmmss')),
    [switch]$PrepareOnly
)
$ErrorActionPreference = 'Stop'
if (-not $EngineRoot) { $EngineRoot = Join-Path $env:ProgramFiles 'Epic Games\UE_5.8' }
$root = [IO.Path]::GetFullPath((Split-Path -Parent $PSScriptRoot))
if ($OutputName -notmatch '^[A-Za-z0-9][A-Za-z0-9._-]{0,95}$' -or $OutputName -in @('.', '..')) { throw 'Use a simple release directory name.' }
$dist = Join-Path $root 'dist'
$out = [IO.Path]::GetFullPath((Join-Path $dist $OutputName))
if (-not $out.StartsWith($dist + '\', [StringComparison]::OrdinalIgnoreCase) -or (Test-Path -LiteralPath $out)) { throw 'Output must be a new directory inside dist; existing releases are never overwritten.' }
$stage = Join-Path $root ('.runtime\package\' + [guid]::NewGuid().ToString('N') + '\KHUTutor')
$source = Join-Path $root 'TutorSandbox\Plugins\KHUTutor'
New-Item -ItemType Directory -Path $out,$stage -Force | Out-Null
$utf8 = [Text.UTF8Encoding]::new($false)
function Save-Json($Value, [string]$Path) { [IO.File]::WriteAllText($Path, ($Value | ConvertTo-Json -Depth 30), $utf8) }
function Remove-Notes($Value) {
    if ($null -eq $Value) { return }
    if ($Value -is [pscustomobject]) {
        foreach ($p in @($Value.PSObject.Properties)) {
            if ($p.Name.StartsWith('_')) { $Value.PSObject.Properties.Remove($p.Name) }
            else { Remove-Notes $p.Value }
        }
    } elseif ($Value -is [array]) { foreach ($v in $Value) { Remove-Notes $v } }
}
Copy-Item -LiteralPath (Join-Path $source 'KHUTutor.uplugin') -Destination $stage
foreach ($dir in @('Source','Resources')) {
    foreach ($file in Get-ChildItem -LiteralPath (Join-Path $source $dir) -Recurse -File) {
        $relative = $file.FullName.Substring($source.Length + 1)
        if ($relative -match '(^|\\)(Tests|Runtime)(\\|$)') { continue }
        $target = Join-Path $stage $relative
        New-Item -ItemType Directory -Path (Split-Path -Parent $target) -Force | Out-Null
        Copy-Item -LiteralPath $file.FullName -Destination $target
    }
}
$config = Join-Path $stage 'Config'
New-Item -ItemType Directory -Path (Join-Path $config 'Prompts') -Force | Out-Null
$configNames = @('Budget.json','ClaudeRuntime.json','Context.json','ErrorPatterns.json','Gateway.json','LogDigest.json','Models.json','Pricing.json','ToolWhitelist.json')
foreach ($name in $configNames) {
    # Budgets and model limits are student policy, independent of developer overrides.
    $path = if ($name -in @('Budget.json','Models.json')) { Join-Path $root ('templates\Config\' + $name) } else { Join-Path $source ('Config\' + $name) }
    $value = Get-Content -LiteralPath $path -Raw -Encoding UTF8 | ConvertFrom-Json
    Remove-Notes $value
    if ($name -eq 'Models.json') {
        foreach ($p in @($value.tiers.PSObject.Properties)) { if ($p.Name -notin @('T1','T2')) { $value.tiers.PSObject.Properties.Remove($p.Name) } }
    }
    if ($name -eq 'ClaudeRuntime.json') { $value.executable = '' }
    Save-Json $value (Join-Path $config $name)
}
Save-Json ([ordered]@{version=1;scope='student_distribution';paid_calls_authorized=$false;unlimited_authorized=$false;allowed_models=@()}) (Join-Path $config 'DevelopmentBudget.json')
Save-Json ([ordered]@{version=1;url='http://127.0.0.1:8001/mcp';control_timeout_seconds=10;call_timeout_seconds=120}) (Join-Path $config 'Mcp.json')
Copy-Item -LiteralPath (Join-Path $source 'Config\Prompts\system.txt') -Destination (Join-Path $config 'Prompts\system.txt')
Copy-Item -LiteralPath (Join-Path $source 'Config\Prompts\l0_contract.txt') -Destination (Join-Path $config 'Prompts\l0_contract.txt')
[IO.File]::WriteAllText((Join-Path $config 'FilterPlugin.ini'), "[FilterPlugin]`n/Config/...`n-/Source/.../Tests/...`n-/Binaries/.../*.pdb`n", $utf8)
$packaged = Join-Path $out 'KHUTutor'
$status = [ordered]@{version=1;kind='internal_release_candidate';engine='5.8';platform='Win64';student_ready=$false;runtime_bundled=$false;runtime_reason='Claude Code deployment through the university gateway requires provider agreement confirmation; binary is not redistributed in this candidate.';gui_install_verified=$false;source_prompt_sha256=(Get-FileHash (Join-Path $config 'Prompts\system.txt')).Hash}
Save-Json $status (Join-Path $out 'DISTRIBUTION_STATUS.json')
if ($PrepareOnly) {
    Copy-Item -LiteralPath $stage -Destination $packaged -Recurse
    Write-Output "Prepared configuration/source candidate: $out"
    exit 0
}
$uat = Join-Path $EngineRoot 'Engine\Build\BatchFiles\RunUAT.bat'
if (-not (Test-Path -LiteralPath $uat)) { throw "Unreal AutomationTool not found: $uat" }
& $uat BuildPlugin "-Plugin=$stage\KHUTutor.uplugin" "-Package=$packaged" -TargetPlatforms=Win64 -HostPlatforms=Win64
if ($LASTEXITCODE -ne 0) { throw "BuildPlugin failed ($LASTEXITCODE). Candidate retained at $out" }
foreach ($file in Get-ChildItem -LiteralPath $packaged -Filter '*.pdb' -Recurse -File) {
    if (-not $file.FullName.StartsWith($packaged + '\', [StringComparison]::OrdinalIgnoreCase)) { throw 'Debug symbol path escaped candidate.' }
    Remove-Item -LiteralPath $file.FullName -Force
}
Copy-Item -LiteralPath (Join-Path $root 'docs\INSTALL_KO.md') -Destination (Join-Path $out 'INSTALL_KO.md')
& (Join-Path $PSScriptRoot 'verify_package.ps1') -Package $out -EngineRoot $EngineRoot
if (-not $?) { throw 'Package verification failed.' }
Compress-Archive -LiteralPath $out -DestinationPath ($out + '.zip') -CompressionLevel Optimal
Write-Output "Verified internal release candidate: $out.zip"
