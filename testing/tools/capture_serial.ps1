param(
    [Parameter(Mandatory=$true)][string]$Port,
    [Parameter(Mandatory=$true)][string]$OutputFile,
    [int]$Minutes = 10
)
$ErrorActionPreference = 'Stop'
if (Test-Path -LiteralPath $OutputFile) { throw 'Choose a new log filename; existing measurements are retained.' }
$serial = [System.IO.Ports.SerialPort]::new($Port, 115200)
$serial.ReadTimeout = 1000
$serial.DtrEnable = $false
$serial.RtsEnable = $false
$writer = $null
try {
    $serial.Open()
    $writer = [IO.StreamWriter]::new($OutputFile, $false)
    $writer.AutoFlush = $true
    Write-Output "Recording $Port to $OutputFile. Close Arduino Serial Monitor first. Ctrl+C ends capture."
    $end = [DateTime]::UtcNow.AddMinutes($Minutes)
    while ([DateTime]::UtcNow -lt $end) {
        try {
            $line = $serial.ReadLine()
            $writer.WriteLine($line.TrimEnd())
            if ($line.StartsWith('[EXP],') -or $line.StartsWith('[TRANSFER]')) { Write-Output $line.TrimEnd() }
        } catch [System.TimeoutException] { }
    }
} finally {
    if ($writer) { $writer.Dispose() }
    if ($serial.IsOpen) { $serial.Close() }
    $serial.Dispose()
}
