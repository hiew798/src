#Requires -Version 7.0
<##
Generate a new, matching certificate/private-key pair for the Heltec HTTPS server.
Uses .NET cryptography in PowerShell 7; OpenSSL is not required.
Run from any directory. Existing files require -Force to replace.
##>
[CmdletBinding()]
param(
    [string]$OutputDirectory = (Split-Path -Parent $PSScriptRoot),
    [string]$IpAddress = '192.168.4.1',
    [ValidateRange(1, 3650)][int]$ValidityDays = 365,
    [DateTimeOffset]$ValidFrom = ([DateTimeOffset]::UtcNow.AddDays(-1)),
    [switch]$Force
)
$ErrorActionPreference = 'Stop'
$taskOutput = [System.IO.Path]::GetFullPath($OutputDirectory)
$taskIp = [System.Net.IPAddress]::Parse($IpAddress)
$taskFiles = @('cert_der.h', 'key_der.h', 'heltec-sos.cer')
foreach ($taskFile in $taskFiles) {
    if ((Test-Path -LiteralPath (Join-Path $taskOutput $taskFile)) -and -not $Force) {
        throw "File already exists: $taskFile. Use -Force only to replace the existing pair."
    }
}
[System.IO.Directory]::CreateDirectory($taskOutput) | Out-Null

function ConvertTo-Header {
    param([byte[]]$Bytes, [string]$Symbol, [string]$Guard, [string]$Description)
    $taskLines = [System.Collections.Generic.List[string]]::new()
    $taskLines.Add("#ifndef $Guard")
    $taskLines.Add("#define $Guard")
    $taskLines.Add('#include <stdint.h>')
    $taskLines.Add("// $Description")
    $taskLines.Add("static unsigned char $Symbol`[] = {")
    for ($taskOffset = 0; $taskOffset -lt $Bytes.Length; $taskOffset += 12) {
        $taskEnd = [Math]::Min($taskOffset + 11, $Bytes.Length - 1)
        $taskValues = $Bytes[$taskOffset..$taskEnd] | ForEach-Object { '0x{0:X2}' -f $_ }
        $taskLines.Add('    ' + ($taskValues -join ', ') + ',')
    }
    $taskLines.Add('};')
    $taskLines.Add("static const uint16_t $($Symbol)_len = sizeof($Symbol);")
    $taskLines.Add('#endif')
    return ($taskLines -join "`n") + "`n"
}

$taskRsa = [System.Security.Cryptography.RSA]::Create(2048)
$taskCertificate = $null
$taskParsedKey = $null
$taskPublicKey = $null
try {
    $taskRequest = [System.Security.Cryptography.X509Certificates.CertificateRequest]::new(
        "CN=$IpAddress, O=Heltec LoRa SOS",
        $taskRsa,
        [System.Security.Cryptography.HashAlgorithmName]::SHA256,
        [System.Security.Cryptography.RSASignaturePadding]::Pkcs1
    )
    $taskSan = [System.Security.Cryptography.X509Certificates.SubjectAlternativeNameBuilder]::new()
    $taskSan.AddIpAddress($taskIp)
    $taskSan.AddDnsName('heltec-lora.local')
    $taskRequest.CertificateExtensions.Add($taskSan.Build())
    $taskRequest.CertificateExtensions.Add(
        [System.Security.Cryptography.X509Certificates.X509BasicConstraintsExtension]::new($false, $false, 0, $true)
    )
    $taskKeyUsage = [System.Security.Cryptography.X509Certificates.X509KeyUsageFlags]::DigitalSignature -bor
                    [System.Security.Cryptography.X509Certificates.X509KeyUsageFlags]::KeyEncipherment
    $taskRequest.CertificateExtensions.Add(
        [System.Security.Cryptography.X509Certificates.X509KeyUsageExtension]::new($taskKeyUsage, $true)
    )
    $taskPurposes = [System.Security.Cryptography.OidCollection]::new()
    $taskPurposes.Add([System.Security.Cryptography.Oid]::new('1.3.6.1.5.5.7.3.1')) | Out-Null
    $taskRequest.CertificateExtensions.Add(
        [System.Security.Cryptography.X509Certificates.X509EnhancedKeyUsageExtension]::new($taskPurposes, $false)
    )
    $taskRequest.CertificateExtensions.Add(
        [System.Security.Cryptography.X509Certificates.X509SubjectKeyIdentifierExtension]::new($taskRequest.PublicKey, $false)
    )
    $taskCertificate = $taskRequest.CreateSelfSigned($ValidFrom, $ValidFrom.AddDays($ValidityDays))
    $taskCertBytes = $taskCertificate.Export([System.Security.Cryptography.X509Certificates.X509ContentType]::Cert)
    # PKCS#1 DER is accepted by the original ESP32 HTTPS Server library.
    $taskKeyBytes = $taskRsa.ExportRSAPrivateKey()

    # Validate a round trip and prove that the private key matches the certificate.
    $taskParsedKey = [System.Security.Cryptography.RSA]::Create()
    $taskBytesRead = 0
    $taskParsedKey.ImportRSAPrivateKey($taskKeyBytes, [ref]$taskBytesRead)
    if ($taskBytesRead -ne $taskKeyBytes.Length) { throw 'Private-key DER validation failed.' }
    $taskPublicKey = [System.Security.Cryptography.X509Certificates.RSACertificateExtensions]::GetRSAPublicKey($taskCertificate)
    $taskProbe = [System.Text.Encoding]::UTF8.GetBytes('Heltec certificate/key validation')
    $taskSignature = $taskParsedKey.SignData($taskProbe,
        [System.Security.Cryptography.HashAlgorithmName]::SHA256,
        [System.Security.Cryptography.RSASignaturePadding]::Pkcs1)
    if (-not $taskPublicKey.VerifyData($taskProbe, $taskSignature,
        [System.Security.Cryptography.HashAlgorithmName]::SHA256,
        [System.Security.Cryptography.RSASignaturePadding]::Pkcs1)) {
        throw 'Certificate and private key do not match.'
    }

    $taskCertHeader = ConvertTo-Header $taskCertBytes 'cert_der' 'CERT_DER_H' 'Public certificate, X.509 DER.'
    $taskKeyHeader = ConvertTo-Header $taskKeyBytes 'key_der' 'KEY_DER_H' 'PRIVATE RSA KEY. Keep this file private; do not publish it.'
    [System.IO.File]::WriteAllText((Join-Path $taskOutput 'cert_der.h'), $taskCertHeader)
    [System.IO.File]::WriteAllText((Join-Path $taskOutput 'key_der.h'), $taskKeyHeader)
    [System.IO.File]::WriteAllBytes((Join-Path $taskOutput 'heltec-sos.cer'), $taskCertBytes)
    Write-Output "Created cert_der.h and key_der.h in $taskOutput"
    Write-Output 'Validated: RSA-2048 key matches the certificate.'
    Write-Output "Certificate IP: $IpAddress"
    Write-Output ('Valid from: {0:u}; expires: {1:u}' -f $ValidFrom, $ValidFrom.AddDays($ValidityDays))
    Write-Output ('SHA-256 fingerprint: ' + $taskCertificate.GetCertHashString([System.Security.Cryptography.HashAlgorithmName]::SHA256))
    Write-Output 'heltec-sos.cer contains the public certificate only. No trust store was changed.'
} finally {
    if ($taskPublicKey) { $taskPublicKey.Dispose() }
    if ($taskParsedKey) { $taskParsedKey.Dispose() }
    if ($taskCertificate) { $taskCertificate.Dispose() }
    $taskRsa.Dispose()
}
