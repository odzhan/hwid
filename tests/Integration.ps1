param([Parameter(Mandatory=$true)][string]$Executable)
$ErrorActionPreference = 'Stop'
# Keep hardware identifiers out of test logs and artifacts.
$text = (& $Executable --json --redact) -join "`n"
if ($LASTEXITCODE -ne 0) { throw 'Inventory failed' }
$inventory = $text | ConvertFrom-Json
if ($inventory.schemaVersion -ne 1 -or !$inventory.redacted) { throw 'Incorrect schema or redaction mode' }
$components = @('system','motherboard','bios','tpm','storage','network','usb','gpu','sdEmmc','cpu')
foreach ($component in $components) {
    $rows = @($inventory.$component)
    if ($rows.Count -eq 0) { throw "Missing results for $component" }
    foreach ($row in $rows) {
        if ($row.component -ne $component -or !$row.source -or !$row.identifierType -or !$row.status) { throw "Incomplete row for $component" }
        if ($row.available -ne ($row.status -eq 'available')) { throw "Inconsistent availability in $component" }
        foreach ($field in @('rawValue','normalizedValue','deviceName','error')) {
            if ($row.$field -and $row.$field -ne '[redacted]') { throw "Redaction leaked $component.$field" }
        }
    }
    Write-Output "$component`: $($rows.Count) records; $(@($rows | Where-Object available).Count) available"
}
$filtered = ((& $Executable --json --redact --cpu) -join "`n") | ConvertFrom-Json
if ($LASTEXITCODE -ne 0 -or @($filtered.cpu).Count -eq 0) { throw 'Component filter failed' }
foreach ($component in $components | Where-Object { $_ -ne 'cpu' }) {
    if (@($filtered.$component).Count -ne 0) { throw "Filter included $component" }
}
if (!($filtered.cpu | Where-Object { $_.identifierType -eq 'Unique CPU serial' -and !$_.available })) { throw 'CPU uniqueness semantics missing' }
