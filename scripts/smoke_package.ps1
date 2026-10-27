# Headless load in a new Blueprint-only project. No model request, key copy or editor GUI automation.
param([Parameter(Mandatory=$true)][string]$Package, [string]$EngineRoot=$env:UE_ROOT)
$ErrorActionPreference = 'Stop'
if (-not $EngineRoot) { $EngineRoot = Join-Path $env:ProgramFiles 'Epic Games\UE_5.8' }
$packageRoot = (Resolve-Path -LiteralPath $Package).Path
& (Join-Path $PSScriptRoot 'verify_package.ps1') -Package $packageRoot -EngineRoot $EngineRoot
$root = Split-Path -Parent $PSScriptRoot
$smokeRoot = Join-Path $root ('.runtime\package-smoke\' + [guid]::NewGuid().ToString('N'))
New-Item -ItemType Directory -Path (Join-Path $smokeRoot 'Plugins') -Force | Out-Null
Copy-Item -LiteralPath (Join-Path $packageRoot 'KHUTutor') -Destination (Join-Path $smokeRoot 'Plugins\KHUTutor') -Recurse
$project = Join-Path $smokeRoot 'PackageSmoke.uproject'
$descriptor = [ordered]@{FileVersion=3;EngineAssociation='5.8';Plugins=@(@{Name='KHUTutor';Enabled=$true},@{Name='ModelContextProtocol';Enabled=$false})}
[IO.File]::WriteAllText($project, ($descriptor | ConvertTo-Json -Depth 8), [Text.UTF8Encoding]::new($false))
$log = Join-Path $smokeRoot 'load.log'
& (Join-Path $EngineRoot 'Engine\Binaries\Win64\UnrealEditor-Cmd.exe') $project '-TestExit=Tutor panels use local editor tools.' "-abslog=$log" -unattended -nopause -nullrhi -nosplash -nosound -stdout
if ($LASTEXITCODE -ne 0) { throw "Fresh-project load failed ($LASTEXITCODE). Inspect $log" }
$text = Get-Content -LiteralPath $log -Raw -Encoding UTF8
if ($text -notmatch 'Tutor panels use local editor tools' -or $text -notmatch 'Registering Toolset KHUTutorTools.KHUTutorReadToolset') { throw 'Both packaged modules were not confirmed loaded.' }
# older builds log the warning as "D7 config:"
if ($text -match '(D7|KHUTutor) config:|Missing or invalid read/show whitelist|Unable to load module|could not be found|Fatal error:|LogModuleManager: Warning') { throw 'Package load/configuration error in log.' }
if (Test-Path -LiteralPath (Join-Path $smokeRoot 'Source')) { throw 'Smoke project unexpectedly contains C++ source.' }
$result = [ordered]@{version=1;headless_loaded=$true;blueprint_only=$true;model_calls=0;gui_verified=$false;runtime_bundled=$false;project=$project.Substring($root.Length + 1).Replace('\','/');log=$log.Substring($root.Length + 1).Replace('\','/')}
[IO.File]::WriteAllText((Join-Path $packageRoot 'SMOKE_RESULT.json'), ($result | ConvertTo-Json -Depth 5), [Text.UTF8Encoding]::new($false))
Write-Output "Packaged modules loaded in fresh Blueprint-only project: $project"
