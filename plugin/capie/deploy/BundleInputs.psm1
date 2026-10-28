<#
.SYNOPSIS
    Reads and checks plugin/capie/deploy/bundle-inputs.json, the one list of reviewed CAPIE binaries.

.DESCRIPTION
    build_launcher.ps1 (launcher bundle and launcher_manifest.generated.hpp) and
    build_proxy.ps1 (the DLL hashes compiled into version.dll) both take their inputs from
    bundle-inputs.json through this module, so the launcher and the proxy cannot pin
    different CAPIE builds any more. The format is documented in DEPLOYMENT.md. The
    release and bootstrap tools read the same file with their own reader.

    The checks here only read files; nothing is built, copied or started.
#>
Set-StrictMode -Version Latest

# --- Constants ---------------------------------------------------------------
$script:BiFileName = 'bundle-inputs.json'
# Bundle order the launcher's BundleManifest and the proxy's dll_hashes expect.
$script:BiExpectedNames = @('capie_core.dll', 'capie_editor.dll', 'capie_injector.exe')
$script:BiShaPattern = '^[0-9a-f]{64}$'
$script:BiHeaderShaPattern = '"([0-9a-f]{64})"'
$script:BiTableBegin = '<!-- bundle-inputs:begin -->'
$script:BiTableEnd = '<!-- bundle-inputs:end -->'
$script:BiTableRowPattern = '^\|\s*([A-Za-z0-9_.]+)\s*\|\s*([0-9A-Fa-f]{64})\s*\|'

function Get-CapieBundleInputPath {
    <#
    .SYNOPSIS
        Path of bundle-inputs.json next to this module.
    .OUTPUTS
        System.String
    .EXAMPLE
        Get-CapieBundleInputPath
    #>
    [CmdletBinding()]
    [OutputType([string])]
    param()
    return (Join-Path $PSScriptRoot $script:BiFileName)
}

function Read-CapieBundleInput {
    <#
    .SYNOPSIS
        Reads and validates bundle-inputs.json.
    .DESCRIPTION
        The file is a JSON array of objects {name, source, sha256, dll}: name is the file
        name in runtime\, source a repository-relative path with forward slashes, sha256 64
        lower-case hex characters, dll a boolean. The entries must be exactly
        capie_core.dll, capie_editor.dll, capie_injector.exe in this order, the two DLLs
        with dll = true. Anything else throws.
    .PARAMETER Path
        File to read; default is the file next to this module.
    .OUTPUTS
        System.Management.Automation.PSCustomObject[] (name, source, sha256, dll)
    .EXAMPLE
        $inputs = Read-CapieBundleInput
    #>
    [CmdletBinding()]
    [OutputType([psobject[]])]
    param([string]$Path = (Get-CapieBundleInputPath))
    $entries = @(Get-Content -LiteralPath $Path -Raw -Encoding utf8 | ConvertFrom-Json)
    if ($entries.Count -ne $script:BiExpectedNames.Count) { throw "${Path}: expected $($script:BiExpectedNames.Count) entries." }
    $result = for ($index = 0; $index -lt $entries.Count; ++$index) {
        $entry = $entries[$index]
        $names = @($entry.PSObject.Properties.Name | Sort-Object)
        if (($names -join ',') -ne 'dll,name,sha256,source') { throw "${Path}: entry $index must have exactly name, source, sha256, dll." }
        if ($entry.name -cne $script:BiExpectedNames[$index]) { throw "${Path}: entry $index must be $($script:BiExpectedNames[$index])." }
        if ($entry.sha256 -cnotmatch $script:BiShaPattern) { throw "${Path}: $($entry.name) sha256 must be 64 lower-case hex characters." }
        if ($entry.dll -isnot [bool] -or $entry.dll -ne $entry.name.EndsWith('.dll')) { throw "${Path}: $($entry.name) has a wrong dll flag." }
        if ($entry.source -isnot [string] -or $entry.source -match '\\|^/|^[A-Za-z]:|(^|/)\.\.(/|$)' -or
            -not $entry.source.EndsWith('/' + $entry.name)) {
            throw "${Path}: $($entry.name) source must be a repository-relative path with / that ends in the file name."
        }
        [pscustomobject]@{ name = [string]$entry.name; source = [string]$entry.source; sha256 = [string]$entry.sha256; dll = [bool]$entry.dll }
    }
    return @($result)
}

