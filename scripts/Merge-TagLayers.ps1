param(
    [Parameter(Mandatory = $true)]
    [ValidateSet('VDT', 'VBT')]
    [string]$Product,

    [Parameter(Mandatory = $true)]
    [string]$BasePath,

    [Parameter(Mandatory = $true)]
    [string]$EditPath,

    [Parameter(Mandatory = $true)]
    [string]$CuratedPath,

    [Parameter(Mandatory = $true)]
    [string]$OutputPath
)

Set-StrictMode -Version Latest
$ErrorActionPreference = 'Stop'

function Read-IniLayer {
    param(
        [string]$Path,
        [ValidateSet('Shared', 'Curated')]
        [string]$Role,
        [ValidateSet('VDT', 'VBT')]
        [string]$ProductName
    )

    if (-not (Test-Path -LiteralPath $Path -PathType Leaf)) {
        throw "Required INI layer was not found: $Path"
    }

    $resolvedPath = (Resolve-Path -LiteralPath $Path).Path
    $content = [System.IO.File]::ReadAllText($resolvedPath)
    $seenPlugins = [System.Collections.Generic.HashSet[string]]::new(
        [System.StringComparer]::OrdinalIgnoreCase)
    $activeKind = ''
    $activePlugin = ''
    $activeHasTag = $false
    $activeProperties = [System.Collections.Generic.HashSet[string]]::new(
        [System.StringComparer]::OrdinalIgnoreCase)
    $pluginCount = 0
    $lineNumber = 0

    $allowedPluginKeys = if ($ProductName -eq 'VDT') {
        @('Tag', 'IncludeForms', 'ExcludeForms')
    } else {
        @('Tag', 'IncludeForms', 'ExcludeForms', 'SkillTags', 'ModNameTags')
    }
    $allowedGeneralKeys = if ($ProductName -eq 'VDT') {
        @('Enabled', 'GlobalPluginNameFallback')
    } else {
        @('Enabled', 'SkillTags', 'ModNameTags', 'GlobalPluginNameFallback')
    }

    foreach ($rawLine in [System.IO.File]::ReadLines($resolvedPath)) {
        ++$lineNumber
        $line = $rawLine.Trim()
        if ($line.Length -eq 0 -or $line.StartsWith(';') -or $line.StartsWith('#')) {
            continue
        }

        if ($line.StartsWith('[')) {
            if ($activeKind -eq 'Plugin' -and -not $activeHasTag) {
                throw "Plugin section [$activePlugin] has no Tag in $resolvedPath"
            }
            if ($line -notmatch '^\[([^]]+)\]$') {
                throw "Malformed section header at $($resolvedPath):$lineNumber"
            }

            $sectionName = $Matches[1].Trim()
            $activeProperties = [System.Collections.Generic.HashSet[string]]::new(
                [System.StringComparer]::OrdinalIgnoreCase)
            $activeHasTag = $false
            $activePlugin = ''

            if ($sectionName -match '^(?i:Plugin):(.*)$') {
                $pluginName = $Matches[1].Trim()
                if ($pluginName.Length -eq 0) {
                    throw "Empty plugin name at $($resolvedPath):$lineNumber"
                }
                if ($pluginName -notmatch '(?i)\.(esp|esm|esl)$') {
                    throw "Unsupported plugin filename [$pluginName] at $($resolvedPath):$lineNumber"
                }
                if (-not $seenPlugins.Add($pluginName)) {
                    throw "Duplicate plugin section [$pluginName] in $resolvedPath"
                }
                $activeKind = 'Plugin'
                $activePlugin = $pluginName
                ++$pluginCount
            } elseif ($sectionName -ieq 'General' -and $Role -eq 'Curated') {
                $activeKind = 'General'
            } else {
                throw "Unsupported section [$sectionName] at $($resolvedPath):$lineNumber"
            }
            continue
        }

        if ($activeKind.Length -eq 0) {
            throw "Property outside a section at $($resolvedPath):$lineNumber"
        }

        $separator = $line.IndexOf('=')
        if ($separator -lt 1) {
            throw "Malformed property at $($resolvedPath):$lineNumber"
        }

        $key = $line.Substring(0, $separator).Trim()
        $value = $line.Substring($separator + 1).Trim()
        if (-not $activeProperties.Add($key)) {
            throw "Duplicate property $key at $($resolvedPath):$lineNumber"
        }

        if ($activeKind -eq 'Plugin') {
            if ($Role -eq 'Shared' -and $key -ine 'Tag') {
                throw "Shared layers may contain only Tag properties: $($resolvedPath):$lineNumber"
            }
            if ($Role -eq 'Curated' -and $allowedPluginKeys -inotcontains $key) {
                throw "Unsupported plugin property $key at $($resolvedPath):$lineNumber"
            }
            if ($key -ieq 'Tag') {
                if ($value.Length -eq 0) {
                    throw "Empty Tag for [$activePlugin] at $($resolvedPath):$lineNumber"
                }
                if ($Role -eq 'Shared' -and ($value.Contains('[') -or $value.Contains(']'))) {
                    throw "Shared Tag values must be unbracketed at $($resolvedPath):$lineNumber"
                }
                $activeHasTag = $true
            }
        } elseif ($allowedGeneralKeys -inotcontains $key) {
            throw "Unsupported General property $key at $($resolvedPath):$lineNumber"
        }
    }

    if ($activeKind -eq 'Plugin' -and -not $activeHasTag) {
        throw "Plugin section [$activePlugin] has no Tag in $resolvedPath"
    }

    [PSCustomObject]@{
        Content = $content
        PluginCount = $pluginCount
    }
}

$base = Read-IniLayer -Path $BasePath -Role Shared -ProductName $Product
$edit = Read-IniLayer -Path $EditPath -Role Shared -ProductName $Product
$curated = Read-IniLayer -Path $CuratedPath -Role Curated -ProductName $Product

$outputDirectory = Split-Path -Parent $OutputPath
if ($outputDirectory.Length -gt 0) {
    New-Item -ItemType Directory -Force -Path $outputDirectory | Out-Null
}

$segments = @($base.Content, $edit.Content, $curated.Content) |
    ForEach-Object { $_.TrimEnd([char[]]@(13, 10)) } |
    Where-Object { $_.Length -gt 0 }
$separator = [Environment]::NewLine + [Environment]::NewLine
$merged = ($segments -join $separator) + [Environment]::NewLine
$utf8WithoutBom = [System.Text.UTF8Encoding]::new($false)
[System.IO.File]::WriteAllText($OutputPath, $merged, $utf8WithoutBom)

Write-Output (
    "Merged {0} Base, {1} EditHere, and {2} Curated plugin sections into {3}" -f
    $base.PluginCount, $edit.PluginCount, $curated.PluginCount, $OutputPath)

