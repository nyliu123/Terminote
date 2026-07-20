param(
    [Parameter(Mandatory = $true)]
    [string]$FontDirectory,

    [Parameter(Mandatory = $true)]
    [string]$Output
)

$workspaceRoot = [IO.Path]::GetFullPath((Join-Path $PSScriptRoot ".."))
$fontPath = [IO.Path]::GetFullPath((Join-Path $workspaceRoot $FontDirectory))
$outputPath = [IO.Path]::GetFullPath((Join-Path $workspaceRoot $Output))
$buildPath = [IO.Path]::GetFullPath((Join-Path $workspaceRoot "build"))
$encoding = New-Object Text.UTF8Encoding($false)

if (-not $fontPath.StartsWith($workspaceRoot) -or
    -not $outputPath.StartsWith($buildPath)) {
    throw "Font generator path is outside the expected workspace directories."
}

$fontFiles = @(Get-ChildItem -LiteralPath $fontPath -Filter *.inc -File |
    Sort-Object Name)
if ($fontFiles.Count -eq 0) {
    throw "No font files found in $fontPath."
}

$outputLines = New-Object System.Collections.Generic.List[string]
$outputLines.Add("static const Glyph FONT[] = {")

foreach ($fontFile in $fontFiles) {
    if ($fontFile.BaseName -cnotmatch '^[A-Z]$') {
        throw "Font file name must be one uppercase letter: $($fontFile.Name)"
    }

    $patternRows = @(Get-Content -LiteralPath $fontFile.FullName -Encoding UTF8)
    if ($patternRows.Count -ne 6) {
        throw "Font file must contain exactly six pattern rows: $($fontFile.Name) " +
              "(rows=$($patternRows.Count))"
    }

    $outputLines.Add("    {'$($fontFile.BaseName)', {")
    for ($row = 0; $row -lt $patternRows.Count; ++$row) {
        $escaped = $patternRows[$row].Replace('\', '\\').Replace('"', '\"')
        $comma = if ($row -lt $patternRows.Count - 1) { ',' } else { '' }
        $outputLines.Add("        `"$escaped`"$comma")
    }
    $outputLines.Add("    }},")
}

$outputLines.Add("};")
$outputLines.Add("")
$outputLines.Add("static const size_t FONT_COUNT = sizeof(FONT) / sizeof(FONT[0]);")
$outputLines.Add("")
$content = $outputLines -join "`r`n"

if ((Test-Path -LiteralPath $outputPath) -and
    [IO.File]::ReadAllText($outputPath) -ceq $content) {
    exit 0
}

[IO.Directory]::CreateDirectory([IO.Path]::GetDirectoryName($outputPath)) | Out-Null
[IO.File]::WriteAllText($outputPath, $content, $encoding)
