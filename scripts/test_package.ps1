# Mutate only a generated source-only candidate to verify the release audit fails closed.
param([Parameter(Mandatory=$true)][string]$Package)
$ErrorActionPreference = 'Stop'
$packageRoot = (Resolve-Path -LiteralPath $Package).Path
$workspaceDist = [IO.Path]::GetFullPath((Join-Path (Split-Path -Parent $PSScriptRoot) 'dist'))
if (-not $packageRoot.StartsWith($workspaceDist + '\', [StringComparison]::OrdinalIgnoreCase)) { throw 'Use an owned candidate in this workspace dist directory.' }
$audit = Join-Path $PSScriptRoot 'verify_package.ps1'
$checks = 0
& $audit -Package $packageRoot -AllowSourceOnly
$checks++
function Reject-Change([string]$Name, [string]$Relative, [scriptblock]$Change) {
    $path = Join-Path $packageRoot $Relative
    $original = [IO.File]::ReadAllBytes($path)
    try {
        & $Change $path
        $rejected = $false
        try { & $audit -Package $packageRoot -AllowSourceOnly | Out-Null } catch { $rejected = $true }
        if (-not $rejected) { throw "Audit accepted unsafe candidate: $Name" }
    } finally { [IO.File]::WriteAllBytes($path, $original) }
    $script:checks++
}
$utf8 = [Text.UTF8Encoding]::new($false)
Reject-Change 'developer paid authorization' 'KHUTutor\Config\DevelopmentBudget.json' {
    param($path) $o = Get-Content $path -Raw | ConvertFrom-Json; $o.paid_calls_authorized = $true; $o.unlimited_authorized = $true
    [IO.File]::WriteAllText($path, ($o | ConvertTo-Json -Depth 8), $utf8)
}
Reject-Change 'developer task allowance' 'KHUTutor\Config\Budget.json' {
    param($path) $o = Get-Content $path -Raw | ConvertFrom-Json; $o.per_task_soft = 4000
    [IO.File]::WriteAllText($path, ($o | ConvertTo-Json -Depth 8), $utf8)
}
Reject-Change 'developer executable path' 'KHUTutor\Config\ClaudeRuntime.json' {
    param($path) $o = Get-Content $path -Raw | ConvertFrom-Json; $o.executable = 'C:\Users\ExampleStudent\private\claude.exe'
    [IO.File]::WriteAllText($path, ($o | ConvertTo-Json -Depth 8), $utf8)
}
foreach ($tier in @('T1','T2')) {
    Reject-Change "$tier developer iteration allowance" 'KHUTutor\Config\Models.json' {
        param($path) $o = Get-Content $path -Raw | ConvertFrom-Json; $o.tiers.$tier.max_iters = 20
        [IO.File]::WriteAllText($path, ($o | ConvertTo-Json -Depth 8), $utf8)
    }
    Reject-Change "missing $tier student tier" 'KHUTutor\Config\Models.json' {
        param($path) $o = Get-Content $path -Raw | ConvertFrom-Json; $o.tiers.PSObject.Properties.Remove($tier)
        [IO.File]::WriteAllText($path, ($o | ConvertTo-Json -Depth 8), $utf8)
    }
}
Reject-Change 'extra development model tier' 'KHUTutor\Config\Models.json' {
    param($path) $o = Get-Content $path -Raw | ConvertFrom-Json; $o.tiers | Add-Member -NotePropertyName D7 -NotePropertyValue $o.tiers.T1
    [IO.File]::WriteAllText($path, ($o | ConvertTo-Json -Depth 8), $utf8)
}
Reject-Change 'changed student model chain' 'KHUTutor\Config\Models.json' {
    param($path) $o = Get-Content $path -Raw | ConvertFrom-Json; $o.tiers.T1.chain = @('unapproved-fixture-model')
    [IO.File]::WriteAllText($path, ($o | ConvertTo-Json -Depth 8), $utf8)
}
Reject-Change 'changed student input allowance' 'KHUTutor\Config\Models.json' {
    param($path) $o = Get-Content $path -Raw | ConvertFrom-Json; $o.tiers.T2.input_budget = 100000
    [IO.File]::WriteAllText($path, ($o | ConvertTo-Json -Depth 8), $utf8)
}
Reject-Change 'changed disabled model policy' 'KHUTutor\Config\Models.json' {
    param($path) $o = Get-Content $path -Raw | ConvertFrom-Json; $o.disabled_patterns = @()
    [IO.File]::WriteAllText($path, ($o | ConvertTo-Json -Depth 8), $utf8)
}
Reject-Change 'credential-like literal' 'KHUTutor\Config\Mcp.json' {
    param($path) $o = Get-Content $path -Raw | ConvertFrom-Json; $o | Add-Member -NotePropertyName test_marker -NotePropertyValue 'sk-package-audit-synthetic-credential-1234567890'
    [IO.File]::WriteAllText($path, ($o | ConvertTo-Json -Depth 8), $utf8)
}
Reject-Change 'missing required prompt' 'KHUTutor\Config\Prompts\system.txt' { param($path) Remove-Item -LiteralPath $path -Force }
Reject-Change 'false student readiness' 'DISTRIBUTION_STATUS.json' {
    param($path) $o = Get-Content $path -Raw | ConvertFrom-Json; $o.student_ready = $true
    [IO.File]::WriteAllText($path, ($o | ConvertTo-Json -Depth 8), $utf8)
}
& $audit -Package $packageRoot -AllowSourceOnly
$checks++
Write-Output "Package policy checks passed: $checks. Mutations restored."
