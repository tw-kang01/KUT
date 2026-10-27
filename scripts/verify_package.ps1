param([Parameter(Mandatory=$true)][string]$Package, [string]$EngineRoot=$env:UE_ROOT, [switch]$AllowSourceOnly)
$ErrorActionPreference = 'Stop'
if (-not $EngineRoot) { $EngineRoot = Join-Path $env:ProgramFiles 'Epic Games\UE_5.8' }
$packageRoot = (Resolve-Path -LiteralPath $Package).Path
$plugin = Join-Path $packageRoot 'KHUTutor'
$config = Join-Path $plugin 'Config'
$required = @('Budget.json','ClaudeRuntime.json','Context.json','DevelopmentBudget.json','ErrorPatterns.json','Gateway.json','LogDigest.json','Mcp.json','Models.json','Pricing.json','ToolWhitelist.json','Prompts\system.txt','Prompts\l0_contract.txt')
foreach ($name in $required) { if (-not (Test-Path -LiteralPath (Join-Path $config $name))) { throw "Missing packaged config: $name" } }
foreach ($name in $required | Where-Object { $_.EndsWith('.json') }) { Get-Content -LiteralPath (Join-Path $config $name) -Raw -Encoding UTF8 | ConvertFrom-Json | Out-Null }
$dev = Get-Content (Join-Path $config 'DevelopmentBudget.json') -Raw -Encoding UTF8 | ConvertFrom-Json
if ($dev.scope -ne 'student_distribution' -or $dev.paid_calls_authorized -ne $false -or $dev.unlimited_authorized -ne $false -or @($dev.allowed_models).Count -ne 0) { throw 'Developer authorization leaked into package.' }
# unlimited_authorized=false also keeps the development subscription connection (KHU_DEV_CLAUDE_OAUTH_TOKEN) inert in student installs.
$contract = Join-Path (Split-Path -Parent $PSScriptRoot) 'TutorSandbox\Plugins\KHUTutor\Config\Prompts\l0_contract.txt'
if ((Get-FileHash (Join-Path $config 'Prompts\l0_contract.txt')).Hash -ne (Get-FileHash $contract).Hash) { throw 'Packaged L0 contract differs from the product file.' }
$budget = Get-Content (Join-Path $config 'Budget.json') -Raw -Encoding UTF8 | ConvertFrom-Json
$expected = Get-Content (Join-Path (Split-Path -Parent $PSScriptRoot) 'templates\Config\Budget.json') -Raw -Encoding UTF8 | ConvertFrom-Json
if ($budget.per_task_soft -ne $expected.per_task_soft -or $budget.per_task_hard -ne $expected.per_task_hard) { throw 'Package does not use the student task budget.' }
$runtime = Get-Content (Join-Path $config 'ClaudeRuntime.json') -Raw -Encoding UTF8 | ConvertFrom-Json
if ($runtime.executable -ne '' -or $runtime.panel_enabled -ne $true) { throw 'Invalid packaged runtime settings.' }
$models = Get-Content (Join-Path $config 'Models.json') -Raw -Encoding UTF8 | ConvertFrom-Json
if ((@($models.tiers.PSObject.Properties.Name | Sort-Object) -join ',') -cne 'T1,T2') { throw 'Student package requires exactly T1 and T2 model tiers.' }
# Compare the complete student policy, ignoring documentation notes and JSON property order.
# This also catches changed model chains/input caps and future policy fields, not just max_iters.
function Get-PolicyJson($Value) {
    if ($Value -is [pscustomobject]) {
        $pairs = @(foreach ($p in @($Value.PSObject.Properties | Where-Object { -not $_.Name.StartsWith('_') } | Sort-Object Name)) {
            ($p.Name | ConvertTo-Json -Compress) + ':' + (Get-PolicyJson $p.Value)
        })
        return '{' + ($pairs -join ',') + '}'
    }
    if ($Value -is [array]) {
        $items = @(foreach ($item in $Value) { Get-PolicyJson $item })
        return '[' + ($items -join ',') + ']'
    }
    return ConvertTo-Json -InputObject $Value -Compress -Depth 30
}
$studentModels = Get-Content (Join-Path (Split-Path -Parent $PSScriptRoot) 'templates\Config\Models.json') -Raw -Encoding UTF8 | ConvertFrom-Json
foreach ($p in @($studentModels.tiers.PSObject.Properties)) { if ($p.Name -notin @('T1','T2')) { $studentModels.tiers.PSObject.Properties.Remove($p.Name) } }
if ((Get-PolicyJson $models) -cne (Get-PolicyJson $studentModels)) { throw 'Package does not use the student model policy (templates/Config/Models.json).' }
$status = Get-Content (Join-Path $packageRoot 'DISTRIBUTION_STATUS.json') -Raw -Encoding UTF8 | ConvertFrom-Json
if ($status.runtime_bundled -ne $false -or $status.student_ready -ne $false) { throw 'This candidate must not claim verified runtime deployment or student readiness.' }
if ($status.source_prompt_sha256 -ne (Get-FileHash (Join-Path $config 'Prompts\system.txt')).Hash) { throw 'Packaged product prompt differs from staged source.' }
$descriptor = Get-Content (Join-Path $plugin 'KHUTutor.uplugin') -Raw -Encoding UTF8 | ConvertFrom-Json
if ('ToolsetRegistry' -notin @($descriptor.Plugins.Name) -or 'ModelContextProtocol' -in @($descriptor.Plugins.Name)) { throw 'Unexpected plugin dependency.' }
$engineModules = Get-Content (Join-Path $EngineRoot 'Engine\Binaries\Win64\UnrealEditor.modules') -Raw | ConvertFrom-Json
if (-not $AllowSourceOnly) {
    $modules = Get-Content (Join-Path $plugin 'Binaries\Win64\UnrealEditor.modules') -Raw | ConvertFrom-Json
    if ($modules.BuildId -ne $engineModules.BuildId) { throw 'Installed engine and plugin BuildId differ.' }
    foreach ($name in @('KHUTutor','KHUTutorTools')) {
        $dll = $modules.Modules.$name
        if ($dll -ne ('UnrealEditor-' + $name + '.dll') -or -not (Test-Path -LiteralPath (Join-Path $plugin ('Binaries\Win64\' + $dll)) -PathType Leaf)) { throw "Missing DLL: $name" }
    }
}
$files = @(Get-ChildItem -LiteralPath $plugin -Recurse -File)
foreach ($file in $files) {
    $relative = $file.FullName.Substring($plugin.Length + 1)
    if ($relative -match '(^|\\)(Saved|Tests|Scenarios|fixtures|HostProject)(\\|$)' -or $relative -match '\.(pdb|dpapi|jsonl|exe)$') { throw "Unwanted distribution file: $relative" }
    if ($file.Extension -in @('.json','.ini','.txt','.md','.cs','.cpp','.h')) {
        $text = Get-Content -LiteralPath $file.FullName -Raw -Encoding UTF8
        if ($text -match '(?i)[A-Z]:[\\/]+Users[\\/]+[^\\/\s]+|sk-[A-Za-z0-9_-]{20,}') { throw "Personal path or credential-like literal in $relative" }
    }
}
$manifest = @($files | ForEach-Object { [ordered]@{path=$_.FullName.Substring($plugin.Length + 1).Replace('\','/');bytes=$_.Length;sha256=(Get-FileHash -LiteralPath $_.FullName).Hash} })
$result = [ordered]@{version=1;verified_utc=[DateTime]::UtcNow.ToString('o');files=$files.Count;source_only=$AllowSourceOnly.IsPresent;engine_build_id=$engineModules.BuildId;student_ready=$false;runtime_bundled=$false;manifest=$manifest}
[IO.File]::WriteAllText((Join-Path $packageRoot 'PACKAGE_AUDIT.json'), ($result | ConvertTo-Json -Depth 8), [Text.UTF8Encoding]::new($false))
Write-Output "Package audit passed: $($files.Count) files; student development privileges disabled; runtime deployment pending."
