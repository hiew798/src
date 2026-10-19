param(
    [string]$Cli = 'E:\arduino\Arduino IDE\resources\app\lib\backend\resources\arduino-cli.exe',
    [string]$CliConfig = 'C:\Users\cyftl\AppData\Local\Temp\heltec-recovery-c49a58378659482f85cb1a945e4032eb\arduino-cli.yaml',
    [string]$OutputRoot = (Join-Path $env:TEMP ('heltec-transfer-builds-' + (Get-Date -Format 'yyyyMMdd-HHmmss')))
)
$ErrorActionPreference = 'Stop'
$project = Split-Path -Parent $PSScriptRoot
$stage = Join-Path $OutputRoot 'source\testing'
$build = Join-Path $OutputRoot 'build'
New-Item -ItemType Directory -Path $stage -Force | Out-Null
# Compile a private snapshot. The user's selected role and mode are never rewritten.
Get-ChildItem -LiteralPath $project -File | Where-Object { $_.Extension -in '.h', '.ino' } | ForEach-Object {
    Copy-Item -LiteralPath $_.FullName -Destination $stage
}
$configText = [IO.File]::ReadAllText((Join-Path $stage 'Config.h'))
$modeText = [IO.File]::ReadAllText((Join-Path $stage 'TransferConfig.h'))
Write-Output "Private build directory: $OutputRoot"
foreach ($mode in 1..4) {
    foreach ($role in 0..1) {
        $roleName = if ($role -eq 0) { 'civilian' } else { 'rescuer' }
        $name = "mode-$mode-$roleName"
        $destination = Join-Path $OutputRoot $name
        New-Item -ItemType Directory -Path $destination -Force | Out-Null
        $roleConfig = [regex]::Replace($configText, '(?m)^#define CURRENT_NODE_ROLE [^\r\n]+', "#define CURRENT_NODE_ROLE $role")
        $modeConfig = [regex]::Replace($modeText, '(?m)^#define TRANSFER_MODE [^\r\n]+', "#define TRANSFER_MODE $mode")
        [IO.File]::WriteAllText((Join-Path $stage 'Config.h'), $roleConfig)
        [IO.File]::WriteAllText((Join-Path $stage 'TransferConfig.h'), $modeConfig)
        $manifest = Get-ChildItem -LiteralPath $stage -File | ForEach-Object {
            [pscustomobject]@{name=$_.Name; sha256=(Get-FileHash -LiteralPath $_.FullName -Algorithm SHA256).Hash}
        }
        $manifest | ConvertTo-Json | Set-Content -LiteralPath (Join-Path $destination 'source-sha256.json')
        $arguments = @('compile', '--fqbn', 'esp32:esp32:heltec_wifi_lora_32_V3', '--build-path', $build, '--output-dir', $destination)
        if ($CliConfig -and (Test-Path -LiteralPath $CliConfig)) { $arguments += @('--config-file', $CliConfig) }
        $arguments += $stage
        $log = Join-Path $destination 'compile.log'
        & $Cli @arguments *> $log
        if ($LASTEXITCODE -ne 0) { throw "$name failed. Compiler log: $log" }
        Write-Output "PASS $name"
        Select-String -LiteralPath $log -Pattern 'Sketch uses|Global variables use' | ForEach-Object { $_.Line }
    }
}
