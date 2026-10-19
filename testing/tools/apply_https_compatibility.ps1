#Requires -Version 7.0
# Apply the ESP32-S3 / Arduino-ESP32 2.0.17 SHA compatibility fix to
# ESP32_HTTPS_Server 1.0.0. Original files are backed up before editing.
[CmdletBinding()]
param(
    [string]$LibraryDirectory = (Join-Path (Split-Path -Parent (Split-Path -Parent $PSScriptRoot)) 'libraries\ESP32_HTTPS_Server')
)
$ErrorActionPreference = 'Stop'
$taskPatches = @(
    @{
        File = 'src\HTTPConnection.hpp'
        Old = '#include <hwcrypto/sha.h>'
        New = '#include <mbedtls/sha1.h>'
    },
    @{
        File = 'src\HTTPConnection.cpp'
        Old = '  esp_sha(SHA1, (uint8_t*)newKey.data(), newKey.length(), shaData);'
        New = "  if (mbedtls_sha1_ret((const unsigned char*)newKey.data(), newKey.length(), shaData) != 0) {`n    return std::string();`n  }"
    }
)
# Validate both source patterns before changing either file.
$taskChanges = @()
foreach ($taskPatch in $taskPatches) {
    $taskPath = Join-Path $LibraryDirectory $taskPatch.File
    $taskText = [System.IO.File]::ReadAllText($taskPath).Replace("`r`n", "`n")
    if ($taskText.Contains($taskPatch.New)) {
        Write-Output "Already patched: $($taskPatch.File)"
    } elseif ($taskText.Contains($taskPatch.Old)) {
        $taskChanges += @{ Path = $taskPath; Text = $taskText.Replace($taskPatch.Old, $taskPatch.New) }
    } else {
        throw "Unrecognized library source: $taskPath. Expected ESP32_HTTPS_Server 1.0.0."
    }
}
foreach ($taskChange in $taskChanges) {
    $taskBackup = $taskChange.Path + '.before-heltec-recovery'
    if (-not (Test-Path -LiteralPath $taskBackup)) {
        Copy-Item -LiteralPath $taskChange.Path -Destination $taskBackup
    }
    [System.IO.File]::WriteAllText($taskChange.Path, $taskChange.Text)
    Write-Output "Patched: $($taskChange.Path)"
}
