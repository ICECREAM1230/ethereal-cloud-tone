$ErrorActionPreference = 'Stop'
$scriptPath = Join-Path $PSScriptRoot 'find-gowin-project.ps1'
$tempRoot = Join-Path ([System.IO.Path]::GetTempPath()) ('gowin-nav-' + [guid]::NewGuid().ToString('N'))
$projectRoot = Join-Path $tempRoot '中文 project'
try {
    New-Item -ItemType Directory -Path (Join-Path $projectRoot 'src') -Force | Out-Null
    New-Item -ItemType Directory -Path (Join-Path $projectRoot 'impl\gwsynthesis') -Force | Out-Null
    New-Item -ItemType Directory -Path (Join-Path $projectRoot 'impl\pnr') -Force | Out-Null
    Set-Content -LiteralPath (Join-Path $projectRoot 'src\a&b.v') -Value 'module x; endmodule' -Encoding UTF8
    Set-Content -LiteralPath (Join-Path $projectRoot 'impl\gwsynthesis\generated.v') -Value 'module g; endmodule' -Encoding UTF8
    $hiddenFile = Join-Path $projectRoot '.inventory-fixture'
    Set-Content -LiteralPath $hiddenFile -Value 'hidden inventory evidence' -Encoding UTF8
    if ([System.IO.Path]::DirectorySeparatorChar -eq '\') { [System.IO.File]::SetAttributes($hiddenFile, [System.IO.FileAttributes]::Hidden) }
    @'
<?xml version="1"?>
<Project>
  <Device
    pn='GW5AST-LV138PG484AC1/I0'/>
  <FileList>
    <!-- <File path='ghost.v' type='file.verilog' enable='1'/> -->
    <File path='src/a&amp;b.v'
          type='file.verilog'
          enable='1'/>
  </FileList>
</Project>
'@ | Set-Content -LiteralPath (Join-Path $projectRoot 'fixture.gprj') -Encoding UTF8
    @'
{
  "IncludePath": [
    "src",
    "路径 with space"
  ],
  "TopModule": "x",
  "topmodule": "x",
  "KEY_TEXT": "must-not-print",
  "to\u006ben": "escaped-marker",
  "Other": {"same": {"password": "nested-marker"}, "same": 0},
  "CPU": [{"cre\u0064ential": "array-marker"}],
  "Route_Option": {"safe": "data contains \"token\": but is not a key"}
}
'@ | Set-Content -LiteralPath (Join-Path $projectRoot 'impl\process_config.json') -Encoding UTF8

    $jsonText = & $scriptPath -Root $tempRoot -AsJson
    $record = $jsonText | ConvertFrom-Json
    if ($record.Device -ne 'GW5AST-LV138PG484AC1/I0') { throw 'single-quoted multiline Device was not parsed' }
    if ($record.ProjectFiles.Count -ne 1 -or $record.ProjectFiles[0].Path -ne 'src/a&b.v' -or -not $record.ProjectFiles[0].Exists) { throw 'multiline/entity File was not parsed' }
    if ($record.FileCounts.synthesis -lt 1) { throw 'impl HDL was not classified as synthesis' }
    if ($record.FileCounts.other -ne 2) { throw 'hidden file was omitted from inventory' }
    $include = $record.ProcessConfigDetails[0].Fields | Where-Object Key -eq 'IncludePath'
    if ($include.Value -notmatch '路径 with space') { throw 'complex JSON value was truncated' }
    if ($jsonText -match 'must-not-print') { throw 'sensitive process field leaked in JSON' }
    if ($jsonText -match 'escaped-marker|nested-marker|array-marker') { throw 'escaped/nested/duplicate sensitive field leaked in JSON' }
    foreach ($key in @('token', 'Other', 'CPU')) {
        $field = @($record.ProcessConfigDetails[0].Fields | Where-Object Key -eq $key)
        if ($field.Count -ne 1 -or $field[0].Value -ne '<redacted>') { throw "nested redaction failed for $key" }
    }
    if ($record.ProcessConfigDetails[0].CaseVariantKeyGroups -notcontains 'TopModule') { throw 'case-variant keys were lost' }
    $safeData = $record.ProcessConfigDetails[0].Fields | Where-Object Key -eq 'Route_Option'
    if ($safeData.Value -eq '<redacted>') { throw 'string data was incorrectly treated as an object key' }
    $textOutput = (& $scriptPath -Project (Join-Path $projectRoot 'fixture.gprj') -Root $tempRoot) -join "`n"
    if ($textOutput -match 'must-not-print') { throw 'sensitive unselected process field leaked' }
    if ($textOutput -match 'escaped-marker|nested-marker|array-marker') { throw 'nested sensitive field leaked in text' }

    # A child's build artifacts are not evidence about the selected parent.
    $nested = Join-Path $projectRoot 'nested'
    New-Item -ItemType Directory -Path (Join-Path $nested 'impl\pnr') -Force | Out-Null
    Set-Content -LiteralPath (Join-Path $nested 'child.gprj') -Value '<Project><Device pn="CHILD"/></Project>' -Encoding UTF8
    Set-Content -LiteralPath (Join-Path $nested 'shared.v') -Value 'module shared; endmodule' -Encoding UTF8
    Set-Content -LiteralPath (Join-Path $nested 'impl\process_config.json') -Value '{"TopModule":"child_top"}' -Encoding UTF8
    Set-Content -LiteralPath (Join-Path $nested 'impl\pnr\child.fs') -Value 'child image' -Encoding UTF8
    $selected = Join-Path $projectRoot 'fixture.gprj'
    Set-Content -LiteralPath $selected -Value '<Project><Device name="" pn=""> gw5ast138c-007 </Device><FileList><File path="nested/shared.v" enable="0"/></FileList></Project>' -Encoding UTF8
    $record = (& $scriptPath -Root $tempRoot -Project $selected -AsJson) | ConvertFrom-Json
    if ($record.ProcessConfigFiles.Count -ne 1 -or $record.FileCounts.project -ne 1 -or $record.RepresentativeOutputs.Count) { throw 'nested project polluted parent inventory' }
    if ($record.NestedProjects.Count -ne 1 -or $record.NestedProjects[0] -notmatch 'nested\\child.gprj$') { throw 'nested project exclusion was not reported' }
    if (-not $record.ProjectFiles[0].Exists -or $record.ProjectFiles[0].Enabled -ne '0') { throw 'manifest reference to nested/shared source was lost' }
    if ($record.Device -ne 'gw5ast138c-007' -or $record.DeviceIdentifier -ne 'gw5ast138c-007' -or $null -ne $record.DevicePartNumber) { throw 'device text was lost or mistaken for a full part number' }
    Set-Content -LiteralPath $selected -Value '<Project><Device name="GW5AST-138C" pn="FULL-PART">gw5ast138c-007</Device></Project>' -Encoding UTF8
    $record = (& $scriptPath -Root $tempRoot -Project $selected -AsJson) | ConvertFrom-Json
    if ($record.Device -ne 'FULL-PART' -or $record.DevicePartNumber -ne 'FULL-PART' -or $record.DeviceIdentifier -ne 'gw5ast138c-007') { throw 'explicit part number precedence failed' }

    $scope = Join-Path $tempRoot 'scope'
    $sibling = Join-Path $tempRoot 'scope-extra'
    New-Item -ItemType Directory -Path $scope, $sibling -Force | Out-Null
    $outside = Join-Path $sibling 'outside.gprj'
    Set-Content -LiteralPath $outside -Value '<Project/>' -Encoding UTF8
    foreach ($badRoot in @($scope, $selected)) {
        $wasRejected = $false
        try { $null = & $scriptPath -Root $badRoot -Project $outside -AsJson } catch { $wasRejected = $true }
        if (-not $wasRejected) { throw 'outside project or non-directory -Root was accepted' }
    }

    Set-Content -LiteralPath (Join-Path $projectRoot 'impl\process_config.json') -Value '{"TopModule":{"token":"invalid-secret-marker"}, BROKEN}' -Encoding UTF8
    $invalidOutput = (& $scriptPath -Root $tempRoot -Project $selected -AsJson) -join "`n"
    $invalidRecord = $invalidOutput | ConvertFrom-Json
    if ($invalidOutput -match 'invalid-secret-marker' -or $invalidRecord.ProcessConfigDetails[0].Fields.Count -or $invalidRecord.ProcessConfigDetails[0].ParseStatus -notlike 'diagnostic-only:*') { throw 'invalid JSON exposed partial fields or parser input' }
    $malformed = Join-Path $tempRoot 'malformed.gprj'
    Set-Content -LiteralPath $malformed -Value '<Project><FileList><File path="real.v" enable="1"/>' -Encoding UTF8
    $wasRejected = $false
    try { $null = & $scriptPath -Project $malformed -Root $tempRoot -AsJson } catch { $wasRejected = $true }
    if (-not $wasRejected) { throw 'malformed XML was accepted' }
    Write-Output 'PASS: XML, artifact classification, complex/case-variant JSON, nested/escaped/duplicate secret redaction, invalid JSON, nested project isolation, device declarations, and -Root containment.'
} finally {
    if (Test-Path -LiteralPath $tempRoot) { Remove-Item -LiteralPath $tempRoot -Recurse -Force }
}
