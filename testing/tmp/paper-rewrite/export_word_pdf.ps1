param([string]$InputDoc, [string]$OutputPdf)
$ErrorActionPreference = 'Stop'
$wordApp = $null
$wordDoc = $null
try {
    $wordApp = New-Object -ComObject Word.Application
    $wordApp.Visible = $false
    $wordApp.DisplayAlerts = 0
    $wordApp.AutomationSecurity = 3
    $wordDoc = $wordApp.Documents.Open($InputDoc, $false, $true, $false)
    $wordDoc.Repaginate()
    $wordDoc.ExportAsFixedFormat($OutputPdf, 17)
    Write-Output ('Rendered pages: ' + $wordDoc.ComputeStatistics(2))
} finally {
    if ($null -ne $wordDoc) { $wordDoc.Close(0); [void][Runtime.InteropServices.Marshal]::ReleaseComObject($wordDoc) }
    if ($null -ne $wordApp) { $wordApp.Quit([ref]0); [void][Runtime.InteropServices.Marshal]::ReleaseComObject($wordApp) }
}