function Get-CapieHeaderHash {
    <#
    .SYNOPSIS
        Every quoted lower-case SHA-256 in a generated header, in order.
    .PARAMETER Path
        launcher_manifest.generated.hpp or proxy_manifest.generated.hpp.
    .OUTPUTS
        System.String[]
    .EXAMPLE
        Get-CapieHeaderHash -Path $header
    #>
    [CmdletBinding()]
    [OutputType([string[]])]
    param([Parameter(Mandatory)][string]$Path)
    $text = Get-Content -LiteralPath $Path -Raw -Encoding utf8
    return @([regex]::Matches($text, $script:BiHeaderShaPattern) | ForEach-Object { $_.Groups[1].Value })
}

function Assert-CapieHashList {
    <#
    .SYNOPSIS
        Throws when two hash lists differ (order and value).
    .PARAMETER Actual
        Hashes found.
    .PARAMETER Expected
        Hashes required.
    .PARAMETER Message
        Error text.
    .OUTPUTS
        None.
    .EXAMPLE
        Assert-CapieHashList -Actual $a -Expected $b -Message 'proxy pins differ'
    #>
    [CmdletBinding()]
    [OutputType([void])]
    param(
        [AllowEmptyCollection()][string[]]$Actual,
        [AllowEmptyCollection()][string[]]$Expected,
        [Parameter(Mandatory)][string]$Message
    )
    if ((@($Actual) -join ',') -cne (@($Expected) -join ',')) {
        throw "$Message (found: $(@($Actual) -join ', '); expected: $(@($Expected) -join ', '))"
    }
}

function Assert-CapieDeploymentTable {
    <#
    .SYNOPSIS
        Throws when the hash table in DEPLOYMENT.md does not mirror bundle-inputs.json.
    .DESCRIPTION
        Reads the Markdown table rows between the bundle-inputs:begin/end markers; they must
        list the same names and hashes (any case) in the same order.
    .PARAMETER Inputs
        Read-CapieBundleInput output.
    .PARAMETER Path
        DEPLOYMENT.md; default is the file next to this module.
    .OUTPUTS
        None.
    .EXAMPLE
        Assert-CapieDeploymentTable -Inputs $inputs
    #>
    [CmdletBinding()]
    [OutputType([void])]
    param(
        [Parameter(Mandatory)][psobject[]]$Inputs,
        [string]$Path = (Join-Path $PSScriptRoot 'DEPLOYMENT.md')
    )
    $text = Get-Content -LiteralPath $Path -Raw -Encoding utf8
    $begin = $text.IndexOf($script:BiTableBegin, [StringComparison]::Ordinal)
    $end = $text.IndexOf($script:BiTableEnd, [StringComparison]::Ordinal)
    if ($begin -lt 0 -or $end -lt $begin) { throw "${Path}: bundle-inputs table markers missing." }
    $rows = foreach ($line in ($text.Substring($begin, $end - $begin) -split "`r?`n")) {
        if ($line -match $script:BiTableRowPattern) { $Matches[1] + '=' + $Matches[2].ToLowerInvariant() }
    }
    Assert-CapieHashList -Actual @($rows) -Expected @($Inputs | ForEach-Object { $_.name + '=' + $_.sha256 }) `
        -Message "${Path}: the bundle-inputs table differs from bundle-inputs.json"
}

function Assert-CapieProxyMatchesBundle {
    <#
    .SYNOPSIS
        Throws when the proxy's compiled DLL hashes are not the bundle's DLL entries.
    .PARAMETER ProxyHashes
        Hashes in proxy_manifest.generated.hpp, in order.
    .PARAMETER Inputs
        Read-CapieBundleInput output.
    .OUTPUTS
        None.
    .EXAMPLE
        Assert-CapieProxyMatchesBundle -ProxyHashes (Get-CapieHeaderHash $h) -Inputs $inputs
    #>
    [CmdletBinding()]
    [OutputType([void])]
    param(
        [AllowEmptyCollection()][string[]]$ProxyHashes,
        [Parameter(Mandatory)][psobject[]]$Inputs
    )
    Assert-CapieHashList -Actual $ProxyHashes -Expected @($Inputs | Where-Object { $_.dll } | ForEach-Object { $_.sha256 }) `
        -Message 'The proxy DLL pins differ from the launcher bundle (bundle-inputs.json)'
}

