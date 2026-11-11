[CmdletBinding()]
param(
    [Parameter(Position = 0)]
    [string]$Root = '.',

    [string]$Project,

    [switch]$AsJson
)

$ErrorActionPreference = 'Stop'
if ($Host.Name -ne 'ServerRemoteHost') {
    try { [Console]::OutputEncoding = New-Object System.Text.UTF8Encoding($false) } catch { }
}

function Resolve-FullPath {
    param([string]$LiteralPath)
    return (Resolve-Path -LiteralPath $LiteralPath -ErrorAction Stop).Path
}

function Get-RelativePath {
    param([string]$Base, [string]$Path)
    $separator = [System.IO.Path]::DirectorySeparatorChar
    $basePath = (Resolve-FullPath $Base).TrimEnd('\', '/') + $separator
    $pathItem = Get-Item -LiteralPath $Path -Force -ErrorAction Stop
    $pathText = $pathItem.FullName
    if ($pathItem.PSIsContainer) { $pathText = $pathText.TrimEnd('\', '/') + $separator }
    $baseUri = [System.Uri]::new($basePath, [System.UriKind]::Absolute)
    $pathUri = [System.Uri]::new($pathText, [System.UriKind]::Absolute)
    $relative = $baseUri.MakeRelativeUri($pathUri).ToString()
    $relative = [System.Uri]::UnescapeDataString($relative).Replace('/', '\')
    if ([string]::IsNullOrWhiteSpace($relative)) { return '.' }
    return $relative.TrimEnd('\')
}

function Read-SafeXml {
    param([string]$Text, [string]$Path)
    # Gowin projects have been observed with XML declaration version="1".
    # Normalize only that declaration in memory; never rewrite the project.
    $normalized = [regex]::Replace($Text, '(?is)<\?xml\s+version\s*=\s*(["''])1\1', '<?xml version="1.0"')
    $settings = New-Object System.Xml.XmlReaderSettings
    # Gowin emits a DOCTYPE in normal projects. Ignore it with no resolver so
    # neither external nor internal entity expansion is performed.
    $settings.DtdProcessing = [System.Xml.DtdProcessing]::Ignore
    $settings.XmlResolver = $null
    $stringReader = New-Object System.IO.StringReader($normalized)
    $reader = $null
    try {
        $reader = [System.Xml.XmlReader]::Create($stringReader, $settings)
        $document = New-Object System.Xml.XmlDocument
        $document.XmlResolver = $null
        $document.Load($reader)
        return $document
    } catch {
        throw "Malformed or unsafe XML in '$Path': $($_.Exception.Message)"
    } finally {
        if ($reader) { $reader.Dispose() }
        $stringReader.Dispose()
    }
}

function Get-ArtifactClass {
    param([string]$RelativePath)
    $p = $RelativePath.ToLowerInvariant()
    if ($p -match '(^|\\)impl\\gwsynthesis(\\|$)') { return 'synthesis' }
    if ($p -match '(^|\\)impl\\pnr(\\|$)') { return 'place-and-route' }
    if ($p -match '\.gprj(\.user)?$') { return 'project' }
    if ($p -match '\.(vp|vhdp|vgp)$') { return 'encrypted-source' }
    if ($p -match '(^|\\)src(\\|$)' -or $p -match '\.(v|sv|vh|svh|vhd|vhdl|cst|sdc|gsc|ipc|mod|gao|rao)$') { return 'source-or-constraint' }
    return 'other'
}

function Test-SensitiveJsonText {
    param([string]$Text)
    # Inspect every object key in validated JSON, including duplicate keys.
    # Decode individual string tokens; deserializing whole objects can discard
    # duplicate/case-variant keys and hide an earlier nested secret.
    $i = 0
    while ($i -lt $Text.Length) {
        if ($Text[$i] -ne '"') { $i++; continue }
        $start = $i
        $i++
        while ($i -lt $Text.Length) {
            if ($Text[$i] -eq '\') { $i += 2; continue }
            if ($Text[$i] -eq '"') { $i++; break }
            $i++
        }
        $end = $i
        while ($i -lt $Text.Length -and [char]::IsWhiteSpace($Text[$i])) { $i++ }
        if ($i -lt $Text.Length -and $Text[$i] -eq ':') {
            $key = ConvertFrom-Json -InputObject $Text.Substring($start, $end - $start)
            if ($key -match '(?i:key|password|secret|token|credential)') { return $true }
        }
    }
    return $false
}

function Get-ProjectInventory {
    param([string]$ProjectRoot)
    $files = New-Object 'System.Collections.Generic.List[System.IO.FileInfo]'
    $nestedProjects = New-Object 'System.Collections.Generic.List[System.IO.FileInfo]'
    $pending = New-Object 'System.Collections.Generic.Stack[string]'
    $pending.Push($ProjectRoot)
    while ($pending.Count) {
        $directory = $pending.Pop()
        $items = @(Get-ChildItem -LiteralPath $directory -Force -ErrorAction SilentlyContinue)
        $projectsHere = @($items | Where-Object { -not $_.PSIsContainer -and $_.Extension -eq '.gprj' })
        if ($directory -ne $ProjectRoot -and $projectsHere.Count) {
            foreach ($projectHere in $projectsHere) { $nestedProjects.Add($projectHere) }
            continue
        }
        foreach ($item in $items) {
            if (-not $item.PSIsContainer) { $files.Add($item) }
            elseif ($item.Name -notin @('.git', '.svn', 'node_modules') -and
                    -not ($item.Attributes -band [System.IO.FileAttributes]::ReparsePoint)) {
                $pending.Push($item.FullName)
            }
        }
    }
    return [pscustomobject]@{
        Files = @($files | Sort-Object FullName)
        NestedProjects = @($nestedProjects | Sort-Object FullName)
    }
}

function Get-ProcessConfigFields {
    param([string]$Path)
    $text = Get-Content -LiteralPath $Path -Raw -Encoding UTF8 -ErrorAction Stop
    $fields = @()
    $cursor = 0
    $keyPattern = [regex]'(?s)"(?<key>(?:\\.|[^"\\])*)"\s*:'
    try {
        if (-not $text.TrimStart().StartsWith('{')) { throw 'Top level must be a JSON object.' }
        if ('System.Text.Json.JsonDocument' -as [type]) {
            $jsonDocument = [System.Text.Json.JsonDocument]::Parse($text)
            $jsonDocument.Dispose()
        } else {
            Add-Type -AssemblyName System.Web.Extensions -ErrorAction Stop
            $validator = New-Object System.Web.Script.Serialization.JavaScriptSerializer
            $validator.MaxJsonLength = [int]::MaxValue
            $null = $validator.DeserializeObject($text)
        }
        while ($cursor -lt $text.Length) {
            $match = $keyPattern.Match($text, $cursor)
            if (-not $match.Success) { break }
            $key = ConvertFrom-Json -InputObject ('"' + $match.Groups['key'].Value + '"')
            $i = $match.Index + $match.Length
            while ($i -lt $text.Length -and [char]::IsWhiteSpace($text[$i])) { $i++ }
            $start = $i
            $inString = $false
            $escaped = $false
            $depth = 0
            while ($i -lt $text.Length) {
                $ch = $text[$i]
                if ($inString) {
                    if ($escaped) { $escaped = $false }
                    elseif ($ch -eq '\') { $escaped = $true }
                    elseif ($ch -eq '"') { $inString = $false }
                } else {
                    if ($ch -eq '"') { $inString = $true }
                    elseif ($ch -eq '[' -or $ch -eq '{') { $depth++ }
                    elseif ($ch -eq ']' -or $ch -eq '}') {
                        if ($depth -eq 0) { break }
                        $depth--
                    } elseif ($ch -eq ',' -and $depth -eq 0) { break }
                }
                $i++
            }
            $raw = $text.Substring($start, $i - $start).Trim()
            if ($key -match '(?i:key|password|secret|token|credential)' -or (Test-SensitiveJsonText $raw)) {
                $raw = '<redacted>'
            }
            $fields += [pscustomobject]@{ Key = $key; Value = $raw }
            $cursor = [Math]::Max($i + 1, $match.Index + $match.Length)
        }
        return [pscustomobject]@{ ParseStatus = 'valid-json/top-level-raw-values'; Fields = @($fields) }
    } catch {
        # Parser exceptions can contain input fragments. Do not emit them or
        # partially parsed fields when validation/redaction fails.
        return [pscustomobject]@{ ParseStatus = 'diagnostic-only: JSON validation or safe extraction failed'; Fields = @() }
    }
}

$resolvedInput = if ($Project) { Resolve-FullPath $Project } else { Resolve-FullPath $Root }
$inputItem = Get-Item -LiteralPath $resolvedInput -Force
if ($Project) {
    if ($inputItem.PSIsContainer -or $inputItem.Extension -ne '.gprj') { throw '-Project must name one .gprj file.' }
    $scanRoot = Resolve-FullPath $Root
    if (-not (Get-Item -LiteralPath $scanRoot -Force).PSIsContainer) { throw '-Root must be a directory when -Project is given.' }
    $rootPrefix = $scanRoot.TrimEnd('\', '/') + [System.IO.Path]::DirectorySeparatorChar
    $comparison = if ([System.IO.Path]::DirectorySeparatorChar -eq '\') { [System.StringComparison]::OrdinalIgnoreCase } else { [System.StringComparison]::Ordinal }
    if (-not $inputItem.FullName.StartsWith($rootPrefix, $comparison)) { throw '-Project must be a .gprj inside -Root.' }
    $projects = @($inputItem)
} elseif (-not $inputItem.PSIsContainer) {
    if ($inputItem.Extension -ne '.gprj') { throw 'When -Root names a file, it must be a .gprj file.' }
    $scanRoot = $inputItem.Directory.FullName
    $projects = @($inputItem)
} else {
    $scanRoot = $inputItem.FullName
    $projects = @(Get-ChildItem -LiteralPath $scanRoot -Recurse -Force -File -Filter '*.gprj' -ErrorAction SilentlyContinue |
        Where-Object { $_.FullName -notmatch '[\\/](?:\.git|\.svn|node_modules)[\\/]' } | Sort-Object FullName)
}

$records = foreach ($projectFile in $projects) {
    $projectRoot = $projectFile.Directory.FullName
    $userFile = Join-Path $projectRoot ($projectFile.BaseName + '.gprj.user')
    $inventory = Get-ProjectInventory $projectRoot
    $allFiles = @($inventory.Files)
    $projectText = Get-Content -LiteralPath $projectFile.FullName -Raw -Encoding UTF8
    $projectXml = Read-SafeXml -Text $projectText -Path $projectFile.FullName
    $deviceNode = $projectXml.SelectSingleNode("//*[local-name()='Device']")
    $devicePart = if ($deviceNode -and $deviceNode.GetAttribute('pn').Trim()) { $deviceNode.GetAttribute('pn').Trim() } else { $null }
    $deviceName = if ($deviceNode -and $deviceNode.GetAttribute('name').Trim()) { $deviceNode.GetAttribute('name').Trim() } else { $null }
    $deviceIdentifier = if ($deviceNode -and $deviceNode.InnerText.Trim()) { $deviceNode.InnerText.Trim() } else { $null }
    $device = if ($devicePart) { $devicePart } elseif ($deviceName) { $deviceName } elseif ($deviceIdentifier) { $deviceIdentifier } else { '<not declared>' }

    $projectFiles = @(
        foreach ($fileNode in $projectXml.SelectNodes("//*[local-name()='FileList']/*[local-name()='File']")) {
            $filePath = $fileNode.GetAttribute('path')
            if (-not $filePath) { continue }
            $candidate = if ([System.IO.Path]::IsPathRooted($filePath)) { $filePath } else { Join-Path $projectRoot ($filePath -replace '[\\/]', [string][System.IO.Path]::DirectorySeparatorChar) }
            [pscustomobject]@{ Path = $filePath; Type = $fileNode.GetAttribute('type'); Enabled = $fileNode.GetAttribute('enable'); Exists = Test-Path -LiteralPath $candidate }
        }
    )

    $registeredResults = @()
    $flowProcesses = @()
    if (Test-Path -LiteralPath $userFile) {
        $userText = Get-Content -LiteralPath $userFile -Raw -Encoding UTF8
        $userXml = Read-SafeXml -Text $userText -Path $userFile
        $registeredResults = @(
            foreach ($resultNode in $userXml.SelectNodes("//*[local-name()='ResultFile']")) {
                $resultPath = $resultNode.GetAttribute('ResultFilePath')
                if (-not $resultPath) { continue }
                $candidate = if ([System.IO.Path]::IsPathRooted($resultPath)) { $resultPath } else { Join-Path $projectRoot ($resultPath -replace '[\\/]', [string][System.IO.Path]::DirectorySeparatorChar) }
                [pscustomobject]@{ Type = $resultNode.GetAttribute('ResultFileType'); Path = $resultPath; Exists = Test-Path -LiteralPath $candidate }
            }
        )
        $flowProcesses = @(
            foreach ($processNode in $userXml.SelectNodes("//*[local-name()='Process']")) {
                if ($processNode.GetAttribute('ID')) { [pscustomobject]@{ Id = $processNode.GetAttribute('ID'); State = $processNode.GetAttribute('State') } }
            }
        )
    }

    $processConfigs = @($allFiles | Where-Object { $_.Name -eq 'process_config.json' -or $_.Name -like '*_process_config.json' } | Sort-Object FullName)
    $processConfigDetails = @(
        foreach ($config in $processConfigs) {
            $parsed = Get-ProcessConfigFields $config.FullName
            $caseVariants = @($parsed.Fields | Group-Object Key | Where-Object Count -gt 1 | ForEach-Object Name)
            $safeFields = @($parsed.Fields | ForEach-Object {
                if ($_.Key -match '(?i:key|password|secret|token|credential)') {
                    [pscustomobject]@{ Key = $_.Key; Value = '<redacted>' }
                } else { $_ }
            })
            [pscustomobject]@{
                Path = Get-RelativePath -Base $scanRoot -Path $config.FullName
                ParseStatus = $parsed.ParseStatus
                Fields = $safeFields
                CaseVariantKeyGroups = $caseVariants
            }
        }
    )

    $relativeFiles = @($allFiles | ForEach-Object { Get-RelativePath -Base $projectRoot -Path $_.FullName })
    $counts = @{}
    foreach ($class in @('project','source-or-constraint','encrypted-source','synthesis','place-and-route','other')) {
        $counts[$class] = @($relativeFiles | Where-Object { (Get-ArtifactClass $_) -eq $class }).Count
    }
    $relativeRoot = Get-RelativePath -Base $scanRoot -Path $projectRoot

    [pscustomobject]@{
        ProjectFile = Get-RelativePath -Base $scanRoot -Path $projectFile.FullName
        ProjectRoot = $relativeRoot
        Device = $device
        DevicePartNumber = $devicePart
        DeviceName = $deviceName
        DeviceIdentifier = $deviceIdentifier
        NestedProjects = @($inventory.NestedProjects | ForEach-Object { Get-RelativePath -Base $scanRoot -Path $_.FullName })
        UserStateFile = if (Test-Path -LiteralPath $userFile) { Get-RelativePath -Base $scanRoot -Path $userFile } else { $null }
        SourceDirectory = if (Test-Path -LiteralPath (Join-Path $projectRoot 'src')) { Join-Path $relativeRoot 'src' } else { $null }
        SynthesisDirectory = if (Test-Path -LiteralPath (Join-Path $projectRoot 'impl\gwsynthesis')) { Join-Path $relativeRoot 'impl\gwsynthesis' } else { $null }
        PnrDirectory = if (Test-Path -LiteralPath (Join-Path $projectRoot 'impl\pnr')) { Join-Path $relativeRoot 'impl\pnr' } else { $null }
        SimulationDirectories = @($allFiles | Where-Object { $_.DirectoryName -match '[\\/](?:sim|simulation)(?:[\\/]|$)' } | ForEach-Object { Get-RelativePath -Base $scanRoot -Path $_.Directory.FullName } | Sort-Object -Unique)
        ProcessConfigFiles = @($processConfigs | ForEach-Object { Get-RelativePath -Base $scanRoot -Path $_.FullName })
        ProcessConfigDetails = $processConfigDetails
        ProjectFiles = $projectFiles
        RegisteredResults = $registeredResults
        FlowProcesses = $flowProcesses
        FileCounts = $counts
        EncryptedFiles = @($relativeFiles | Where-Object { $_ -match '\.(vp|vhdp|vgp)$' } | Sort-Object)
        RepresentativeOutputs = @($relativeFiles | Where-Object { $_ -match '\.(vg|rpt\.html|rpt\.txt|tr\.html|timing_paths|power\.html|pin\.html|fs|bin|db)$' } | Select-Object -First 30)
    }
}

if ($AsJson) { $records | ConvertTo-Json -Depth 8; exit 0 }
if (-not $records) { Write-Output "No .gprj files found below $scanRoot"; exit 0 }

$safeFieldNames = '^(?i:TopModule|IncludePath|show_all_warnings|VCC|VCCX|VCCAUX|COMPRESS|CRC_CHECK|SECURITY_BIT|SSPI|MSPI|JTAG|Clock_Route_Order|Place_Option|Route_Option|Route_Maxfan|Global_Freq|Optimization_Goal|Synthesize_tool|CPU|Correct_Hold_Violation|POWER_ON_RESET_MONITOR|Unused_Pin)$'
foreach ($record in $records) {
    Write-Output "Project: $($record.ProjectFile)"
    Write-Output "  Root: $($record.ProjectRoot)"
    Write-Output "  Device: $($record.Device)"
    Write-Output "  Part number: $(if ($record.DevicePartNumber) { $record.DevicePartNumber } else { '<not declared>' }); identifier: $(if ($record.DeviceIdentifier) { $record.DeviceIdentifier } else { '<not declared>' })"
    if ($record.NestedProjects.Count) { Write-Output "  Separate nested projects: $($record.NestedProjects -join ', ')" }
    Write-Output "  Configured files: $($record.ProjectFiles.Count); registered results: $($record.RegisteredResults.Count)"
    Write-Output "  Synthesis: $(if ($record.SynthesisDirectory) { $record.SynthesisDirectory } else { '<missing>' })"
    Write-Output "  PnR: $(if ($record.PnrDirectory) { $record.PnrDirectory } else { '<missing>' })"
    foreach ($config in $record.ProcessConfigDetails) {
        Write-Output "  Process config: $($config.Path) [$($config.ParseStatus)]"
        $config.Fields | Where-Object { $_.Key -match $safeFieldNames } | ForEach-Object { Write-Output "    $($_.Key)=$($_.Value)" }
        if ($config.CaseVariantKeyGroups.Count) { Write-Output "    case-variant key groups: $($config.CaseVariantKeyGroups -join ', ')" }
    }
    Write-Output "  Encrypted HDL/IP files: $(if ($record.EncryptedFiles.Count) { $record.EncryptedFiles -join ', ' } else { '<none detected>' })"
    Write-Output "  Counts: $($record.FileCounts | ConvertTo-Json -Compress)"
}
