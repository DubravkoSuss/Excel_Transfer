param(
    [Parameter(Mandatory=$true)]
    [string]$FilePath
)

$excel = $null
$wb = $null

try {
    $FilePath = [System.IO.Path]::GetFullPath($FilePath)

    if (-not (Test-Path $FilePath)) {
        Write-Error "File not found: $FilePath"
        exit 1
    }

    # Copy to local temp path to avoid network drive COM restrictions
    $tempPath = [System.IO.Path]::Combine($env:TEMP, "exceltransfer_repair_" + [System.IO.Path]::GetFileName($FilePath))
    Copy-Item -Path $FilePath -Destination $tempPath -Force

    $excel = New-Object -ComObject Excel.Application
    $excel.Visible = $false
    $excel.DisplayAlerts = $false
    $excel.AskToUpdateLinks = $false
    $excel.AlertBeforeOverwriting = $false

    # Open local copy with CorruptLoad = xlRepairFile (2) — silently accepts repair dialog
    $wb = $excel.Workbooks.Open(
        $tempPath,
        0,                # UpdateLinks = don't update
        $false,           # ReadOnly = false
        [Type]::Missing,  # Format
        [Type]::Missing,  # Password
        [Type]::Missing,  # WriteResPassword
        $true,            # IgnoreReadOnlyRecommended
        [Type]::Missing,  # Origin
        [Type]::Missing,  # Delimiter
        [Type]::Missing,  # Editable
        [Type]::Missing,  # Notify
        [Type]::Missing,  # Converter
        $false,           # AddToMru
        [Type]::Missing,  # Local
        2                 # CorruptLoad = xlRepairFile
    )

    # Save clean local copy then copy back to original location
    $wb.Save()
    $wb.Close($false)
    [System.Runtime.Interopservices.Marshal]::ReleaseComObject($wb) | Out-Null
    $wb = $null
    $excel.Quit()
    [System.Runtime.Interopservices.Marshal]::ReleaseComObject($excel) | Out-Null
    $excel = $null
    [GC]::Collect()
    [GC]::WaitForPendingFinalizers()

    # Copy repaired file back to original location
    Copy-Item -Path $tempPath -Destination $FilePath -Force
    Remove-Item -Path $tempPath -Force -ErrorAction SilentlyContinue

    Write-Output "OK"
    exit 0
}
catch {
    Write-Error $_.Exception.Message
    exit 1
}
finally {
    if ($wb -ne $null) {
        try { $wb.Close($false) } catch {}
        [System.Runtime.Interopservices.Marshal]::ReleaseComObject($wb) | Out-Null
    }
    if ($excel -ne $null) {
        try { $excel.Quit() } catch {}
        [System.Runtime.Interopservices.Marshal]::ReleaseComObject($excel) | Out-Null
    }
    [GC]::Collect()
    [GC]::WaitForPendingFinalizers()
    # Clean up temp file if still exists
    if ($tempPath -and (Test-Path $tempPath)) {
        Remove-Item -Path $tempPath -Force -ErrorAction SilentlyContinue
    }
}