function Assert-CapieLauncherMatchesBundle {
    <#
    .SYNOPSIS
        Throws when launcher_manifest.generated.hpp does not pin exactly the bundle entries.
    .PARAMETER LauncherHashes
        Hashes in launcher_manifest.generated.hpp, in order.
    .PARAMETER Inputs
        Read-CapieBundleInput output.
    .OUTPUTS
        None.
    .EXAMPLE
        Assert-CapieLauncherMatchesBundle -LauncherHashes (Get-CapieHeaderHash $h) -Inputs $inputs
    #>
    [CmdletBinding()]
    [OutputType([void])]
    param(
        [AllowEmptyCollection()][string[]]$LauncherHashes,
        [Parameter(Mandatory)][psobject[]]$Inputs
    )
    Assert-CapieHashList -Actual $LauncherHashes -Expected @($Inputs | ForEach-Object { $_.sha256 }) `
        -Message 'The launcher manifest differs from bundle-inputs.json'
}

function Test-CapieOtherBuild {
    <#
    .SYNOPSIS
        Compares the other build's generated header (launcher or proxy) with bundle-inputs.json.
    .DESCRIPTION
        Returns a warning text when that header exists and pins other hashes (a stale build
        output: rebuild it), or empty when it matches or does not exist. It does not throw:
        the build that runs now is checked strictly against the JSON, and two outputs built
        from the same JSON cannot differ.
    .PARAMETER HeaderPath
        Generated header of the other build.
    .PARAMETER Inputs
        Read-CapieBundleInput output.
    .PARAMETER Proxy
        The header is the proxy's (only DLL entries).
    .OUTPUTS
        System.String
    .EXAMPLE
        Test-CapieOtherBuild -HeaderPath $proxyHeader -Inputs $inputs -Proxy
    #>
    [CmdletBinding()]
    [OutputType([string])]
    param(
        [Parameter(Mandatory)][string]$HeaderPath,
        [Parameter(Mandatory)][psobject[]]$Inputs,
        [switch]$Proxy
    )
    if (-not (Test-Path -LiteralPath $HeaderPath)) { return '' }
    try {
        $hashes = Get-CapieHeaderHash -Path $HeaderPath
        if ($Proxy) { Assert-CapieProxyMatchesBundle -ProxyHashes $hashes -Inputs $Inputs }
        else { Assert-CapieLauncherMatchesBundle -LauncherHashes $hashes -Inputs $Inputs }
        return ''
    } catch {
        return "stale build output, rebuild it: $HeaderPath ($($_.Exception.Message))"
    }
}

Export-ModuleMember -Function @(
    'Get-CapieBundleInputPath', 'Read-CapieBundleInput', 'Get-CapieHeaderHash', 'Assert-CapieHashList',
    'Assert-CapieDeploymentTable', 'Assert-CapieProxyMatchesBundle', 'Assert-CapieLauncherMatchesBundle',
    'Test-CapieOtherBuild'
)
