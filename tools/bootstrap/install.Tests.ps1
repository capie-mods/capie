<#
.SYNOPSIS
    Offline Pester tests for tools/bootstrap/install.ps1 (the generated single file).

.DESCRIPTION
    Pester 3.4 syntax (the version bundled with Windows); also runs on PowerShell 7.
        Invoke-Pester -Script .\tools\bootstrap\install.Tests.ps1
    Work directory: $env:RA_BOOTSTRAP_TEST_ROOT (default: %TEMP%\ra-bootstrap-tests).
    Optional: $env:RA_BOOTSTRAP_REAL_LOCALCONFIG = path to a COPY of a real localconfig.vdf.
    Nothing outside the work directory is written; Steam/game are never touched. Every
    adapter that reaches outside the process (network, Steam process, prompts, desktop,
    folder dialog) is replaced in Reset-Env.
#>

$here = Split-Path -Parent $MyInvocation.MyCommand.Path
$scriptPath = Join-Path $here 'install.ps1'
$buildScript = Join-Path $here 'build-install.ps1'
$sourceDir = Join-Path $here 'src'
. $scriptPath
# Unit tests call the functions directly, so enable the same strict mode Invoke-RaMain uses.
Set-StrictMode -Version Latest

$testRoot = if ($env:RA_BOOTSTRAP_TEST_ROOT) { $env:RA_BOOTSTRAP_TEST_ROOT } else { Join-Path ([IO.Path]::GetTempPath()) 'ra-bootstrap-tests' }
$script:RaQuiet = $true
$origRepo = $script:RaGitHubRepo
$origKey = $script:RaUpdatePublicKeyXml
$origPinned = $script:RaPinnedRelease
$origAdapters = @{
    ReadAllBytes = $script:RaReadAllBytes; WriteAllBytes = $script:RaWriteAllBytes; CopyFile = $script:RaCopyFile
    ReadShortcut = $script:RaReadShortcut; WriteShortcut = $script:RaWriteShortcut; Now = $script:RaNow
    GetRegistryValue = $script:RaGetRegistryValue; WriteHost = $script:RaWriteHost
}
$signScript = Join-Path $here 'sign-release-manifest.ps1'
$L = 'C:\Users\Test User\AppData\Local\RenameAnything\0.1.0\RenameAnythingLauncher.exe'
$LOld = 'C:\Users\Test User\AppData\Local\RenameAnything\0.0.9\RenameAnythingLauncher.exe'
$utf8 = New-Object System.Text.UTF8Encoding($false)
$fakeGitHubAssetHost = 'https://github.com/test-owner/test-repo/releases/download'
$fakeGitHubApiUrl = 'https://api.github.com/repos/test-owner/test-repo/releases/latest'

function Assert-RaThrow {
    <#
    .SYNOPSIS
        Asserts that a script block throws, optionally with a message fragment.
    .DESCRIPTION
        Pester 3.4 "Should Throw" does not work on PowerShell 7, so the tests use this.
    .PARAMETER Block
        Code expected to throw.
    .PARAMETER MessagePart
        Optional text the exception message must contain.
    .OUTPUTS
        None. Fails the current test through Should.
    .EXAMPLE
        Assert-RaThrow { Add-RaWrapper '%command% %command%' $L } 'more than once'
    #>
    [CmdletBinding()]
    [OutputType([void])]
    param(
        [Parameter(Mandatory, Position = 0)][scriptblock]$Block,
        [Parameter(Position = 1)][string]$MessagePart = ''
    )
    $threw = $false
    $msg = ''
    try { & $Block } catch {
        $threw = $true
        $msg = $_.Exception.Message
    }
    $threw | Should Be $true
    if ($MessagePart) { $msg.Contains($MessagePart) | Should Be $true }
}

function New-LocalConfigText {
    <#
    .SYNOPSIS
        Builds a synthetic localconfig.vdf with decoy blocks.
    .DESCRIPTION
        Contains another game's LaunchOptions, an escaped JSON value and the WebStorage
        and depots decoys that also mention 394360. The HOI4 app block is inserted
        where -AppBlock says (or omitted with -NoApp).
    .PARAMETER AppBlock
        Text of the 394360 block (see New-AppBlock).
    .PARAMETER Nl
        Line ending to use.
    .PARAMETER NoApp
        Leave the 394360 block out.
    .OUTPUTS
        System.String
    .EXAMPLE
        New-LocalConfigText (New-AppBlock '"LaunchOptions"		"-debug_mode"')
    #>
    [CmdletBinding()]
    [OutputType([string])]
    param(
        [Parameter(Position = 0)][AllowEmptyString()][string]$AppBlock = '',
        [ValidateSet("`n", "`r`n")][string]$Nl = "`n",
        [switch]$NoApp
    )
    $app = if ($NoApp) { '' } else { $AppBlock }
    $t = @"
"UserLocalConfigStore"
{
	"Broadcast"
	{
		"Permissions"		"0"
	}
	"Software"
	{
		"Valve"
		{
			"Steam"
			{
				"apps"
				{
					"394310"
					{
						"LastPlayed"		"1711824261"
						"LaunchOptions"		"-other_game"
					}
$app					"8930"
					{
						"Playtime"		"5"
					}
				}
				"SomeJson"		"{\"a\":\"b\\\\c\"}"
			}
		}
	}
	"WebStorage"
	{
		"apps"
		{
			"394360"
			{
				"DefaultLaunchOption"
				{
				}
			}
		}
	}
	"depots"
	{
		"394360"
		{
			"LaunchOptions"		"decoy"
		}
	}
}
"@
    $t = $t -replace "`r`n", "`n"
    if ($Nl -ne "`n") { $t = $t -replace "`n", $Nl }
    return $t
}

function New-AppBlock {
    <#
    .SYNOPSIS
        Builds the "394360" app block for New-LocalConfigText.
    .DESCRIPTION
        Includes LastPlayed and a nested cloud block; the given line (usually the
        LaunchOptions key) is added before the closing brace.
    .PARAMETER LaunchOptionsLine
        Extra line inside the block (empty for none).
    .OUTPUTS
        System.String
    .EXAMPLE
        New-AppBlock '"LaunchOptions"		"-debug_mode"'
    #>
    [CmdletBinding()]
    [OutputType([string])]
    param([Parameter(Position = 0)][AllowEmptyString()][AllowNull()][string]$LaunchOptionsLine)
    $lo = if ($null -ne $LaunchOptionsLine) { "`t`t`t`t`t`t$LaunchOptionsLine`n" } else { '' }
    $t5 = "`t" * 5
    $t6 = "`t" * 6
    $head = "$t5`"394360`"`n$t5{`n$t6`"LastPlayed`"`t`t`"1791737197`"`n$t6`"cloud`"`n$t6{`n"
    return $head + "$t6`t`"last_sync_state`"`t`t`"synchronized`"`n$t6}`n$lo$t5}`n"
}

function Write-Utf8 {
    <#
    .SYNOPSIS
        Writes text as UTF-8 without BOM.
    .DESCRIPTION
        Fixture helper.
    .PARAMETER Path
        Target file.
    .PARAMETER Text
        Content.
    .OUTPUTS
        None.
    .EXAMPLE
        Write-Utf8 $p 'text'
    #>
    [CmdletBinding()]
    [OutputType([void])]
    param(
        [Parameter(Mandatory, Position = 0)][string]$Path,
        [Parameter(Mandatory, Position = 1)][AllowEmptyString()][string]$Text
    )
    [IO.File]::WriteAllText($Path, $Text, $utf8)
}

function New-FakeSteam {
    <#
    .SYNOPSIS
        Creates a synthetic Steam folder tree.
    .DESCRIPTION
        userdata\<id>\config\localconfig.vdf per account, loginusers.vdf with persona
        names, libraryfolders.vdf pointing at itself, appmanifest_394360.acf and a game
        folder with hoi4.exe (a copy of notepad.exe) and a dummy dowser.exe.
    .PARAMETER Root
        Steam folder to create.
    .PARAMETER Accounts
        Account id -> localconfig.vdf text.
    .OUTPUTS
        None.
    .EXAMPLE
        New-FakeSteam $steam @{ '111' = (New-LocalConfigText (New-AppBlock $null)) }
    #>
    [CmdletBinding()]
    [OutputType([void])]
    param(
        [Parameter(Mandatory, Position = 0)][string]$Root,
        [Parameter(Mandatory, Position = 1)][hashtable]$Accounts
    )
    New-Item -ItemType Directory -Force -Path (Join-Path $Root 'config'), (Join-Path $Root 'steamapps\common\Hearts of Iron IV') | Out-Null
    $users = ''
    foreach ($id in $Accounts.Keys) {
        $cfgDir = Join-Path $Root "userdata\$id\config"
        New-Item -ItemType Directory -Force -Path $cfgDir | Out-Null
        Write-Utf8 (Join-Path $cfgDir 'localconfig.vdf') $Accounts[$id]
        $sid = [uint64]$id + [uint64]76561197960265728
        $users += "`t`"$sid`"`n`t{`n`t`t`"AccountName`"`t`t`"acc$id`"`n`t`t`"PersonaName`"`t`t`"Persona $id`"`n`t}`n"
    }
    Write-Utf8 (Join-Path $Root 'config\loginusers.vdf') "`"users`"`n{`n$users}`n"
    $esc = $Root.Replace('\', '\\')
    $libraries = "`"libraryfolders`"`n{`n`t`"0`"`n`t{`n`t`t`"path`"`t`t`"$esc`"`n`t`t`"apps`"`n`t`t{`n`t`t`t`"394360`"`t`t`"1`"`n`t`t}`n`t}`n}`n"
    Write-Utf8 (Join-Path $Root 'steamapps\libraryfolders.vdf') $libraries
    Write-Utf8 (Join-Path $Root 'steamapps\appmanifest_394360.acf') "`"AppState`"`n{`n`t`"appid`"`t`t`"394360`"`n`t`"installdir`"`t`t`"Hearts of Iron IV`"`n}`n"
    $game = Join-Path $Root 'steamapps\common\Hearts of Iron IV'
    Copy-Item -LiteralPath (Join-Path $env:WINDIR 'System32\notepad.exe') -Destination (Join-Path $game 'hoi4.exe')
    Write-Utf8 (Join-Path $game 'dowser.exe') 'dummy'
}

function New-TestPackage {
    <#
    .SYNOPSIS
        Builds a small release ZIP plus a manifest with real hashes.
    .DESCRIPTION
        $Tamper: none | filehash (one file's bytes differ) | extra (unlisted entry) |
        zipslip (../ entry) | missing (one listed file absent).
    .PARAMETER Dir
        Folder for the ZIP.
    .PARAMETER Tamper
        Kind of damage to apply.
    .PARAMETER Version
        Manifest version.
    .PARAMETER WithProxy
        Include a proxy asset with distinct bytes for each release.
    .PARAMETER WithMod
        Include the "CAPIE" switch mod under mod/ (descriptor text carries the version).
    .OUTPUTS
        System.Collections.Hashtable (Zip, Manifest)
    .EXAMPLE
        New-TestPackage $dir -Tamper 'extra'
    #>
    [CmdletBinding()]
    [OutputType([hashtable])]
    param(
        [Parameter(Mandatory, Position = 0)][string]$Dir,
        [Parameter(Position = 1)][ValidateSet('none', 'filehash', 'extra', 'zipslip', 'missing')][string]$Tamper = 'none',
        [string]$Version = '0.1.0',
        [switch]$WithProxy,
        [switch]$WithMod
    )
    $src = Join-Path $Dir 'pkgsrc'
    New-Item -ItemType Directory -Force -Path (Join-Path $src 'runtime') | Out-Null
    $files = [ordered]@{
        'RenameAnythingLauncher.exe'          = 'launcher-bytes'
        'LICENSE.MinHook.txt'                 = 'license'
        'runtime/rename_anything_country.dll' = 'dll-bytes'
    }
    if ($WithProxy) { $files['proxy/version.dll'] = 'proxy-' + $Version }
    if ($WithMod) {
        $files['mod/capie.mod'] = "name=`"CAPIE`"`npath=`"mod/capie`"`n"
        $files['mod/capie/descriptor.mod'] = "name=`"CAPIE`"`nversion=`"$Version`"`n"
        $files['mod/capie/interface/frontendmainview.gui'] = 'guiTypes = { }'
    }
    $manifest = [ordered]@{ Version = $Version; ZipName = "RA-$Version-$Tamper.zip"; ZipSha256 = ''; Files = [ordered]@{}; SupportedGameSha256 = @() }
    foreach ($k in $files.Keys) {
        $bytes = $utf8.GetBytes($files[$k])
        $manifest.Files[$k] = Get-RaSha256OfData $bytes
    }
    $zip = Join-Path $Dir "RA-$Version-$Tamper.zip"
    if (Test-Path $zip) { Remove-Item $zip -Force }
    Add-Type -AssemblyName System.IO.Compression -ErrorAction SilentlyContinue
    Add-Type -AssemblyName System.IO.Compression.FileSystem
    $z = [IO.Compression.ZipFile]::Open($zip, 'Create')
    try {
        foreach ($k in $files.Keys) {
            if ($Tamper -eq 'missing' -and $k -eq 'LICENSE.MinHook.txt') { continue }
            $content = if ($Tamper -eq 'filehash' -and $k -eq 'runtime/rename_anything_country.dll') { 'EVIL' } else { $files[$k] }
            $e = $z.CreateEntry($k)
            $s = $e.Open()
            $b = $utf8.GetBytes($content)
            $s.Write($b, 0, $b.Length)
            $s.Dispose()
        }
        foreach ($extra in @(@{ Kind = 'extra'; Name = 'runtime/extra.dll' }, @{ Kind = 'zipslip'; Name = '../evil.txt' })) {
            if ($Tamper -ne $extra.Kind) { continue }
            $s = $z.CreateEntry($extra.Name).Open()
            $s.WriteByte(1)
            $s.Dispose()
        }
    } finally { $z.Dispose() }
    $manifest.ZipSha256 = (Get-FileHash -LiteralPath $zip -Algorithm SHA256).Hash
    return @{ Zip = $zip; Manifest = $manifest }
}

function Reset-Env {
    <#
    .SYNOPSIS
        Gives a test a fresh work folder and safe adapters.
    .DESCRIPTION
        Resets every runtime switch and adapter: no Steam process, a temporary Desktop,
        prompts that fail the test, no folder dialog, and a network mock that records
        every URL and fails on anything not registered in $script:fakeWeb.
    .PARAMETER Name
        Sub-folder name under the test root.
    .OUTPUTS
        System.String (the work folder)
    .EXAMPLE
        $dir = Reset-Env 'e2e'
    #>
    [CmdletBinding()]
    [OutputType([string])]
    param([Parameter(Mandatory, Position = 0)][string]$Name)
    $dir = Join-Path $testRoot $Name
    if (Test-Path $dir) { Remove-Item -LiteralPath $dir -Recurse -Force }
    New-Item -ItemType Directory -Force -Path $dir | Out-Null
    $script:RaRoot = Join-Path $dir 'LocalAppData\RenameAnything'
    $script:RaDry = $false
    $script:RaAssumeYes = $true
    $script:RaQuiet = $true
    $script:RaAfterWriteHook = $null
    $script:RaBeforeOwnedRemove = $null
    $script:RaGetSteamProcess = { @() }
    $desk = Join-Path $dir 'Desktop'
    New-Item -ItemType Directory -Force -Path $desk | Out-Null
    $script:RaDesktopPath = [scriptblock]::Create("'$desk'")
    $docs = Join-Path $dir 'Documents'
    New-Item -ItemType Directory -Force -Path (Join-Path $docs 'Paradox Interactive\Hearts of Iron IV\mod') | Out-Null
    $script:RaDocumentsPath = [scriptblock]::Create("'$docs'")
    $script:RaReadHost = { param($p) throw "Unexpected prompt: $p" }
    $script:RaGitHubRepo = $origRepo
    $script:RaUpdatePublicKeyXml = $origKey
    $script:RaPinnedRelease = $origPinned
    $script:RaFolderPicker = $null
    $script:RaReadAllBytes = $origAdapters.ReadAllBytes
    $script:RaWriteAllBytes = $origAdapters.WriteAllBytes
    $script:RaCopyFile = $origAdapters.CopyFile
    $script:RaReadShortcut = $origAdapters.ReadShortcut
    $script:RaWriteShortcut = $origAdapters.WriteShortcut
    $script:RaNow = $origAdapters.Now
    $script:RaGetRegistryValue = { param($p, $n) throw "no registry in tests: $p\$n" }
    $script:RaWriteHost = $origAdapters.WriteHost
    # Terminal interface: a redirected console by default (plain prompts), output captured.
    $script:RaGetTerminal = { [pscustomobject]@{ Interactive = $false; Vt = $false; Width = 80 } }
    $script:RaGetEnv = { $null }
    $script:rawOut = New-Object System.Text.StringBuilder
    $script:RaWriteRaw = { param($t) [void]$script:rawOut.Append($t) }
    $script:RaReadKey = { throw 'Unexpected key read' }
    $script:sleeps = New-Object System.Collections.ArrayList
    $script:RaSleep = { param($ms) [void]$script:sleeps.Add($ms) }
    $script:RaLogSink = $null
    $script:RaMpWarningShown = $false
    $script:requested = New-Object System.Collections.ArrayList
    # Network is always mocked: any unexpected request fails the test.
    $script:fakeWeb = @{}
    $script:RaHttpGet = {
        param($u)
        [void]$script:requested.Add($u)
        if (-not $script:fakeWeb.ContainsKey($u)) { throw "no network in tests: $u" }
        , $script:fakeWeb[$u]
    }
    return $dir
}

function Get-TreeHash {
    <#
    .SYNOPSIS
        Lists "path=sha256" for every file under a folder (sorted).
    .DESCRIPTION
        Used to prove that an operation wrote nothing.
    .PARAMETER Dir
        Folder to scan; a missing folder yields an empty list.
    .OUTPUTS
        System.String[]
    .EXAMPLE
        (Get-TreeHash $dir) -join '|'
    #>
    [CmdletBinding()]
    [OutputType([string[]], [object[]])]
    param([Parameter(Mandatory, Position = 0)][string]$Dir)
    if (-not (Test-Path $Dir)) { return @() }
    $files = Get-ChildItem -LiteralPath $Dir -Recurse -File | Sort-Object FullName
    return @($files | ForEach-Object { $_.FullName + '=' + (Get-FileHash -LiteralPath $_.FullName).Hash })
}

Describe 'Script hygiene' {
    It 'is pure ASCII (safe for PS 5.1 and irm|iex decoding)' {
        $bytes = [IO.File]::ReadAllBytes($scriptPath)
        @($bytes | Where-Object { $_ -gt 127 }).Count | Should Be 0
    }
    It 'parses without errors' {
        $errors = $null
        [void][System.Management.Automation.Language.Parser]::ParseFile($scriptPath, [ref]$null, [ref]$errors)
        @($errors).Count | Should Be 0
    }
    It 'has English messages only, all ASCII, and every key used in the source exists' {
        (Get-RaText 'MpWarning') | Should Match 'NOT declared multiplayer-compatible'
        foreach ($key in $script:RaMessages.Keys) {
            @([Text.Encoding]::UTF8.GetBytes([string]$script:RaMessages[$key]) | Where-Object { $_ -gt 127 }).Count | Should Be 0
        }
        $source = [IO.File]::ReadAllText($scriptPath)
        $used = @([regex]::Matches($source, "Get-RaText '([A-Za-z]+)'") | ForEach-Object { $_.Groups[1].Value } | Sort-Object -Unique)
        @($used | Where-Object { -not $script:RaMessages.ContainsKey($_) }) -join ',' | Should Be ''
        $source.Contains('l_polish') -or $source.Contains('Get-UICulture') | Should Be $false
        Assert-RaThrow { Get-RaText 'NoSuchKey' } 'Unknown message key'
    }
    It 'has no BOM, because a BOM in the downloaded text would break irm | iex' {
        $bytes = [IO.File]::ReadAllBytes($scriptPath)
        ($bytes[0] -eq 0xEF) | Should Be $false
        Assert-RaThrow { & ([scriptblock]::Create([string][char]0xFEFF + 'Write-Output 1')) }
    }
    It 'Assert-RaThrow itself fails for a non-throwing block (negative control)' {
        $failed = $false
        try { Assert-RaThrow { 1 } } catch { $failed = $true }
        $failed | Should Be $true
    }
    It 'runs as a downloaded scriptblock (irm-style) in a child process: -Uninstall -DryRun writes nothing' {
        $dir = Reset-Env 'child-scriptblock'
        $steam = Join-Path $dir 'Steam'
        New-FakeSteam $steam @{ '111' = (New-LocalConfigText (New-AppBlock '"LaunchOptions"		"-debug_mode"')) }
        $before = Get-TreeHash $dir
        $exe = (Get-Process -Id $PID).Path
        $prefix = "`$env:LOCALAPPDATA = '$dir\LocalAppData'; `$t = [IO.File]::ReadAllText('$scriptPath'); "
        $cmd = $prefix + "& ([scriptblock]::Create(`$t)) -Uninstall -DryRun -Yes -SteamRoot '$steam'; 'CHILD-EXIT-OK'"
        $out = & $exe -NoProfile -NonInteractive -Command $cmd 2>&1 | Out-String
        $out | Should Match 'CHILD-EXIT-OK'
        $out | Should Match 'DRY RUN'
        (Get-TreeHash $dir) -join '|' | Should Be ($before -join '|')
    }
    It 'has a single placeholder release URL constant and refuses an unpinned manifest' {
        $origRepo | Should Match 'PLACEHOLDER'
        $origKey | Should Be 'TO-BE-GENERATED'
        Assert-RaThrow { Test-RaManifest $origPinned } 'not configured'
    }
}

Describe 'VDF parser' {
    It 'finds the HOI4 app only under Software/Valve/Steam/apps (ignores WebStorage/depots decoys)' {
        $info = Get-RaAppLaunchInfo (New-LocalConfigText (New-AppBlock '"LaunchOptions"		"-debug_mode"'))
        $info.Value | Should Be '-debug_mode'
        $info.App.Key | Should Be '394360'
    }
    It 'reports missing LaunchOptions as null' {
        (Get-RaAppLaunchInfo (New-LocalConfigText (New-AppBlock $null))).Value | Should Be $null
    }
    It 'unescapes values' {
        $root = ConvertFrom-RaVdf (New-LocalConfigText (New-AppBlock $null))
        $n = @(Find-RaVdfPath $root @('UserLocalConfigStore', 'Software', 'Valve', 'Steam', 'SomeJson'))
        $n[0].Value | Should Be '{"a":"b\\c"}'
    }
    It 'escape round trip' {
        $v = '"C:\a b\x.exe" %command% -x "q"'
        ConvertFrom-RaVdfString (ConvertTo-RaVdfString $v) | Should Be $v
    }
    It 'rejects unbalanced braces' {
        Assert-RaThrow { ConvertFrom-RaVdf ((New-LocalConfigText (New-AppBlock $null)) + "}`n") }
        Assert-RaThrow { ConvertFrom-RaVdf ((New-LocalConfigText (New-AppBlock $null)).TrimEnd().TrimEnd('}')) }
    }
    It 'rejects an unterminated string' {
        Assert-RaThrow { ConvertFrom-RaVdf "`"a`"`n{`n`t`"b`"`t`t`"oops`n}`n" }
    }
    It 'rejects a truncated (corrupted) file' {
        $t = New-LocalConfigText (New-AppBlock $null)
        Assert-RaThrow { Get-RaAppLaunchInfo $t.Substring(0, [int]($t.Length / 2)) }
    }
    It 'rejects a file without the apps block' {
        Assert-RaThrow { Get-RaAppLaunchInfo "`"UserLocalConfigStore`"`n{`n`t`"x`"`t`t`"1`"`n}`n" } 'No UserLocalConfigStore'
    }
    It 'rejects duplicate 394360 blocks' {
        $b = New-AppBlock $null
        Assert-RaThrow { Get-RaAppLaunchInfo (New-LocalConfigText ($b + $b)) } 'Multiple'
    }
    It 'rejects duplicate LaunchOptions keys' {
        $b = New-AppBlock ("`"LaunchOptions`"`t`t`"a`"`n`t`t`t`t`t`t`"LaunchOptions`"`t`t`"b`"")
        Assert-RaThrow { Get-RaAppLaunchInfo (New-LocalConfigText $b) } 'Multiple LaunchOptions'
    }
}

Describe 'Launch option composition' {
    It 'empty -> wrapper + %command%' { Add-RaWrapper '' $L | Should Be "`"$L`" %command%" }
    It 'missing key (null) -> wrapper + %command%' { Add-RaWrapper $null $L | Should Be "`"$L`" %command%" }
    It 'keeps plain flags after %command%' { Add-RaWrapper '-debug_mode -nolog' $L | Should Be "`"$L`" %command% -debug_mode -nolog" }
    It 'inserts before an existing %command%' { Add-RaWrapper '%command% -debug_mode' $L | Should Be "`"$L`" %command% -debug_mode" }
    It 'chains after another wrapper' { Add-RaWrapper '"C:\Tools\other.exe" %command% -x' $L | Should Be "`"C:\Tools\other.exe`" `"$L`" %command% -x" }
    It 'is idempotent' { $once = Add-RaWrapper '-debug_mode' $L; Add-RaWrapper $once $L | Should Be $once }
    It 'replaces an older version path' { Add-RaWrapper (Add-RaWrapper '-x' $LOld) $L | Should Be "`"$L`" %command% -x" }
    It 'refuses two %command%' { Assert-RaThrow { Add-RaWrapper '%command% %command%' $L } }
    It 'refuses a launcher path with quotes' { Assert-RaThrow { Add-RaWrapper '' 'C:\a"b\RenameAnythingLauncher.exe' } }
    It 'removes only our fragment (flags)' { Remove-RaWrapper (Add-RaWrapper '-debug_mode' $L) | Should Be '-debug_mode' }
    It 'removes to empty' { Remove-RaWrapper (Add-RaWrapper '' $L) | Should Be '' }
    It 'removes and keeps another wrapper' {
        Remove-RaWrapper (Add-RaWrapper '"C:\Tools\other.exe" %command% -x' $L) | Should Be '"C:\Tools\other.exe" %command% -x'
    }
    It 'detects an unquoted path variant' {
        Test-RaHasWrapper 'C:\RA\RenameAnythingLauncher.exe %command%' | Should Be $true
        Remove-RaWrapper 'C:\RA\RenameAnythingLauncher.exe %command% -x' | Should Be '-x'
    }
    It 'leaves foreign options unchanged' { Remove-RaWrapper '-debug_mode' | Should Be '-debug_mode' }
}

Describe 'Minimal text edit of localconfig.vdf' {
    It 'replaces only the LaunchOptions value span' {
        $t = New-LocalConfigText (New-AppBlock '"LaunchOptions"		"-debug_mode"')
        $r = Set-RaLaunchOptionsInText $t (Add-RaWrapper '-debug_mode' $L)
        $r.Action | Should Be 'ReplacedValue'
        $idx = $t.IndexOf('"-debug_mode"')
        $r.Text.Substring(0, $idx) | Should Be $t.Substring(0, $idx)
        $r.Text.EndsWith($t.Substring($idx + '"-debug_mode"'.Length)) | Should Be $true
        $expectedToken = '"\"C:\\Users\\Test User\\AppData\\Local\\RenameAnything\\0.1.0\\RenameAnythingLauncher.exe\" %command% -debug_mode"'
        $r.Text | Should Match ([regex]::Escape($expectedToken))
        (Get-RaAppLaunchInfo $r.Text).Value | Should Be (Add-RaWrapper '-debug_mode' $L)
        # the other game's options untouched
        $r.Text | Should Match '"LaunchOptions"\t\t"-other_game"'
    }
    It 'inserts the key when missing (indentation matches)' {
        $t = New-LocalConfigText (New-AppBlock $null)
        $r = Set-RaLaunchOptionsInText $t 'X'
        $r.Action | Should Be 'InsertedKey'
        $r.Text | Should Match "\n\t{6}`"LaunchOptions`"\t\t`"X`"\n\t{5}}\n"
        (Get-RaAppLaunchInfo $r.Text).Value | Should Be 'X'
        $r.Text.Replace("`t`t`t`t`t`t`"LaunchOptions`"`t`t`"X`"`n", '') | Should Be $t
    }
    It 'inserts a whole 394360 block when absent' {
        $t = New-LocalConfigText '' -NoApp
        $r = Set-RaLaunchOptionsInText $t 'X'
        $r.Action | Should Be 'InsertedBlock'
        (Get-RaAppLaunchInfo $r.Text).Value | Should Be 'X'
        (Get-RaAppLaunchInfo $r.Text).App.Key | Should Be '394360'
    }
    It 'preserves CRLF line endings' {
        $t = New-LocalConfigText (New-AppBlock $null) -Nl "`r`n"
        $r = Set-RaLaunchOptionsInText $t 'X' "`r`n"
        ($r.Text -split "`r`n").Count | Should Be (($t -split "`r`n").Count + 1)
        $r.Text -replace "`r`n", '' | Should Not Match "`n"
    }
}

Describe 'File update with backup, verification and restore' {
    It 'writes, verifies and keeps a byte-identical backup; BOM preserved' {
        $dir = Reset-Env 'file-ok'
        $p = Join-Path $dir 'localconfig.vdf'
        $orig = [byte[]](@(0xEF, 0xBB, 0xBF) + $utf8.GetBytes((New-LocalConfigText (New-AppBlock '"LaunchOptions"		"-debug_mode"'))))
        [IO.File]::WriteAllBytes($p, $orig)
        $r = Update-RaLocalConfig -ConfigPath $p -Compute { param($o) Add-RaWrapper $o $L }
        $r.Changed | Should Be $true
        Test-RaBytesEqual ([IO.File]::ReadAllBytes($r.Backup)) $orig | Should Be $true
        $after = [IO.File]::ReadAllBytes($p)
        $after[0] | Should Be 0xEF
        (Get-RaAppLaunchInfo (Read-RaTextFile $p).Text).Value | Should Be "`"$L`" %command% -debug_mode"
        Test-Path "$p.renameanything-tmp" | Should Be $false
    }
    It 'second identical run is a no-op (no new backup)' {
        $dir = Reset-Env 'file-noop'
        $p = Join-Path $dir 'localconfig.vdf'
        Write-Utf8 $p (New-LocalConfigText (New-AppBlock '"LaunchOptions"		""'))
        [void](Update-RaLocalConfig -ConfigPath $p -Compute { param($o) Add-RaWrapper $o $L })
        $r2 = Update-RaLocalConfig -ConfigPath $p -Compute { param($o) Add-RaWrapper $o $L }
        $r2.Changed | Should Be $false
        @(Get-ChildItem $dir -Filter '*.renameanything-backup-*').Count | Should Be 1
    }
    It 'restores the backup when post-write verification fails' {
        $dir = Reset-Env 'file-restore'
        $p = Join-Path $dir 'localconfig.vdf'
        Write-Utf8 $p (New-LocalConfigText (New-AppBlock '"LaunchOptions"		"-debug_mode"'))
        $orig = [IO.File]::ReadAllBytes($p)
        $script:RaAfterWriteHook = { param($path) [IO.File]::AppendAllText($path, 'garbage"') }
        Assert-RaThrow { Update-RaLocalConfig -ConfigPath $p -Compute { param($o) Add-RaWrapper $o $L } } 'restored from backup'
        Test-RaBytesEqual ([IO.File]::ReadAllBytes($p)) $orig | Should Be $true
        $script:RaAfterWriteHook = $null
    }
    It 'refuses to edit a corrupted file and leaves it untouched' {
        $dir = Reset-Env 'file-corrupt'
        $p = Join-Path $dir 'localconfig.vdf'
        $t = New-LocalConfigText (New-AppBlock $null)
        Write-Utf8 $p $t.Substring(0, $t.Length - 3)
        $orig = [IO.File]::ReadAllBytes($p)
        Assert-RaThrow { Update-RaLocalConfig -ConfigPath $p -Compute { 'x' } }
        Test-RaBytesEqual ([IO.File]::ReadAllBytes($p)) $orig | Should Be $true
        @(Get-ChildItem $dir -Filter '*.renameanything-*').Count | Should Be 0
    }
    It 'refuses invalid UTF-8' {
        $dir = Reset-Env 'file-utf8'
        $p = Join-Path $dir 'localconfig.vdf'
        [IO.File]::WriteAllBytes($p, [byte[]](0x22, 0xC3, 0x28, 0x22))
        Assert-RaThrow { Read-RaTextFile $p } 'UTF-8'
    }
}

Describe 'Steam process, accounts and end-to-end flows' {
    It 'Steam running + -Yes: refuses, never writes' {
        $dir = Reset-Env 'steam-running'
        $steam = Join-Path $dir 'Steam'
        New-FakeSteam $steam @{ '111' = (New-LocalConfigText (New-AppBlock '"LaunchOptions"		"-debug_mode"')) }
        $before = Get-TreeHash $steam
        $script:RaGetSteamProcess = { @('fake-steam') }
        $state = Read-RaState
        Assert-RaThrow { Set-RaSteamLaunchOption $state $steam '' $L } 'Steam is running'
        (Get-TreeHash $steam) -join '|' | Should Be ($before -join '|')
    }
    It 'Steam running interactive: Q skips, Enter after closing proceeds' {
        [void](Reset-Env 'steam-wait')
        $script:RaAssumeYes = $false
        $script:RaGetSteamProcess = { @('fake-steam') }
        $script:RaReadHost = { 'q' }
        Wait-RaSteamClosed | Should Be $false
        $script:steamCalls = 0
        $script:RaGetSteamProcess = { $script:steamCalls++; if ($script:steamCalls -le 2) { @('fake') } else { @() } }
        $script:RaReadHost = { '' }
        Wait-RaSteamClosed | Should Be $true
    }
    It 'multiple accounts: -Yes without selector fails; selector and interactive choice work' {
        $dir = Reset-Env 'accounts'
        $steam = Join-Path $dir 'Steam'
        $cfg = New-LocalConfigText (New-AppBlock $null)
        New-FakeSteam $steam @{ '111' = $cfg; '222' = $cfg }
        $acc = @(Get-RaSteamAccount $steam)
        $acc.Count | Should Be 2
        ($acc | Where-Object { $_.Id -eq '222' }).Name | Should Be 'Persona 222'
        Assert-RaThrow { Select-RaSteamAccount $acc '' } 'Several Steam accounts'
        @(Select-RaSteamAccount $acc 'all').Count | Should Be 2
        @(Select-RaSteamAccount $acc '222')[0].Id | Should Be '222'
        Assert-RaThrow { Select-RaSteamAccount $acc '999' } 'Unknown'
        $script:RaAssumeYes = $false
        $script:RaReadHost = { '2' }
        $sel = @(Select-RaSteamAccount $acc '')
        $sel.Count | Should Be 1
        $sel[0].Id | Should Be '222'
    }
    It 'finds the game directory through libraryfolders/appmanifest' {
        $dir = Reset-Env 'gamedir'
        $steam = Join-Path $dir 'Steam'
        New-FakeSteam $steam @{ '111' = (New-LocalConfigText (New-AppBlock $null)) }
        Get-RaGameDir $steam | Should Be (Join-Path $steam 'steamapps\common\Hearts of Iron IV')
    }

    It 'install fails closed on wrong ZIP hash, tampered file, extra file, zip-slip and missing file' {
        $dir = Reset-Env 'install-bad'
        foreach ($case in 'zip', 'filehash', 'extra', 'zipslip', 'missing') {
            $pkg = New-TestPackage $dir $(if ($case -eq 'zip') { 'none' } else { $case })
            $m = $pkg.Manifest
            if ($case -eq 'zip') { $m.ZipSha256 = ('0' * 64) }
            elseif ($case -ne 'none') {
                # Pin the tampered ZIP itself so the per-file checks are what must catch it.
                $m.ZipSha256 = (Get-FileHash -LiteralPath $pkg.Zip).Hash
            }
            $script:RaPinnedRelease = $m
            $state = Read-RaState
            Assert-RaThrow { Install-RaPackage $state $pkg.Zip }
            Test-Path (Join-Path $script:RaRoot '0.1.0') | Should Be $false
            @(Get-ChildItem $script:RaRoot -Force -Filter '.staging-*' -ErrorAction SilentlyContinue).Count | Should Be 0
            Test-Path (Join-Path $dir 'evil.txt') | Should Be $false
            Test-Path (Join-Path $script:RaRoot 'evil.txt') | Should Be $false
        }
    }

    It 'end-to-end: install + Steam option (existing flags) + shortcut in custom folder, then uninstall restores exactly' {
        $dir = Reset-Env 'e2e'
        $steam = Join-Path $dir 'Steam'
        $cfgText = New-LocalConfigText (New-AppBlock '"LaunchOptions"		"-debug_mode"')
        New-FakeSteam $steam @{ '111' = $cfgText }
        $cfg = Join-Path $steam 'userdata\111\config\localconfig.vdf'
        $origCfg = [IO.File]::ReadAllBytes($cfg)
        $pkg = New-TestPackage $dir
        $script:RaPinnedRelease = $pkg.Manifest
        $custom = Join-Path $dir 'My Games Folder'
        New-Item -ItemType Directory -Path $custom | Out-Null
        $gameBefore = Get-TreeHash (Join-Path $steam 'steamapps')

        $code = Invoke-RaMain @{ LoadMethod = 'Launcher'
            Install = $true; SteamLaunchOption = $true; ShortcutPath = $custom; Yes = $true; PackagePath = $pkg.Zip; SteamRoot = $steam
        }
        $code | Should Be 0
        $launcher = Join-Path $script:RaRoot 'current\RenameAnythingLauncher.exe'
        Test-Path $launcher | Should Be $true
        (Get-RaAppLaunchInfo (Read-RaTextFile $cfg).Text).Value | Should Be "`"$launcher`" %command% -debug_mode"
        $lnk = Join-Path $custom $script:RaShortcutFileName
        Test-Path $lnk | Should Be $true
        $si = Get-RaShortcutInfo $lnk
        $si.TargetPath | Should Be $launcher
        $si.Arguments | Should Be ('"' + (Join-Path $steam 'steamapps\common\Hearts of Iron IV\dowser.exe') + '"')
        $si.IconLocation | Should Match 'hoi4\.exe,0$'
        $si.Description | Should Match 'Not declared multiplayer-compatible'
        $state = Read-RaState
        $state.InstalledVersion | Should Be '0.1.0'
        @($state.Shortcuts) -contains $lnk | Should Be $true
        @($state.SteamEntries)[0].OriginalValue | Should Be '-debug_mode'
        (Get-TreeHash (Join-Path $steam 'steamapps')) -join '|' | Should Be ($gameBefore -join '|')
        Test-Path (Join-Path $script:RaRoot 'bootstrap.log') | Should Be $true

        # A foreign shortcut with the same name elsewhere must survive uninstall.
        $desk = & $script:RaDesktopPath
        $foreign = Join-Path $desk $script:RaShortcutFileName
        $sh = New-Object -ComObject WScript.Shell; $s = $sh.CreateShortcut($foreign); $s.TargetPath = (Join-Path $env:WINDIR 'System32\notepad.exe'); $s.Save()
        # Uninstall with Steam running must not remove anything.
        $script:RaGetSteamProcess = { @('fake') }
        (Invoke-RaMain @{ LoadMethod = 'Launcher'; Uninstall = $true; Yes = $true; SteamRoot = $steam }) | Should Be 1
        Test-Path $launcher | Should Be $true
        Test-Path $lnk | Should Be $true
        $script:RaGetSteamProcess = { @() }
        (Invoke-RaMain @{ LoadMethod = 'Launcher'; Uninstall = $true; Yes = $true; SteamRoot = $steam }) | Should Be 0
        Test-RaBytesEqual ([IO.File]::ReadAllBytes($cfg)) $origCfg | Should Be $true
        Test-Path $lnk | Should Be $false
        Test-Path $foreign | Should Be $true
        @(Get-ChildItem $script:RaRoot -Force | ForEach-Object Name) -join ',' | Should Be 'bootstrap.log'
        (Get-TreeHash (Join-Path $steam 'steamapps')) -join '|' | Should Be ($gameBefore -join '|')
    }

    It 'uninstall without state removes only our fragment (user edited options afterwards)' {
        $dir = Reset-Env 'uninstall-nostate'
        $steam = Join-Path $dir 'Steam'
        $val = ConvertTo-RaVdfString ((Add-RaWrapper '-debug_mode' $L) + ' -nomusic')
        New-FakeSteam $steam @{
            '111' = (New-LocalConfigText (New-AppBlock ('"LaunchOptions"		"' + $val + '"')))
            '222' = (New-LocalConfigText (New-AppBlock '"LaunchOptions"		"-x"'))
        }
        $cfg222 = Join-Path $steam 'userdata\222\config\localconfig.vdf'
        $orig222 = [IO.File]::ReadAllBytes($cfg222)
        (Invoke-RaMain @{ LoadMethod = 'Launcher'; RemoveSteamLaunchOption = $true; Yes = $true; SteamRoot = $steam }) | Should Be 0
        (Get-RaAppLaunchInfo (Read-RaTextFile (Join-Path $steam 'userdata\111\config\localconfig.vdf')).Text).Value | Should Be '-debug_mode -nomusic'
        Test-RaBytesEqual ([IO.File]::ReadAllBytes($cfg222)) $orig222 | Should Be $true
        @(Get-ChildItem (Split-Path $cfg222) -Filter '*.renameanything-*').Count | Should Be 0
    }

    It 'Steam option on an account without the 394360 block and on multiple accounts' {
        $dir = Reset-Env 'noblock'
        $steam = Join-Path $dir 'Steam'
        New-FakeSteam $steam @{ '111' = (New-LocalConfigText '' -NoApp); '222' = (New-LocalConfigText (New-AppBlock $null)) }
        $pkg = New-TestPackage $dir
        $script:RaPinnedRelease = $pkg.Manifest
        $options = @{ LoadMethod = 'Launcher'
            Install = $true; SteamLaunchOption = $true; SteamAccount = 'all'; Yes = $true; PackagePath = $pkg.Zip; SteamRoot = $steam
        }
        (Invoke-RaMain $options) | Should Be 0
        $launcher = Join-Path $script:RaRoot 'current\RenameAnythingLauncher.exe'
        foreach ($id in '111', '222') {
            (Get-RaAppLaunchInfo (Read-RaTextFile (Join-Path $steam "userdata\$id\config\localconfig.vdf")).Text).Value | Should Be "`"$launcher`" %command%"
        }
        (Invoke-RaMain @{ LoadMethod = 'Launcher'; Uninstall = $true; Yes = $true; SteamRoot = $steam }) | Should Be 0
        foreach ($id in '111', '222') {
            (Get-RaAppLaunchInfo (Read-RaTextFile (Join-Path $steam "userdata\$id\config\localconfig.vdf")).Text).Value | Should Be ''
        }
    }

    It 'dry run writes nothing anywhere' {
        $dir = Reset-Env 'dryrun'
        $steam = Join-Path $dir 'Steam'
        New-FakeSteam $steam @{ '111' = (New-LocalConfigText (New-AppBlock '"LaunchOptions"		"-debug_mode"')) }
        $pkg = New-TestPackage $dir
        $script:RaPinnedRelease = $pkg.Manifest
        # Install for real first so the Steam/shortcut steps have a launcher, then dry-run those.
        (Invoke-RaMain @{ LoadMethod = 'Launcher'; Install = $true; Yes = $true; PackagePath = $pkg.Zip; SteamRoot = $steam }) | Should Be 0
        $before = Get-TreeHash $dir
        (Invoke-RaMain @{
            LoadMethod = 'Launcher'; SteamLaunchOption = $true; Shortcut = $true
            DryRun = $true; Yes = $true; SteamRoot = $steam
        }) | Should Be 0
        (Invoke-RaMain @{ LoadMethod = 'Launcher'; Uninstall = $true; DryRun = $true; Yes = $true; SteamRoot = $steam }) | Should Be 0
        (Get-TreeHash $dir) -join '|' | Should Be ($before -join '|')
    }

    It 'refuses to overwrite a foreign file with the shortcut name' {
        $dir = Reset-Env 'foreign-lnk'
        $steam = Join-Path $dir 'Steam'
        New-FakeSteam $steam @{ '111' = (New-LocalConfigText (New-AppBlock $null)) }
        $pkg = New-TestPackage $dir
        $script:RaPinnedRelease = $pkg.Manifest
        $desk = & $script:RaDesktopPath
        Write-Utf8 (Join-Path $desk $script:RaShortcutFileName) 'not a shortcut'
        (Invoke-RaMain @{ LoadMethod = 'Launcher'; Install = $true; Yes = $true; PackagePath = $pkg.Zip; SteamRoot = $steam }) | Should Be 0
        $lnkResult = Invoke-RaMain @{ LoadMethod = 'Launcher'; Shortcut = $true; Yes = $true; SteamRoot = $steam }
        $lnkResult | Should Be 1
        [IO.File]::ReadAllText((Join-Path $desk $script:RaShortcutFileName)) | Should Be 'not a shortcut'
    }

    It 'interactive flow: install + shortcut via folder picker, declining Steam' {
        $dir = Reset-Env 'interactive'
        $steam = Join-Path $dir 'Steam'
        New-FakeSteam $steam @{ '111' = (New-LocalConfigText (New-AppBlock $null)) }
        $cfg = Join-Path $steam 'userdata\111\config\localconfig.vdf'
        $origCfg = [IO.File]::ReadAllBytes($cfg)
        $pkg = New-TestPackage $dir
        $script:RaPinnedRelease = $pkg.Manifest
        $picked = Join-Path $dir 'Picked'
        New-Item -ItemType Directory -Path $picked | Out-Null
        $script:RaFolderPicker = [scriptblock]::Create("'$picked'")
        $script:answers = New-Object System.Collections.Queue
        foreach ($a in 'y', '3', 'n', '2', 'y') { $script:answers.Enqueue($a) }  # install, both, decline Steam, picker, confirm shortcut
        $script:RaAssumeYes = $false
        $script:RaReadHost = { $script:answers.Dequeue() }
        (Invoke-RaMain @{ LoadMethod = 'Launcher'; PackagePath = $pkg.Zip; SteamRoot = $steam; Plain = $true }) | Should Be 0
        $script:answers.Count | Should Be 0
        Test-Path (Join-Path $picked $script:RaShortcutFileName) | Should Be $true
        Test-RaBytesEqual ([IO.File]::ReadAllBytes($cfg)) $origCfg | Should Be $true
        (Get-RaShortcutInfo (Join-Path $picked $script:RaShortcutFileName)).Description | Should Match 'Not declared multiplayer-compatible'
        $script:RaFolderPicker = $null
    }
}

function New-SignedRelease {
    <#
    .SYNOPSIS
        Builds a package and a signed manifest and registers fake GitHub URLs for them.
    .DESCRIPTION
        Uses sign-release-manifest.ps1 (-MakeManifest, -Sign) exactly as a maintainer
        would, then serves the release JSON, manifest, signature and ZIP through the
        network mock ($script:fakeWeb).
    .PARAMETER Dir
        Work folder.
    .PARAMETER Version
        Release version.
    .PARAMETER KeyPath
        Private key used to sign.
    .PARAMETER GameSha
        Supported hoi4.exe hashes.
    .PARAMETER Tag
        Release tag; default v<Version>.
    .PARAMETER AssetHost
        Asset URL prefix (a foreign host must be refused).
    .PARAMETER WithMod
        Include the "CAPIE" switch mod in the package.
    .OUTPUTS
        System.Collections.Hashtable (Zip, ManifestPath, Base, ZipName)
    .EXAMPLE
        New-SignedRelease -Dir $e.Dir -Version '0.2.0' -KeyPath $e.Key
    #>
    [CmdletBinding()]
    [OutputType([hashtable])]
    param(
        [Parameter(Mandatory)][string]$Dir,
        [Parameter(Mandatory)][string]$Version,
        [Parameter(Mandatory)][string]$KeyPath,
        [string[]]$GameSha = @(),
        [string]$Tag = '',
        [string]$AssetHost = $fakeGitHubAssetHost,
        [switch]$WithMod
    )
    $pkg = New-TestPackage (Join-Path $Dir "rel-$Version") -Version $Version -WithMod:$WithMod
    $mPath = Join-Path $Dir "rel-$Version\release-manifest.json"
    [void](& $signScript -MakeManifest -ZipPath $pkg.Zip -Version $Version -SupportedGameSha256 $GameSha -OutPath $mPath)
    [void](& $signScript -Sign -ManifestPath $mPath -PrivateKeyPath $KeyPath)
    if (-not $Tag) { $Tag = "v$Version" }
    $base = "$AssetHost/$Tag"
    $zipName = [IO.Path]::GetFileName($pkg.Zip)
    $assets = @(
        [pscustomobject]@{ name = 'release-manifest.json'; browser_download_url = "$base/release-manifest.json" },
        [pscustomobject]@{ name = 'release-manifest.json.sig'; browser_download_url = "$base/release-manifest.json.sig" },
        [pscustomobject]@{ name = $zipName; browser_download_url = "$base/$zipName" }
    )
    $rel = [pscustomobject]@{ tag_name = $Tag; assets = $assets }
    $script:fakeWeb[$fakeGitHubApiUrl] = $utf8.GetBytes(($rel | ConvertTo-Json -Depth 5))
    $script:fakeWeb["$base/release-manifest.json"] = [IO.File]::ReadAllBytes($mPath)
    $script:fakeWeb["$base/release-manifest.json.sig"] = [IO.File]::ReadAllBytes("$mPath.sig")
    $script:fakeWeb["$base/$zipName"] = [IO.File]::ReadAllBytes($pkg.Zip)
    return @{ Zip = $pkg.Zip; ManifestPath = $mPath; Base = $base; ZipName = $zipName }
}

function Initialize-UpdateEnv {
    <#
    .SYNOPSIS
        Prepares an installed 0.1.0 with Steam option and shortcut, a signing key and the test repository.
    .DESCRIPTION
        Starting point of every update/rollback test.
    .PARAMETER Name
        Work folder name.
    .OUTPUTS
        System.Collections.Hashtable (Dir, Steam, Key, Cfg, GameSha)
    .EXAMPLE
        $e = Initialize-UpdateEnv 'upd-flow'
    #>
    [CmdletBinding()]
    [OutputType([hashtable])]
    param([Parameter(Mandatory, Position = 0)][string]$Name)
    $dir = Reset-Env $Name
    $steam = Join-Path $dir 'Steam'
    New-FakeSteam $steam @{ '111' = (New-LocalConfigText (New-AppBlock '"LaunchOptions"		"-debug_mode"')) }
    $key = Join-Path $dir 'update.private.xml'
    $script:RaUpdatePublicKeyXml = (& $signScript -NewKey -PrivateKeyPath $key -KeySize 2048) | Out-String
    $script:RaGitHubRepo = 'test-owner/test-repo'
    $p1 = New-TestPackage (Join-Path $dir 'v1') -Version '0.1.0'
    $script:RaPinnedRelease = $p1.Manifest
    $options = @{
        LoadMethod = 'Launcher'; Install = $true; SteamLaunchOption = $true; Shortcut = $true
        Yes = $true; PackagePath = $p1.Zip; SteamRoot = $steam
    }
    # Pester 3.4 'Should' can emit a value; keep the helper's output to the one hashtable.
    $null = (Invoke-RaMain $options) | Should Be 0
    return @{
        Dir     = $dir
        Steam   = $steam
        Key     = $key
        Cfg     = (Join-Path $steam 'userdata\111\config\localconfig.vdf')
        GameSha = (Get-FileHash (Join-Path $steam 'steamapps\common\Hearts of Iron IV\hoi4.exe')).Hash
    }
}

function Get-CurrentTargetName {
    <#
    .SYNOPSIS
        Name of the version folder the 'current' junction points to.
    .DESCRIPTION
        Shortcut for assertions on the active version.
    .OUTPUTS
        System.String
    .EXAMPLE
        Get-CurrentTargetName | Should Be '0.2.0'
    #>
    [CmdletBinding()]
    [OutputType([string])]
    param()
    return (Split-Path -Leaf (Get-RaJunctionTarget (Get-RaCurrentLink)))
}

Describe 'Updates (mocked GitHub API, signed manifest, current junction, rollback)' {
    It 'install creates the stable current junction; Steam option and shortcut point at it' {
        $e = Initialize-UpdateEnv 'upd-install'
        Get-CurrentTargetName | Should Be '0.1.0'
        $cur = Join-Path $script:RaRoot 'current\RenameAnythingLauncher.exe'
        Test-Path $cur | Should Be $true
        (Get-RaAppLaunchInfo (Read-RaTextFile $e.Cfg).Text).Value | Should Be "`"$cur`" %command% -debug_mode"
        (Get-RaShortcutInfo (Join-Path (& $script:RaDesktopPath) $script:RaShortcutFileName)).TargetPath | Should Be $cur
        $script:requested.Count | Should Be 0
    }
    It '-CheckUpdate: one API call + 2 manifest assets, reports newer version and game support, changes nothing' {
        $e = Initialize-UpdateEnv 'upd-check'
        [void](New-SignedRelease -Dir $e.Dir -Version '0.2.0' -KeyPath $e.Key -GameSha @($e.GameSha))
        $before = @(Get-TreeHash $e.Dir | Where-Object { $_ -notmatch 'bootstrap\.log=' })
        $r = Invoke-RaCheckUpdate (Read-RaState) (Get-RaGameDir $e.Steam) $null
        $r.Newer | Should Be $true
        $r.InstalledSupportsGame | Should Be $false
        $r.LatestSupportsGame | Should Be $true
        $script:requested.Count | Should Be 3
        $script:requested[0] | Should Be 'https://api.github.com/repos/test-owner/test-repo/releases/latest'
        @(Get-TreeHash $e.Dir | Where-Object { $_ -notmatch 'bootstrap\.log=' }) -join '|' | Should Be ($before -join '|')
        (Invoke-RaMain @{ LoadMethod = 'Launcher'; CheckUpdate = $true; Yes = $true; SteamRoot = $e.Steam }) | Should Be 0
    }
    It '-Update switches current without touching Steam config/shortcut; -Rollback returns; third version prunes the oldest' {
        $e = Initialize-UpdateEnv 'upd-flow'
        $cfgBefore = [IO.File]::ReadAllBytes($e.Cfg)
        $lnk = Join-Path (& $script:RaDesktopPath) $script:RaShortcutFileName
        $lnkBefore = [IO.File]::ReadAllBytes($lnk)
        [void](New-SignedRelease -Dir $e.Dir -Version '0.2.0' -KeyPath $e.Key)
        (Invoke-RaMain @{ LoadMethod = 'Launcher'; Update = $true; Yes = $true; SteamRoot = $e.Steam }) | Should Be 0
        Get-CurrentTargetName | Should Be '0.2.0'
        $st = Read-RaState
        $st.InstalledVersion | Should Be '0.2.0'
        $st.PreviousVersion | Should Be '0.1.0'
        Test-RaBytesEqual ([IO.File]::ReadAllBytes($e.Cfg)) $cfgBefore | Should Be $true
        Test-RaBytesEqual ([IO.File]::ReadAllBytes($lnk)) $lnkBefore | Should Be $true
        @(Get-ChildItem (Split-Path $e.Cfg) -Filter '*.renameanything-backup-*').Count | Should Be 1
        # Up to date: second -Update does nothing.
        (Invoke-RaMain @{ LoadMethod = 'Launcher'; Update = $true; Yes = $true; SteamRoot = $e.Steam }) | Should Be 0
        (Read-RaState).PreviousVersion | Should Be '0.1.0'
        # Rollback
        (Invoke-RaMain @{ LoadMethod = 'Launcher'; Rollback = $true; Yes = $true; SteamRoot = $e.Steam }) | Should Be 0
        Get-CurrentTargetName | Should Be '0.1.0'
        (Read-RaState).PreviousVersion | Should Be '0.2.0'
        # Roll forward again via -Update (0.2.0 already present and verified)
        (Invoke-RaMain @{ LoadMethod = 'Launcher'; Update = $true; Yes = $true; SteamRoot = $e.Steam }) | Should Be 0
        Get-CurrentTargetName | Should Be '0.2.0'
        # 0.3.0 -> keep 0.3.0 (current) and 0.2.0 (previous), prune 0.1.0
        [void](New-SignedRelease -Dir $e.Dir -Version '0.3.0' -KeyPath $e.Key)
        (Invoke-RaMain @{ LoadMethod = 'Launcher'; Update = $true; Yes = $true; SteamRoot = $e.Steam }) | Should Be 0
        Get-CurrentTargetName | Should Be '0.3.0'
        Test-Path (Join-Path $script:RaRoot '0.1.0') | Should Be $false
        Test-Path (Join-Path $script:RaRoot '0.2.0') | Should Be $true
        Test-RaBytesEqual ([IO.File]::ReadAllBytes($e.Cfg)) $cfgBefore | Should Be $true
        # Uninstall removes the junction and all versions, keeps only the log; Steam option restored
        (Invoke-RaMain @{ LoadMethod = 'Launcher'; Uninstall = $true; Yes = $true; SteamRoot = $e.Steam }) | Should Be 0
        @(Get-ChildItem $script:RaRoot -Force | ForEach-Object Name) -join ',' | Should Be 'bootstrap.log'
        (Get-RaAppLaunchInfo (Read-RaTextFile $e.Cfg).Text).Value | Should Be '-debug_mode'
    }
    It 'tampered manifest (bad signature) is refused and nothing changes' {
        $e = Initialize-UpdateEnv 'upd-badsig'
        $r = New-SignedRelease -Dir $e.Dir -Version '0.2.0' -KeyPath $e.Key
        $script:fakeWeb["$($r.Base)/release-manifest.json"] = $utf8.GetBytes(([IO.File]::ReadAllText($r.ManifestPath)).Replace('"0.2.0"', '"0.2.1"'))
        (Invoke-RaMain @{ LoadMethod = 'Launcher'; Update = $true; Yes = $true; SteamRoot = $e.Steam }) | Should Be 1
        Get-CurrentTargetName | Should Be '0.1.0'
        Test-Path (Join-Path $script:RaRoot '0.2.0') | Should Be $false
        Test-Path (Join-Path $script:RaRoot '0.2.1') | Should Be $false
    }
    It 'manifest signed with a different key is refused' {
        $e = Initialize-UpdateEnv 'upd-otherkey'
        $other = Join-Path $e.Dir 'other.private.xml'
        [void](& $signScript -NewKey -PrivateKeyPath $other -KeySize 2048)
        [void](New-SignedRelease -Dir $e.Dir -Version '0.2.0' -KeyPath $other)
        (Invoke-RaMain @{ LoadMethod = 'Launcher'; Update = $true; Yes = $true; SteamRoot = $e.Steam }) | Should Be 1
        Get-CurrentTargetName | Should Be '0.1.0'
    }
    It 'valid signature but ZIP with a different hash is refused (fail-closed), current unchanged' {
        $e = Initialize-UpdateEnv 'upd-badzip'
        $r = New-SignedRelease -Dir $e.Dir -Version '0.2.0' -KeyPath $e.Key
        $bad = New-TestPackage (Join-Path $e.Dir 'bad') -Tamper 'filehash' -Version '0.2.0'
        $script:fakeWeb["$($r.Base)/$($r.ZipName)"] = [IO.File]::ReadAllBytes($bad.Zip)
        (Invoke-RaMain @{ LoadMethod = 'Launcher'; Update = $true; Yes = $true; SteamRoot = $e.Steam }) | Should Be 1
        Get-CurrentTargetName | Should Be '0.1.0'
        Test-Path (Join-Path $script:RaRoot '0.2.0') | Should Be $false
        @(Get-ChildItem $script:RaRoot -Force -Filter 'download-*').Count | Should Be 0
        @(Get-ChildItem $script:RaRoot -Force -Filter '.staging-*').Count | Should Be 0
    }
    It 'tag/manifest version mismatch and foreign asset host are refused' {
        $e = Initialize-UpdateEnv 'upd-mismatch'
        [void](New-SignedRelease -Dir $e.Dir -Version '0.2.0' -KeyPath $e.Key -Tag 'v9.9.9')
        Assert-RaThrow { Get-RaRemoteRelease $null } 'does not match'
        $script:fakeWeb = @{}
        [void](New-SignedRelease -Dir $e.Dir -Version '0.2.0' -KeyPath $e.Key -AssetHost 'https://evil.example/releases/download')
        Assert-RaThrow { Get-RaRemoteRelease $null } 'Unexpected asset URL'
        Get-CurrentTargetName | Should Be '0.1.0'
    }
    It 'fails closed while the signing key or repository is still a placeholder' {
        $e = Initialize-UpdateEnv 'upd-placeholder'
        [void](New-SignedRelease -Dir $e.Dir -Version '0.2.0' -KeyPath $e.Key)
        $script:RaUpdatePublicKeyXml = 'TO-BE-GENERATED'
        Assert-RaThrow { Get-RaRemoteRelease $null } 'signing key is not configured'
        $script:RaGitHubRepo = $origRepo
        $script:requested.Clear()
        Assert-RaThrow { Get-RaRemoteRelease $null } 'not configured'
        $script:requested.Count | Should Be 0
    }
    It 'offline update with -ManifestPath/-PackagePath makes no network request' {
        $e = Initialize-UpdateEnv 'upd-offline'
        $r = New-SignedRelease -Dir $e.Dir -Version '0.2.0' -KeyPath $e.Key
        $script:fakeWeb = @{}
        $script:requested.Clear()
        $options = @{
            LoadMethod = 'Launcher'; Update = $true; ManifestPath = $r.ManifestPath
            PackagePath = $r.Zip; Yes = $true; SteamRoot = $e.Steam
        }
        (Invoke-RaMain $options) | Should Be 0
        Get-CurrentTargetName | Should Be '0.2.0'
        $script:requested.Count | Should Be 0
    }
    It 'rollback refuses when the previous version is missing or modified' {
        $e = Initialize-UpdateEnv 'upd-rbfail'
        (Invoke-RaMain @{ LoadMethod = 'Launcher'; Rollback = $true; Yes = $true; SteamRoot = $e.Steam }) | Should Be 1
        [void](New-SignedRelease -Dir $e.Dir -Version '0.2.0' -KeyPath $e.Key)
        (Invoke-RaMain @{ LoadMethod = 'Launcher'; Update = $true; Yes = $true; SteamRoot = $e.Steam }) | Should Be 0
        [IO.File]::WriteAllText((Join-Path $script:RaRoot '0.1.0\runtime\rename_anything_country.dll'), 'tampered')
        (Invoke-RaMain @{ LoadMethod = 'Launcher'; Rollback = $true; Yes = $true; SteamRoot = $e.Steam }) | Should Be 1
        Get-CurrentTargetName | Should Be '0.2.0'
    }
}

Describe 'Real localconfig.vdf copy (optional)' {
    $real = $env:RA_BOOTSTRAP_REAL_LOCALCONFIG
    if ($real -and (Test-Path -LiteralPath $real)) {
        It 'adds and removes the wrapper with a minimal diff and exact round trip' {
            $dir = Reset-Env 'real-copy'
            $p = Join-Path $dir 'localconfig.vdf'
            Copy-Item -LiteralPath $real -Destination $p
            $orig = [IO.File]::ReadAllBytes($p)
            $origText = (Read-RaTextFile $p).Text
            $before = (Get-RaAppLaunchInfo $origText).Value
            [void](Update-RaLocalConfig -ConfigPath $p -Compute { param($o) Add-RaWrapper $o $L })
            $newText = (Read-RaTextFile $p).Text
            $a = $origText -split "`n"; $b = $newText -split "`n"
            $diff = @(Compare-Object $a $b)
            ($diff.Count -le 2) | Should Be $true
            @($diff | Where-Object { $_.InputObject -notmatch '"LaunchOptions"' }).Count | Should Be 0
            [void](Update-RaLocalConfig -ConfigPath $p -Compute { if ($null -eq $before) { '' } else { $before } })
            if ($null -ne $before) { Test-RaBytesEqual ([IO.File]::ReadAllBytes($p)) $orig | Should Be $true }
        }
    } else {
        It 'skipped (RA_BOOTSTRAP_REAL_LOCALCONFIG not set)' { $true | Should Be $true }
    }
}

Describe 'Build and structure (single-file distribution from src/)' -Tags 'Build' {
    It 'install.ps1 is up to date with tools/bootstrap/src (build-install.ps1 -Check)' {
        $out = & $buildScript -Check
        $LASTEXITCODE | Should Be 0
        ($out -join ' ') | Should Match 'UP-TO-DATE'
    }
    It 'the -Check mode detects a stale install.ps1 (negative control)' {
        $dir = Reset-Env 'build-stale'
        $copy = Join-Path $dir 'install.ps1'
        [IO.File]::WriteAllText($copy, [IO.File]::ReadAllText($scriptPath) + "`n# hand edit`n")
        $out = & $buildScript -Check -OutputPath $copy
        $LASTEXITCODE | Should Be 1
        ($out -join ' ') | Should Match 'STALE'
    }
    It 'every source fragment is pure ASCII without BOM' {
        foreach ($f in Get-ChildItem -LiteralPath $sourceDir -Recurse -Filter '*.ps1') {
            @([IO.File]::ReadAllBytes($f.FullName) | Where-Object { $_ -gt 127 }).Count | Should Be 0
        }
    }
    It 'every function is documented and typed (help, CmdletBinding, OutputType)' {
        $ast = [System.Management.Automation.Language.Parser]::ParseFile($scriptPath, [ref]$null, [ref]$null)
        $functions = @($ast.FindAll({ param($n) $n -is [System.Management.Automation.Language.FunctionDefinitionAst] }, $true))
        $functions.Count | Should BeGreaterThan 60
        $bad = @()
        foreach ($fn in $functions) {
            $help = $fn.GetHelpContent()
            $attrs = @($fn.Body.ParamBlock.Attributes | ForEach-Object { $_.TypeName.Name })
            $ok = $help -and $help.Synopsis -and $help.Description -and $help.Examples.Count -gt 0 -and $help.Outputs.Count -gt 0 -and
            ($attrs -contains 'CmdletBinding') -and ($attrs -contains 'OutputType')
            if (-not $ok) { $bad += $fn.Name }
        }
        ($bad -join ', ') | Should Be ''
    }
    It 'outside Adapters.ps1 nothing calls the host, processes, registry, network or COM directly' {
        $forbiddenCommands = @('Write-Host', 'Read-Host', 'Get-Process', 'Get-ItemProperty', 'Get-UICulture', 'Get-Date')
        $forbiddenText = @('System.Net.WebClient', 'WScript.Shell')
        $violations = @()
        foreach ($f in Get-ChildItem -LiteralPath $sourceDir -Recurse -Filter '*.ps1' | Where-Object { $_.Name -ne 'Adapters.ps1' }) {
            $ast = [System.Management.Automation.Language.Parser]::ParseFile($f.FullName, [ref]$null, [ref]$null)
            $commands = $ast.FindAll({ param($n) $n -is [System.Management.Automation.Language.CommandAst] }, $true)
            foreach ($c in $commands) { if ($forbiddenCommands -contains $c.GetCommandName()) { $violations += "$($f.Name): $($c.GetCommandName())" } }
            $text = [IO.File]::ReadAllText($f.FullName)
            foreach ($s in $forbiddenText) { if ($text.Contains($s)) { $violations += "$($f.Name): $s" } }
        }
        ($violations -join '; ') | Should Be ''
    }
    It 'the derived wrapper pattern equals the historical literal' {
        $script:RaWrapperPattern | Should BeExactly '(?:"[^"]*\\RenameAnythingLauncher\.exe"|(?<!\S)[^\s"]*\\RenameAnythingLauncher\.exe)[ \t]*'
    }
}

Describe 'Adapters (test seams for external dependencies)' {
    It 'registry adapter: Steam path from HKCU is normalized; missing value fails clearly' {
        $dir = Reset-Env 'adapter-registry'
        $steam = Join-Path $dir 'Steam'
        New-Item -ItemType Directory -Path $steam | Out-Null
        $script:registryCalls = New-Object System.Collections.ArrayList
        $script:RaGetRegistryValue = { param($p, $n) [void]$script:registryCalls.Add("$p|$n"); $steam.Replace('\', '/') }
        Get-RaSteamRoot '' | Should Be $steam
        $script:registryCalls[0] | Should Be 'HKCU:\Software\Valve\Steam|SteamPath'
        $script:RaGetRegistryValue = { throw 'value missing' }
        Assert-RaThrow { Get-RaSteamRoot '' } 'Steam not found'
        $script:RaGetRegistryValue = { Join-Path $dir 'NoSuchSteam' }
        Assert-RaThrow { Get-RaSteamRoot '' } 'does not exist'
    }
    It 'no action on a console that cannot answer: usage text, exit code 2, nothing written, no prompt' {
        $dir = Reset-Env 'no-tty'
        $script:lines = New-Object System.Collections.ArrayList
        $script:RaWriteHost = { param($m, $c) [void]$c; [void]$script:lines.Add($m) }
        $script:RaQuiet = $false
        $before = Get-TreeHash $dir
        (Invoke-RaMain @{ DryRun = $true }) | Should Be 2
        (Invoke-RaMain @{ SteamRoot = 'C:\nowhere' }) | Should Be 2
        $text = $script:lines -join "`n"
        $text | Should Match 'nothing was changed'
        $text | Should Match '-Install'
        $text | Should Match '-Uninstall \[-Purge\]'
        Test-Path $script:RaRoot | Should Be $false
        (Get-TreeHash $dir) -join '|' | Should Be ($before -join '|')
    }
    It 'a real child process with redirected input and output exits with 2 instead of waiting' {
        $dir = Reset-Env 'no-tty-child'
        $start = New-Object System.Diagnostics.ProcessStartInfo
        $start.FileName = (Get-Process -Id $PID).Path
        $start.Arguments = "-NoProfile -NonInteractive -File `"$scriptPath`" -DryRun"
        $start.UseShellExecute = $false
        $start.RedirectStandardInput = $true
        $start.RedirectStandardOutput = $true
        $start.RedirectStandardError = $true
        $start.EnvironmentVariables['LOCALAPPDATA'] = Join-Path $dir 'LocalAppData'
        $child = [System.Diagnostics.Process]::Start($start)
        try {
            $stdout = $child.StandardOutput.ReadToEndAsync()
            $finished = $child.WaitForExit(60000)
            if (-not $finished) { $child.Kill() }
            $finished | Should Be $true
            $child.ExitCode | Should Be 2
            $stdout.Result | Should Match 'Run it again with one action'
            @($stdout.Result.ToCharArray() | Where-Object { [int]$_ -gt 127 }).Count | Should Be 0
        } finally { $child.Dispose() }
        Test-Path (Join-Path $dir 'LocalAppData\RenameAnything') | Should Be $false
    }
    It 'clock adapter: backup names are deterministic and never overwrite' {
        $dir = Reset-Env 'adapter-clock'
        $script:RaNow = { [datetime]::new(2026, 1, 2, 3, 4, 5) }
        $p = Join-Path $dir 'localconfig.vdf'
        New-RaBackupPath $p | Should Be "$p.renameanything-backup-20260102-030405"
        Write-Utf8 "$p.renameanything-backup-20260102-030405" 'x'
        New-RaBackupPath $p | Should Be "$p.renameanything-backup-20260102-030405-1"
        Write-Utf8 $p (New-LocalConfigText (New-AppBlock $null))
        $r = Update-RaLocalConfig -ConfigPath $p -Compute { 'X' }
        $r.Backup | Should Be "$p.renameanything-backup-20260102-030405-1"
    }
    It 'console adapter: levels map to colours; quiet, -NoConsole and dry-run behave' {
        $dir = Reset-Env 'adapter-console'
        $script:lines = New-Object System.Collections.ArrayList
        $script:RaWriteHost = { param($m, $c) [void]$script:lines.Add("$c|$m") }
        $script:RaQuiet = $false
        Write-RaLog 'w' -Level WARN
        Write-RaLog 'p' -Level PLAN
        Write-RaLog 'hidden' -NoConsole
        $script:RaQuiet = $true
        Write-RaLog 'quiet'
        ($script:lines -join ',') | Should Be 'Yellow|w,Cyan|p'
        $log = [IO.File]::ReadAllText((Get-RaLogPath))
        $log | Should Match '\[WARN\] w'
        $log | Should Match '\[INFO\] hidden'
        $log | Should Match '\[INFO\] quiet'
        $script:RaDry = $true
        Write-RaLog 'dry'
        [IO.File]::ReadAllText((Get-RaLogPath)) | Should Not Match 'dry'
        (Get-RaLogPath).StartsWith($dir) | Should Be $true
    }
    It 'file adapters: a failing replace is restored from the verified backup' {
        $dir = Reset-Env 'adapter-copy'
        $p = Join-Path $dir 'localconfig.vdf'
        Write-Utf8 $p (New-LocalConfigText (New-AppBlock '"LaunchOptions"		"-debug_mode"'))
        $orig = [IO.File]::ReadAllBytes($p)
        $script:RaCopyFile = {
            param($s, $d)
            if ($s.EndsWith('.renameanything-tmp')) { throw 'disk full (simulated)' }
            [IO.File]::Copy($s, $d, $true)
        }
        Assert-RaThrow { Update-RaLocalConfig -ConfigPath $p -Compute { param($o) Add-RaWrapper $o $L } } 'restored from backup'
        Test-RaBytesEqual ([IO.File]::ReadAllBytes($p)) $orig | Should Be $true
        Test-Path "$p.renameanything-tmp" | Should Be $false
    }
    It 'file adapters: the strict reader sees exactly the adapter bytes' {
        [void](Reset-Env 'adapter-read')
        $script:RaReadAllBytes = { , [byte[]](0xEF, 0xBB, 0xBF, 0x61, 0x0D, 0x0A) }
        $f = Read-RaTextFile 'C:\does-not-exist\x.vdf'
        $f.HasBom | Should Be $true
        $f.Text | Should Be "a`r`n"
        $f.Newline | Should Be "`r`n"
        $script:RaReadAllBytes = { , [byte[]](0x22, 0xC3, 0x28) }
        Assert-RaThrow { Read-RaTextFile 'C:\does-not-exist\x.vdf' } 'not valid UTF-8'
    }
    It 'shortcut adapters: the .lnk spec and ownership checks work without COM' {
        $dir = Reset-Env 'adapter-lnk'
        $steam = Join-Path $dir 'Steam'
        New-FakeSteam $steam @{ '111' = (New-LocalConfigText (New-AppBlock $null)) }
        $game = Get-RaGameDir $steam
        $script:lnkStore = @{}
        $script:RaWriteShortcut = { param($p, $spec) $script:lnkStore[$p] = $spec; Write-Utf8 $p 'lnk' }
        $script:RaReadShortcut = {
            param($p)
            if ($script:lnkStore.ContainsKey($p)) { $script:lnkStore[$p] } else { [pscustomobject]@{ TargetPath = 'C:\other.exe' } }
        }
        $launcher = Join-Path $script:RaRoot 'current\RenameAnythingLauncher.exe'
        $state = New-RaState
        $folder = Join-Path $dir 'Games'
        New-Item -ItemType Directory -Path $folder | Out-Null
        $lnk = New-RaShortcut -State $state -Folder $folder -LauncherPath $launcher -GameDir $game
        $spec = $script:lnkStore[$lnk]
        $spec.TargetPath | Should Be $launcher
        $spec.Arguments | Should Be ('"' + (Join-Path $game 'dowser.exe') + '"')
        $spec.WorkingDirectory | Should Be (Split-Path -Parent $launcher)
        $spec.IconLocation | Should Be ((Join-Path $game 'hoi4.exe') + ',0')
        $spec.Description | Should Match 'Not declared multiplayer-compatible'
        @($state.Shortcuts) -contains $lnk | Should Be $true
        $foreign = Join-Path (& $script:RaDesktopPath) $script:RaShortcutFileName
        Write-Utf8 $foreign 'foreign'
        Test-RaOwnShortcut $lnk | Should Be $true
        Test-RaOwnShortcut $foreign | Should Be $false
        Remove-RaOwnShortcut $state
        Test-Path $lnk | Should Be $false
        Test-Path $foreign | Should Be $true
        @($state.Shortcuts).Count | Should Be 0
    }
    It 'network adapter: only HTTPS reaches the adapter' {
        [void](Reset-Env 'adapter-http')
        Assert-RaThrow { Get-RaHttpContent 'http://example.com/x' } 'non-HTTPS'
        $script:requested.Count | Should Be 0
        $script:fakeWeb['https://example.com/x'] = [byte[]](1, 2, 3)
        $bytes = Get-RaHttpContent 'https://example.com/x'
        $bytes.Count | Should Be 3
        $bytes.GetType().Name | Should Be 'Byte[]'
        $script:requested.Count | Should Be 1
    }
}

Describe 'Extracted functions' {
    It 'Get-RaUndoValue restores the exact original only while our value is unchanged' {
        $entry = [pscustomobject]@{ WrittenValue = (Add-RaWrapper '-x' $L); OriginalValue = '%command% -x' }
        Get-RaUndoValue (Add-RaWrapper '-x' $L) $entry | Should Be '%command% -x'
        Get-RaUndoValue ((Add-RaWrapper '-x' $L) + ' -y') $entry | Should Be '-x -y'
        $nullOriginal = [pscustomobject]@{ WrittenValue = (Add-RaWrapper '' $L); OriginalValue = $null }
        Get-RaUndoValue (Add-RaWrapper '' $L) $nullOriginal | Should Be ''
        Get-RaUndoValue (Add-RaWrapper '-z' $L) $null | Should Be '-z'
    }
    It 'Get-RaSetupPlan maps the start mode menu and re-asks on invalid input' {
        [void](Reset-Env 'plan')
        $script:RaAssumeYes = $false
        $script:answers = New-Object System.Collections.Queue
        foreach ($a in 'n', '9', '1', 'y') { $script:answers.Enqueue($a) }
        $script:RaReadHost = { $script:answers.Dequeue() }
        $plan = Get-RaSetupPlan @{ LoadMethod = 'Launcher'; Install = $null; SteamLaunchOption = $null; Shortcut = $null; ShortcutPath = $null }
        $plan.Install | Should Be $false
        $plan.Steam | Should Be $true
        $plan.Shortcut | Should Be $false
        $script:answers.Count | Should Be 0
        $script:RaAssumeYes = $true
        Assert-RaThrow {
            Get-RaSetupPlan @{ LoadMethod = 'Launcher'; Install = $null; SteamLaunchOption = $null; Shortcut = $null; ShortcutPath = $null }
        } 'With -Yes'
    }
    It 'Test-RaReleaseAssetUrl accepts only this repository''s release downloads' {
        [void](Reset-Env 'asset-url')
        $script:RaGitHubRepo = 'test-owner/test-repo'
        Test-RaReleaseAssetUrl "$fakeGitHubAssetHost/v1/x.zip" | Should Be $true
        Test-RaReleaseAssetUrl 'HTTPS://GITHUB.COM/test-owner/test-repo/releases/download/v1/x.zip' | Should Be $true
        Test-RaReleaseAssetUrl 'https://github.com/other/test-repo/releases/download/v1/x.zip' | Should Be $false
        Test-RaReleaseAssetUrl '' | Should Be $false
    }
    It 'Get-RaSteamLibrary reads the legacy libraryfolders.vdf format' {
        $dir = Reset-Env 'libraries'
        $steam = Join-Path $dir 'Steam'
        New-Item -ItemType Directory -Force -Path (Join-Path $steam 'steamapps') | Out-Null
        $legacy = "`"LibraryFolders`"`n{`n`t`"TimeNextStatsReport`"`t`t`"1`"`n`t`"1`"`t`t`"D:\\Games\\Steam`"`n}`n"
        Write-Utf8 (Join-Path $steam 'steamapps\libraryfolders.vdf') $legacy
        $libs = @(Get-RaSteamLibrary $steam)
        $libs.Count | Should Be 2
        $libs[1] | Should Be 'D:\Games\Steam'
    }
    It 'Compare-RaVersion compares numerically and ignores suffixes' {
        Compare-RaVersion '0.10.0' '0.9.9' | Should Be 1
        Compare-RaVersion '1.0.0-beta' '1.0.0' | Should Be 0
        Compare-RaVersion '0.1.0' '0.2.0' | Should Be -1
    }
    It 'Test-RaManifest rejects unsafe paths, a missing launcher and bad game hashes' {
        $h = 'A' * 64
        $base = { [ordered]@{ Version = '1.0.0'; ZipName = 'a.zip'; ZipSha256 = $h; Files = [ordered]@{ 'RenameAnythingLauncher.exe' = $h } } }
        $m = & $base
        Test-RaManifest $m
        $m.Contains('SupportedGameSha256') | Should Be $true
        $m = & $base
        $m.Files['runtime/../x.dll'] = $h
        Assert-RaThrow { Test-RaManifest $m } 'unsafe path'
        $m = & $base
        $m.Files.Remove('RenameAnythingLauncher.exe')
        $m.Files['x.dll'] = $h
        Assert-RaThrow { Test-RaManifest $m } 'launcher missing'
        $m = & $base
        $m['SupportedGameSha256'] = @('nothex')
        Assert-RaThrow { Test-RaManifest $m } 'bad game hash'
    }
    It 'Select-RaSteamAccount: interactive A selects all, invalid numbers are asked again' {
        $accounts = @([pscustomobject]@{ Id = '1'; Name = $null }, [pscustomobject]@{ Id = '2'; Name = 'P' })
        [void](Reset-Env 'select-all')
        $script:RaAssumeYes = $false
        $script:answers = New-Object System.Collections.Queue
        foreach ($a in '7', 'a') { $script:answers.Enqueue($a) }
        $script:RaReadHost = { $script:answers.Dequeue() }
        @(Select-RaSteamAccount $accounts '').Count | Should Be 2
        $script:answers.Count | Should Be 0
    }
    It 'Get-RaPackageZip refuses to guess a download URL while the repository is a placeholder' {
        [void](Reset-Env 'pkg-url')
        $m = [ordered]@{ Version = '1.0.0'; ZipName = 'a.zip' }
        Assert-RaThrow { Get-RaPackageZip -Manifest $m -LocalZip '' -ZipUrl '' } 'placeholder'
        $script:requested.Count | Should Be 0
    }
    It 'ConvertTo-RaFileContent writes the BOM only when asked' {
        @(ConvertTo-RaFileContent 'a' $false).Count | Should Be 1
        $withBom = ConvertTo-RaFileContent 'a' $true
        $withBom.Count | Should Be 4
        $withBom[0] | Should Be 0xEF
    }
}

Describe 'CAPIE proxy installation and ownership' {
    It 'interactive Proxy menu re-asks unsupported shortcut choices without changing the selected method' {
        [void](Reset-Env 'proxy-menu')
        $script:RaAssumeYes = $false
        $script:answers = New-Object System.Collections.Queue
        foreach ($answer in @('n', '2', '2', '3', '4')) { $script:answers.Enqueue($answer) }
        $script:RaReadHost = { $script:answers.Dequeue() }
        $plan = Get-RaSetupPlan @{ Install = $null; SteamLaunchOption = $null; Shortcut = $null; ShortcutPath = $null }
        $plan.LoadMethod | Should Be 'Proxy'
        $plan.Shortcut | Should Be $false
        $plan.Steam | Should Be $false
        $script:answers.Count | Should Be 0
    }
    It 'interactive explicit Proxy choice overrides a saved Launcher preference' {
        [void](Reset-Env 'proxy-menu-existing')
        $script:RaAssumeYes = $false
        $state = New-RaState
        $state.InstalledVersion = '0.1.0'
        $state.LoadMethod = 'Launcher'
        $script:answers = New-Object System.Collections.Queue
        foreach ($answer in @('n', '2', '4')) { $script:answers.Enqueue($answer) }
        $script:RaReadHost = { $script:answers.Dequeue() }
        $plan = Get-RaSetupPlan -Options @{
            Install = $null; SteamLaunchOption = $null; Shortcut = $null; ShortcutPath = $null
        } -State $state
        $plan.LoadMethod | Should Be 'Proxy'
        $script:answers.Count | Should Be 0
    }
    It 'requires a complete, case-sensitive activation argument and preserves quoted text' {
        Test-RaHasCapieFlag '-debug_mode -capie' | Should Be $true
        Test-RaHasCapieFlag '"-capie"' | Should Be $true
        foreach ($value in @('-capie-extra', '-CAPIE', '"description -capie text"', 'prefix"-capie"', '"-capie"suffix')) {
            Test-RaHasCapieFlag $value | Should Be $false
        }
        Add-RaCapieFlag '-debug_mode' | Should Be '-debug_mode -capie'
        Add-RaCapieFlag '-debug_mode -capie' | Should Be '-debug_mode -capie'
        Remove-RaCapieFlag '"description -capie text" -capie' | Should Be '"description -capie text"'
        Test-RaHasCapieFlag '-ca"pie"' | Should Be $true
        Remove-RaCapieFlag '-debug -ca"pie"' | Should Be '-debug'
        Test-RaHasCapieFlag '-debug " -capie' | Should Be $false
        Assert-RaThrow { Add-RaCapieFlag '-debug " -capie' } 'unterminated quote'
        Test-RaHasCapieFlag '""" -capie' | Should Be $true
        Test-RaHasCapieFlag '"""" -capie' | Should Be $false
    }
    It 'explicit Proxy install copies only verified proxy, appends the Steam flag, and uninstall restores original files' {
        $dir = Reset-Env 'proxy-default'
        $steam = Join-Path $dir 'Steam'
        New-FakeSteam $steam @{ '111' = (New-LocalConfigText (New-AppBlock '"LaunchOptions" "-debug_mode"')) }
        $game = Get-RaGameDir $steam
        $before = Get-TreeHash $game
        $cfg = Join-Path $steam 'userdata\111\config\localconfig.vdf'
        $cfgBefore = [IO.File]::ReadAllBytes($cfg)
        $pkg = New-TestPackage $dir -WithProxy
        $script:RaPinnedRelease = $pkg.Manifest
        $proxyOptions = @{ LoadMethod = 'Proxy'; Install = $true; SteamLaunchOption = $true; Yes = $true; SteamRoot = $steam; PackagePath = $pkg.Zip }
        (Invoke-RaMain $proxyOptions) | Should Be 0
        $state = Read-RaState
        $state.LoadMethod | Should Be 'Proxy'
        $state.Proxy.Path | Should Be (Join-Path $game 'version.dll')
        Test-RaOwnProxy $state $state.Proxy.Path | Should Be $true
        (Get-RaAppLaunchInfo (Read-RaTextFile $cfg).Text).Value | Should Be '-debug_mode -capie'
        Test-Path (Join-Path $script:RaRoot 'proxy.enabled') | Should Be $false
        (Invoke-RaMain @{ Uninstall = $true; Yes = $true; SteamRoot = $steam }) | Should Be 0
        (Get-TreeHash $game) -join '|' | Should Be ($before -join '|')
        Test-RaBytesEqual ([IO.File]::ReadAllBytes($cfg)) $cfgBefore | Should Be $true
    }
    It 'foreign version.dll is neither overwritten nor deleted, including when the state has been lost' {
        $dir = Reset-Env 'proxy-foreign'
        $steam = Join-Path $dir 'Steam'
        New-FakeSteam $steam @{ '111' = (New-LocalConfigText (New-AppBlock $null)) }
        $path = Join-Path (Get-RaGameDir $steam) 'version.dll'
        Write-Utf8 $path 'foreign-tool'
        $pkg = New-TestPackage $dir -WithProxy
        $script:RaPinnedRelease = $pkg.Manifest
        (Invoke-RaMain @{ LoadMethod = 'Proxy'; Install = $true; Yes = $true; SteamRoot = $steam; PackagePath = $pkg.Zip }) | Should Be 1
        [IO.File]::ReadAllText($path) | Should Be 'foreign-tool'
        (Invoke-RaMain @{ Uninstall = $true; Yes = $true; SteamRoot = $steam }) | Should Be 0
        [IO.File]::ReadAllText($path) | Should Be 'foreign-tool'
    }
    It 'launcher-only releases refuse explicit Proxy mode; the default Launcher mode leaves the game folder unchanged' {
        $dir = Reset-Env 'proxy-old-release'
        $steam = Join-Path $dir 'Steam'
        New-FakeSteam $steam @{ '111' = (New-LocalConfigText (New-AppBlock $null)) }
        $pkg = New-TestPackage $dir
        $script:RaPinnedRelease = $pkg.Manifest
        (Invoke-RaMain @{ LoadMethod = 'Proxy'; Install = $true; Yes = $true; SteamRoot = $steam; PackagePath = $pkg.Zip }) | Should Be 1
        (Invoke-RaMain @{ Install = $true; Yes = $true; SteamRoot = $steam; PackagePath = $pkg.Zip }) | Should Be 0
        (Invoke-RaMain @{ Install = $true; Shortcut = $true; Yes = $true; SteamRoot = $steam; PackagePath = $pkg.Zip }) | Should Be 0
        (Read-RaState).LoadMethod | Should Be 'Launcher'
        Test-Path (Join-Path (Get-RaGameDir $steam) 'version.dll') | Should Be $false
    }
    It 'Config activation is explicit, survives state round trip, and Flag removes the owned marker' {
        $dir = Reset-Env 'proxy-config'
        $steam = Join-Path $dir 'Steam'
        New-FakeSteam $steam @{ '111' = (New-LocalConfigText (New-AppBlock $null)) }
        $pkg = New-TestPackage $dir -WithProxy
        $script:RaPinnedRelease = $pkg.Manifest
        $configOptions = @{ LoadMethod = 'Proxy'; Install = $true; ProxyActivation = 'Config'; Yes = $true; SteamRoot = $steam; PackagePath = $pkg.Zip }
        (Invoke-RaMain $configOptions) | Should Be 0
        $path = Join-Path $script:RaRoot 'proxy.enabled'
        [IO.File]::ReadAllText($path) | Should BeExactly "CAPIE_PROXY_ENABLED_V1`n"
        $state = Read-RaState
        $state.ProxyConfigSha256 | Should Be (Get-RaFileSha256 $path)
        (Invoke-RaMain @{ Install = $true; Yes = $true; SteamRoot = $steam; PackagePath = $pkg.Zip }) | Should Be 0
        (Read-RaState).ProxyActivation | Should Be 'Config'
        [IO.File]::ReadAllText($path) | Should BeExactly "CAPIE_PROXY_ENABLED_V1`n"
        Set-RaProxyActivation $state Flag
        Test-Path $path | Should Be $false
    }
    It 'switching to Launcher removes owned proxy and marker before activating a launcher-only release' {
        $dir = Reset-Env 'proxy-to-launcher'
        $steam = Join-Path $dir 'Steam'
        New-FakeSteam $steam @{ '111' = (New-LocalConfigText (New-AppBlock $null)) }
        $p1 = New-TestPackage (Join-Path $dir 'v1') -WithProxy
        $script:RaPinnedRelease = $p1.Manifest
        $configOptions = @{ LoadMethod = 'Proxy'; Install = $true; ProxyActivation = 'Config'; Yes = $true; SteamRoot = $steam; PackagePath = $p1.Zip }
        (Invoke-RaMain $configOptions) | Should Be 0
        $p2 = New-TestPackage (Join-Path $dir 'v2') -Version '0.2.0'
        $script:RaPinnedRelease = $p2.Manifest
        (Invoke-RaMain @{
            LoadMethod = 'Launcher'; Install = $true; SteamLaunchOption = $true
            Yes = $true; SteamRoot = $steam; PackagePath = $p2.Zip
        }) | Should Be 0
        $state = Read-RaState
        $state.InstalledVersion | Should Be '0.2.0'
        $state.LoadMethod | Should Be 'Launcher'
        $null -eq $state.Proxy | Should Be $true
        Test-Path (Join-Path (Get-RaGameDir $steam) 'version.dll') | Should Be $false
        Test-Path (Join-Path $script:RaRoot 'proxy.enabled') | Should Be $false
        $cfg = Join-Path $steam 'userdata\111\config\localconfig.vdf'
        Test-RaHasWrapper (Get-RaAppLaunchInfo (Read-RaTextFile $cfg).Text).Value | Should Be $true
    }
    It 'modified activation config is retained and blocks uninstall before bundle removal' {
        [void](Reset-Env 'proxy-config-foreign')
        Initialize-RaRoot
        $path = Join-Path $script:RaRoot 'proxy.enabled'
        Write-Utf8 $path 'foreign-data'
        $state = New-RaState
        Assert-RaThrow { Set-RaProxyActivation $state Config } 'left untouched'
        Assert-RaThrow { Invoke-RaUninstall $state $null } 'left untouched'
        [IO.File]::ReadAllText($path) | Should Be 'foreign-data'
    }
    It 'a replacement racing owned DLL or marker removal is restored and never deleted' {
        $dir = Reset-Env 'proxy-remove-race'
        foreach ($name in @('version.dll', 'proxy.enabled')) {
            $path = Join-Path $dir $name
            Write-Utf8 $path 'owned-file'
            $hash = Get-RaFileSha256 $path
            $script:RaBeforeOwnedRemove = { param($replacementPath) Write-Utf8 $replacementPath 'foreign-replacement' }
            try { Assert-RaThrow { Remove-RaOwnedFile -Path $path -Sha256 $hash } 'File changed during removal' }
            finally { $script:RaBeforeOwnedRemove = $null }
            [IO.File]::ReadAllText($path) | Should Be 'foreign-replacement'
            @(Get-ChildItem -LiteralPath $dir -Filter '*.remove').Count | Should Be 0
        }
    }
    It 'updating and rolling back synchronize proxy bytes and current runtime; a modified proxy blocks the switch' {
        $dir = Reset-Env 'proxy-rollback'
        $steam = Join-Path $dir 'Steam'
        New-FakeSteam $steam @{ '111' = (New-LocalConfigText (New-AppBlock $null)) }
        $p1 = New-TestPackage (Join-Path $dir 'v1') -WithProxy
        $script:RaPinnedRelease = $p1.Manifest
        (Invoke-RaMain @{ LoadMethod = 'Proxy'; Install = $true; Yes = $true; SteamRoot = $steam; PackagePath = $p1.Zip }) | Should Be 0
        $state = Read-RaState
        $p2 = New-TestPackage (Join-Path $dir 'v2') -Version '0.2.0' -WithProxy
        [void](Install-RaVersion $p2.Manifest $p2.Zip $null)
        Set-RaActiveVersion $state '0.2.0'
        [IO.File]::ReadAllText($state.Proxy.Path) | Should Be 'proxy-0.2.0'
        Invoke-RaRollback $state
        [IO.File]::ReadAllText($state.Proxy.Path) | Should Be 'proxy-0.1.0'
        $state.InstalledVersion | Should Be '0.1.0'
        Write-Utf8 $state.Proxy.Path 'foreign-replacement'
        Assert-RaThrow { Set-RaActiveVersion $state '0.2.0' } 'left untouched'
        $state.InstalledVersion | Should Be '0.1.0'
        (Get-RaJunctionTarget (Get-RaCurrentLink)) | Should Be (Get-RaVersionDir '0.1.0')
        Remove-RaProxy $state
        [IO.File]::ReadAllText((Join-Path (Get-RaGameDir $steam) 'version.dll')) | Should Be 'foreign-replacement'
    }
    It 'a locked owned proxy keeps the active version and ownership state for a later retry' {
        $dir = Reset-Env 'proxy-locked'
        $steam = Join-Path $dir 'Steam'
        New-FakeSteam $steam @{ '111' = (New-LocalConfigText (New-AppBlock $null)) }
        $p1 = New-TestPackage (Join-Path $dir 'v1') -WithProxy
        $script:RaPinnedRelease = $p1.Manifest
        (Invoke-RaMain @{ LoadMethod = 'Proxy'; Install = $true; Yes = $true; SteamRoot = $steam; PackagePath = $p1.Zip }) | Should Be 0
        $state = Read-RaState
        $p2 = New-TestPackage (Join-Path $dir 'v2') -Version '0.2.0' -WithProxy
        [void](Install-RaVersion $p2.Manifest $p2.Zip $null)
        $lock = [IO.File]::Open($state.Proxy.Path, 'Open', 'Read', 'Read')
        try { Assert-RaThrow { Set-RaActiveVersion $state '0.2.0' } } finally { $lock.Dispose() }
        Test-RaOwnProxy $state $state.Proxy.Path | Should Be $true
        $state.InstalledVersion | Should Be '0.1.0'
        (Get-RaJunctionTarget (Get-RaCurrentLink)) | Should Be (Get-RaVersionDir '0.1.0')
    }
    It 'failure after replacing current restores both the old junction and owned proxy before returning' {
        $dir = Reset-Env 'proxy-switch-failure'
        $steam = Join-Path $dir 'Steam'
        New-FakeSteam $steam @{ '111' = (New-LocalConfigText (New-AppBlock $null)) }
        $p1 = New-TestPackage (Join-Path $dir 'v1') -WithProxy
        $script:RaPinnedRelease = $p1.Manifest
        (Invoke-RaMain @{ LoadMethod = 'Proxy'; Install = $true; Yes = $true; SteamRoot = $steam; PackagePath = $p1.Zip }) | Should Be 0
        $state = Read-RaState
        $p2 = New-TestPackage (Join-Path $dir 'v2') -Version '0.2.0' -WithProxy
        [void](Install-RaVersion $p2.Manifest $p2.Zip $null)
        $script:RaQuiet = $false
        $script:RaWriteHost = {
            param($message, $color)
            [void]$color
            if ($message -like 'Active version: 0.2.0*') { throw 'Injected failure after current was switched.' }
        }
        Assert-RaThrow { Set-RaActiveVersion $state '0.2.0' } 'Injected failure'
        $state.InstalledVersion | Should Be '0.1.0'
        $null -eq $state.PreviousVersion | Should Be $true
        Test-RaOwnProxy $state $state.Proxy.Path | Should Be $true
        [IO.File]::ReadAllText($state.Proxy.Path) | Should Be 'proxy-0.1.0'
        (Get-RaJunctionTarget (Get-RaCurrentLink)) | Should Be (Get-RaVersionDir '0.1.0')
    }
    It 'directories and links named version.dll are refused and dry-run writes nothing' {
        $dir = Reset-Env 'proxy-link'
        $steam = Join-Path $dir 'Steam'
        New-FakeSteam $steam @{ '111' = (New-LocalConfigText (New-AppBlock $null)) }
        $path = Join-Path (Get-RaGameDir $steam) 'version.dll'
        $target = Join-Path $dir 'foreign-target'
        New-Item -ItemType Directory -Path $target | Out-Null
        New-Item -ItemType Junction -Path $path -Target $target | Out-Null
        Assert-RaThrow { Get-RaProxyPath (Get-RaGameDir $steam) } 'left untouched'
        [IO.Directory]::Delete($path, $false)
        $pkg = New-TestPackage $dir -WithProxy
        $script:RaPinnedRelease = $pkg.Manifest
        $before = Get-TreeHash $dir
        (Invoke-RaMain @{ LoadMethod = 'Proxy'; Install = $true; DryRun = $true; Yes = $true; SteamRoot = $steam; PackagePath = $pkg.Zip }) | Should Be 0
        (Get-TreeHash $dir) -join '|' | Should Be ($before -join '|')
    }
}

function New-InstallOption {
    <#
    .SYNOPSIS
        Options of a non-interactive launcher-mode install with the Steam launch option.
    .DESCRIPTION
        Shared by the release-installer and terminal-interface tests.
    .PARAMETER Package
        Result of New-TestPackage.
    .PARAMETER Steam
        Fake Steam folder.
    .OUTPUTS
        System.Collections.Hashtable
    .EXAMPLE
        Invoke-RaMain (New-InstallOption $pkg $steam)
    #>
    [CmdletBinding()]
    [OutputType([hashtable])]
    param([Parameter(Mandatory, Position = 0)][hashtable]$Package, [Parameter(Mandatory, Position = 1)][string]$Steam)
    return @{
        LoadMethod = 'Launcher'; Install = $true; SteamLaunchOption = $true; Yes = $true
        PackagePath = $Package.Zip; SteamRoot = $Steam
    }
}
function Set-TuiKeyQueue {
    <#
    .SYNOPSIS
        Feeds named keys to the terminal interface (non-interactive test mode).
    .DESCRIPTION
        Names: Up, Down, Enter, Escape, or one character. Reading past the queue fails the test.
    .PARAMETER Keys
        Keys in order.
    .OUTPUTS
        None.
    .EXAMPLE
        Set-TuiKeyQueue 'Down', 'Enter'
    #>
    [CmdletBinding()]
    [OutputType([void])]
    param([Parameter(Mandatory, Position = 0)][string[]]$Keys)
    $script:keyQueue = New-Object System.Collections.Queue
    foreach ($k in $Keys) { $script:keyQueue.Enqueue($k) }
    $script:RaReadKey = {
        if ($script:keyQueue.Count -eq 0) { throw 'No more keys queued' }
        $k = $script:keyQueue.Dequeue()
        $named = @{ Up = 'UpArrow'; Down = 'DownArrow'; Enter = 'Enter'; Escape = 'Escape' }
        if ($named.ContainsKey($k)) { [pscustomobject]@{ Key = $named[$k]; KeyChar = [char]0 } }
        else { [pscustomobject]@{ Key = 'Other'; KeyChar = [char]$k } }
    }
}

function Set-TuiTerminal {
    <#
    .SYNOPSIS
        Makes the terminal adapter report an interactive console.
    .DESCRIPTION
        -Vt selects a virtual-terminal host (colour, in-place redraw); -Env sets environment values.
    .PARAMETER Vt
        Virtual-terminal host.
    .PARAMETER Width
        Console width.
    .PARAMETER Env
        Environment values seen by the script.
    .OUTPUTS
        None.
    .EXAMPLE
        Set-TuiTerminal -Vt
    #>
    [CmdletBinding()]
    [OutputType([void])]
    param([switch]$Vt, [int]$Width = 100, [hashtable]$Env = @{})
    $script:termVt = [bool]$Vt
    $script:termWidth = $Width
    $script:termEnv = $Env
    $script:RaGetTerminal = { [pscustomobject]@{ Interactive = $true; Vt = $script:termVt; Width = $script:termWidth } }
    $script:RaGetEnv = { param($n) $script:termEnv[$n] }
    $script:RaQuiet = $false   # console log lines are part of the interface
}

function Get-PlainOutput {
    <#
    .SYNOPSIS
        Captured interface output without ANSI sequences.
    .DESCRIPTION
        Removes SGR and cursor sequences so assertions read the visible text.
    .OUTPUTS
        System.String
    .EXAMPLE
        Get-PlainOutput | Should Match 'Summary'
    #>
    [CmdletBinding()]
    [OutputType([string])]
    param()
    return ($script:rawOut.ToString() -replace "$([char]27)\[[0-9;?]*[A-Za-z]", '')
}

Describe 'Release installer (-Install -Latest) and the way back to the clean game' {
    It '-Install -Latest: signed manifest and every hash verified, installed into the user folder, game folder untouched' {
        $dir = Reset-Env 'latest-install'
        $steam = Join-Path $dir 'Steam'
        New-FakeSteam $steam @{ '111' = (New-LocalConfigText (New-AppBlock '"LaunchOptions"		"-debug_mode"')) }
        $key = Join-Path $dir 'update.private.xml'
        $script:RaUpdatePublicKeyXml = (& $signScript -NewKey -PrivateKeyPath $key -KeySize 2048) | Out-String
        $script:RaGitHubRepo = 'test-owner/test-repo'
        [void](New-SignedRelease -Dir $dir -Version '0.2.0' -KeyPath $key)
        $gameBefore = Get-TreeHash (Join-Path $steam 'steamapps')
        (Invoke-RaMain @{ Install = $true; Latest = $true; LoadMethod = 'Launcher'; Yes = $true; SteamRoot = $steam }) | Should Be 0
        Get-CurrentTargetName | Should Be '0.2.0'
        $script:requested.Count | Should Be 4
        (Get-TreeHash (Join-Path $steam 'steamapps')) -join '|' | Should Be ($gameBefore -join '|')
        $log = [IO.File]::ReadAllText((Get-RaLogPath))
        $log | Should Match 'Release 0\.2\.0: signed manifest verified'
        $log | Should Match 'SHA256 matches the manifest'
        (Read-RaState).Proxy | Should Be $null
    }
    It '-Latest alone means -Install; a tampered ZIP installs nothing' {
        $dir = Reset-Env 'latest-bad'
        $steam = Join-Path $dir 'Steam'
        New-FakeSteam $steam @{ '111' = (New-LocalConfigText (New-AppBlock $null)) }
        $key = Join-Path $dir 'update.private.xml'
        $script:RaUpdatePublicKeyXml = (& $signScript -NewKey -PrivateKeyPath $key -KeySize 2048) | Out-String
        $script:RaGitHubRepo = 'test-owner/test-repo'
        $r = New-SignedRelease -Dir $dir -Version '0.2.0' -KeyPath $key
        $bad = New-TestPackage (Join-Path $dir 'bad') -Tamper 'filehash' -Version '0.2.0'
        $script:fakeWeb["$($r.Base)/$($r.ZipName)"] = [IO.File]::ReadAllBytes($bad.Zip)
        (Invoke-RaMain @{ Latest = $true; LoadMethod = 'Launcher'; Yes = $true; SteamRoot = $steam }) | Should Be 1
        Test-Path (Join-Path $script:RaRoot '0.2.0') | Should Be $false
        Test-Path (Get-RaCurrentLink) | Should Be $false
    }
    It '-Uninstall -Purge restores the exact state from before the installation' {
        $dir = Reset-Env 'purge'
        $steam = Join-Path $dir 'Steam'
        New-FakeSteam $steam @{ '111' = (New-LocalConfigText (New-AppBlock '"LaunchOptions"		"-debug_mode"')) }
        $pkg = New-TestPackage (Join-Path $dir 'pkg')
        $script:RaPinnedRelease = $pkg.Manifest
        $before = @(Get-TreeHash $dir | Where-Object { $_ -notmatch '\\pkg\\' })
        (Invoke-RaMain (New-InstallOption $pkg $steam)) | Should Be 0
        @(Get-ChildItem (Join-Path $steam 'userdata\111\config') -Filter '*.renameanything-backup-*').Count | Should Be 1
        (Invoke-RaMain @{ Uninstall = $true; Purge = $true; Yes = $true; SteamRoot = $steam }) | Should Be 0
        Test-Path $script:RaRoot | Should Be $false
        @(Get-TreeHash $dir | Where-Object { $_ -notmatch '\\pkg\\' }) -join '|' | Should Be ($before -join '|')
    }
    It '-Purge deletes only our own localconfig.vdf backups; other files in the Steam folder stay' {
        $dir = Reset-Env 'purge-foreign'
        $steam = Join-Path $dir 'Steam'
        New-FakeSteam $steam @{ '111' = (New-LocalConfigText (New-AppBlock $null)) }
        $pkg = New-TestPackage (Join-Path $dir 'pkg')
        $script:RaPinnedRelease = $pkg.Manifest
        $cfgDir = Join-Path $steam 'userdata\111\config'
        Write-Utf8 (Join-Path $cfgDir 'localconfig.vdf.my-own-backup') 'mine'
        Write-Utf8 (Join-Path $cfgDir 'localconfig.vdf.renameanything-backup-notadate') 'lookalike'
        (Invoke-RaMain (New-InstallOption $pkg $steam)) | Should Be 0
        (Invoke-RaMain @{ Uninstall = $true; Purge = $true; Yes = $true; SteamRoot = $steam }) | Should Be 0
        Test-Path (Join-Path $cfgDir 'localconfig.vdf.my-own-backup') | Should Be $true
        Test-Path (Join-Path $cfgDir 'localconfig.vdf.renameanything-backup-notadate') | Should Be $true
        @(Get-ChildItem $cfgDir -Force | Where-Object { $_.Name -match $script:RaBackupNamePattern }).Count | Should Be 0
        Test-Path $script:RaRoot | Should Be $false
    }
    It '-Purge keeps our folder when a foreign file appears in it after the uninstall step' {
        $dir = Reset-Env 'purge-late-foreign'
        New-Item -ItemType Directory -Force -Path $script:RaRoot | Out-Null
        Write-Utf8 (Get-RaLogPath) 'log'
        $notes = Join-Path $script:RaRoot 'notes.txt'
        Write-Utf8 $notes 'user file'
        Invoke-RaPurge '' | Should Be $false
        @(Get-ChildItem $script:RaRoot -Force | ForEach-Object Name | Sort-Object) -join ',' | Should Be 'bootstrap.log,notes.txt'
        [IO.File]::Delete($notes)
        Invoke-RaPurge '' | Should Be $true
        Test-Path $script:RaRoot | Should Be $false
        Test-Path $dir | Should Be $true
    }
    It '-Purge without -Uninstall is ignored; -Uninstall -Purge -DryRun writes nothing' {
        $dir = Reset-Env 'purge-dry'
        $steam = Join-Path $dir 'Steam'
        New-FakeSteam $steam @{ '111' = (New-LocalConfigText (New-AppBlock '"LaunchOptions"		"-x"')) }
        $pkg = New-TestPackage (Join-Path $dir 'pkg')
        $script:RaPinnedRelease = $pkg.Manifest
        (Invoke-RaMain (New-InstallOption $pkg $steam)) | Should Be 0
        $snapshot = Get-TreeHash $dir
        (Invoke-RaMain @{ Uninstall = $true; Purge = $true; DryRun = $true; Yes = $true; SteamRoot = $steam }) | Should Be 0
        (Get-TreeHash $dir) -join '|' | Should Be ($snapshot -join '|')
        $plainInstall = @{ Purge = $true; Install = $true; Yes = $true; PackagePath = $pkg.Zip; SteamRoot = $steam }
        (Invoke-RaMain $plainInstall) | Should Be 0
        Test-Path (Get-RaLauncherPath) | Should Be $true
    }
    It '-Disable removes only the Steam launch option part; files stay; -SteamLaunchOption turns it on again' {
        $dir = Reset-Env 'disable'
        $steam = Join-Path $dir 'Steam'
        New-FakeSteam $steam @{ '111' = (New-LocalConfigText (New-AppBlock '"LaunchOptions"		"-debug_mode"')) }
        $cfg = Join-Path $steam 'userdata\111\config\localconfig.vdf'
        $orig = [IO.File]::ReadAllBytes($cfg)
        $pkg = New-TestPackage (Join-Path $dir 'pkg')
        $script:RaPinnedRelease = $pkg.Manifest
        (Invoke-RaMain (New-InstallOption $pkg $steam)) | Should Be 0
        $script:RaGetSteamProcess = { @('fake') }
        (Invoke-RaMain @{ Disable = $true; Yes = $true; SteamRoot = $steam }) | Should Be 1
        $script:RaGetSteamProcess = { @() }
        (Invoke-RaMain @{ Disable = $true; Yes = $true; SteamRoot = $steam }) | Should Be 0
        Test-RaBytesEqual ([IO.File]::ReadAllBytes($cfg)) $orig | Should Be $true
        Test-Path (Get-RaLauncherPath) | Should Be $true
        @((Read-RaState).SteamEntries).Count | Should Be 0
        (Invoke-RaMain @{ SteamLaunchOption = $true; Yes = $true; SteamRoot = $steam }) | Should Be 0
        (Get-RaAppLaunchInfo (Read-RaTextFile $cfg).Text).Value | Should Match 'RenameAnythingLauncher\.exe" %command% -debug_mode$'
    }
}

Describe 'Terminal interface (irm | iex without switches)' {
    It 'opens only for an interactive console without action switches, -Yes or -Plain' {
        [void](Reset-Env 'tui-mode')
        (Get-RaTuiMode @{}).Tui | Should Be $false
        Set-TuiTerminal -Vt
        $m = Get-RaTuiMode @{}
        $m.Tui | Should Be $true
        $m.Color | Should Be $true
        $m.Animate | Should Be $true
        foreach ($o in @(@{ Install = $true }, @{ Uninstall = $true }, @{ Disable = $true }, @{ Yes = $true }, @{ Plain = $true }, @{ CheckUpdate = $true })) {
            (Get-RaTuiMode $o).Tui | Should Be $false
        }
        (Get-RaTuiMode @{ DryRun = $true; SteamRoot = 'x' }).Tui | Should Be $true
    }
    It 'NO_COLOR, TERM=dumb, no VT host turn colour off; RA_NO_ANIMATION, CI, -NoAnimation turn animation off' {
        [void](Reset-Env 'tui-env')
        foreach ($env in @(@{ NO_COLOR = '1' }, @{ TERM = 'dumb' })) {
            Set-TuiTerminal -Vt -Env $env
            $m = Get-RaTuiMode @{}
            $m.Color | Should Be $false
            $m.Animate | Should Be $false
        }
        Set-TuiTerminal
        (Get-RaTuiMode @{}).Color | Should Be $false
        foreach ($env in @(@{ RA_NO_ANIMATION = '1' }, @{ CI = 'true' })) {
            Set-TuiTerminal -Vt -Env $env
            $m = Get-RaTuiMode @{}
            $m.Color | Should Be $true
            $m.Animate | Should Be $false
        }
        Set-TuiTerminal -Vt -Env @{ RA_NO_ANIMATION = '0' }
        (Get-RaTuiMode @{}).Animate | Should Be $true
        Set-TuiTerminal -Vt
        (Get-RaTuiMode @{ NoAnimation = $true }).Animate | Should Be $false
        Set-TuiTerminal -Vt -Width 300
        (Get-RaTuiMode @{}).Width | Should Be 78
        Set-TuiTerminal -Vt -Width 20
        (Get-RaTuiMode @{}).Width | Should Be 44
    }
    # The mutation test runs a copy outside the repository; there the token file is absent.
    $tokensPath = Join-Path $here '..\..\docs\design\tokens.json'
    It 'palette equals docs/design/tokens.json' -Skip:(-not (Test-Path -LiteralPath $tokensPath)) {
        $tokens = Get-Content -LiteralPath $tokensPath -Raw | ConvertFrom-Json
        $flat = @{}
        foreach ($group in $tokens.color.PSObject.Properties) {
            foreach ($t in $group.Value.PSObject.Properties) { $flat[$t.Name] = $t.Value.'$value' }
        }
        foreach ($name in $script:RaTuiPalette.Keys) { $script:RaTuiPalette[$name] | Should Be $flat[$name] }
        foreach ($style in $script:RaTuiStatusStyles.Values) { $script:RaTuiPalette.Contains($style) | Should Be $true }
    }
    It 'map sheet: equal row widths and the flag stands in grid square C3' {
        $model = Get-RaTuiMapModel
        $lines = @(foreach ($line in $model) { (@($line) | ForEach-Object { $_.T }) -join '' })
        @($lines | Select-Object -Skip 1 | ForEach-Object { $_.Length } | Sort-Object -Unique).Count | Should Be 1
        $flagRow = $lines | Where-Object { $_ -match '##==' }
        $flagRow | Should Match '^3 '
        $column = $flagRow.IndexOf('|', 3)
        $script:RaTuiMapHeader.Substring($column - 2, 5).Trim() | Should Be 'C'
        @($lines | Where-Object { $_ -match '[^\x20-\x7E]' }).Count | Should Be 0
    }
    It 'splash: animated = one frame per character then the full name and a still hold; static = one frame, no ANSI without colour' {
        [void](Reset-Env 'tui-splash')
        $animated = [pscustomobject]@{ Tui = $true; Color = $true; Vt = $true; Animate = $true; Width = 78 }
        $frames = Show-RaTuiSplash $animated
        $frames | Should Be ($script:RaTuiAnnotationName.Length + 1)
        $script:sleeps.Count | Should Be ($script:RaTuiAnnotationName.Length + 1)
        $script:sleeps[-1] | Should Be $script:RaTuiHoldDelayMs
        (Get-PlainOutput) | Should Match 'C3   Rename Anything<====\]'
        $script:rawOut.ToString() | Should Match ([regex]::Escape("$([char]27)[38;2;"))
        [void](Reset-Env 'tui-splash-static')
        $static = [pscustomobject]@{ Tui = $true; Color = $false; Vt = $true; Animate = $false; Width = 78 }
        Show-RaTuiSplash $static | Should Be 1
        $script:sleeps.Count | Should Be 0
        $script:rawOut.ToString().Contains([string][char]27) | Should Be $false
        $script:rawOut.ToString() | Should Match "Built on CAPIE \(Cyprian's API Extender\)"
    }
    It 'every rendered line fits the panel width, and coloured lines end with a reset' {
        [void](Reset-Env 'tui-width')
        foreach ($color in $true, $false) {
            $mode = [pscustomobject]@{ Tui = $true; Color = $color; Vt = $true; Animate = $false; Width = 44 }
            foreach ($pass in 1, 2) {
                [void]$pass
                $script:rawOut.Clear() | Out-Null
                [void](Show-RaTuiSplash $mode)
                Write-RaTuiModel (Get-RaTuiMenuModel 2 $mode) $mode
                Write-RaTuiModel (Get-RaTuiStatusModel 'FAIL' ('x' * 90 + ' ' + (Get-RaText 'TuiMpNote')) $mode) $mode
                Write-RaTuiModel (Get-RaTuiSummaryModel @(, @('Log', 'C:\Users\Someone\AppData\Local\RenameAnything\bootstrap.log')) $mode) $mode
                foreach ($line in (Get-PlainOutput).Split("`n")) { $line.Length | Should Not BeGreaterThan 44 }
                if ($color) {
                    foreach ($line in $script:rawOut.ToString().TrimEnd("`n").Split("`n")) { $line.EndsWith("$([char]27)[0m") | Should Be $true }
                }
            }
        }
    }
    It 'status lines follow the CLI pattern and read without colour' {
        $mode = [pscustomobject]@{ Tui = $true; Color = $false; Vt = $false; Animate = $false; Width = 60 }
        $lines = Get-RaTuiStatusModel 'OK' 'CAPIE launcher installed' $mode
        (Format-RaTuiLine $lines[0] $mode) | Should Be '  OK    CAPIE launcher installed'
        $wrapped = Get-RaTuiStatusModel 'NOTE' ('word ' * 30) $mode
        $wrapped.Count | Should BeGreaterThan 1
        (Format-RaTuiLine $wrapped[1] $mode) | Should Match '^ {8}word'
        @(Split-RaTuiText 'C:\a\very\long\path\without\spaces' 10) -join '|' | Should Be 'C:\a\very\|long\path\|without\sp|aces'
    }
    It 'menu: arrows wrap around, digits select, Enter confirms, Esc and Q quit' {
        [void](Reset-Env 'tui-menu')
        $vt = [pscustomobject]@{ Tui = $true; Color = $true; Vt = $true; Animate = $false; Width = 78 }
        Set-TuiKeyQueue 'Up', 'Enter'
        Select-RaTuiAction $vt | Should Be 'Quit'
        Set-TuiKeyQueue 'Down', 'Down', 'Enter'
        Select-RaTuiAction $vt | Should Be 'Disable'
        Set-TuiKeyQueue '4', 'Enter'
        Select-RaTuiAction $vt | Should Be 'Uninstall'
        Set-TuiKeyQueue 'Escape'
        Select-RaTuiAction $vt | Should Be 'Quit'
        Set-TuiKeyQueue 'q'
        Select-RaTuiAction $vt | Should Be 'Quit'
        $narrow = [pscustomobject]@{ Tui = $true; Color = $true; Vt = $true; Animate = $false; Width = 44 }
        $rows = Get-RaTuiMenuModel 0 $narrow
        @($rows).Count | Should BeGreaterThan $script:RaTuiMenuActions.Count
        [void]$script:rawOut.Clear()
        Set-TuiKeyQueue 'Down', 'Enter'
        Select-RaTuiAction $narrow | Should Be 'Update'
        $script:rawOut.ToString().Contains("$([char]27)[$(@($rows).Count)A") | Should Be $true
        $plain = [pscustomobject]@{ Tui = $true; Color = $false; Vt = $false; Animate = $false; Width = 78 }
        Set-TuiKeyQueue '2'
        Select-RaTuiAction $plain | Should Be 'Update'
        (Get-PlainOutput) | Should Match '> 1  Install'
    }
    It 'Install through the interface: user folder, Steam launch option after consent, summary; adapters restored' {
        $dir = Reset-Env 'tui-install'
        $steam = Join-Path $dir 'Steam'
        New-FakeSteam $steam @{ '111' = (New-LocalConfigText (New-AppBlock '"LaunchOptions"		"-debug_mode"')) }
        $pkg = New-TestPackage (Join-Path $dir 'pkg')
        $script:RaPinnedRelease = $pkg.Manifest
        $gameBefore = Get-TreeHash (Join-Path $steam 'steamapps')
        Set-TuiTerminal -Vt -Env @{ RA_NO_ANIMATION = '1' }
        Set-TuiKeyQueue 'Enter', 'Enter'
        $hostBefore = $script:RaWriteHost
        $script:RaAssumeYes = $false
        (Invoke-RaMain @{ PackagePath = $pkg.Zip; SteamRoot = $steam }) | Should Be 0
        $script:keyQueue.Count | Should Be 0
        $out = Get-PlainOutput
        $out | Should Match 'NOTE  CAPIE is not installed\.'
        $out | Should Match 'ASK   Add the CAPIE launcher'
        $out | Should Match 'OK    Package RA-0\.1\.0-none\.zip: SHA256 matches the manifest\.'
        $out | Should Match 'Summary'
        $out | Should Match 'Game folder\s+unchanged'
        $out | Should Not Match 'WARNING: while the extension'
        $launch = (Get-RaAppLaunchInfo (Read-RaTextFile (Join-Path $steam 'userdata\111\config\localconfig.vdf')).Text).Value
        $launch | Should Match 'RenameAnythingLauncher\.exe" %command% -debug_mode$'
        (Read-RaState).LoadMethod | Should Be 'Launcher'
        (Get-TreeHash (Join-Path $steam 'steamapps')) -join '|' | Should Be ($gameBefore -join '|')
        $script:RaWriteHost | Should Be $hostBefore
        $script:RaLogSink | Should Be $null
        $script:RaAssumeYes | Should Be $false
    }
    It 'Uninstall with purge through the interface leaves nothing; Esc and Quit change nothing' {
        $dir = Reset-Env 'tui-uninstall'
        $steam = Join-Path $dir 'Steam'
        New-FakeSteam $steam @{ '111' = (New-LocalConfigText (New-AppBlock '"LaunchOptions"		"-debug_mode"')) }
        $pkg = New-TestPackage (Join-Path $dir 'pkg')
        $script:RaPinnedRelease = $pkg.Manifest
        $before = @(Get-TreeHash $dir | Where-Object { $_ -notmatch '\\pkg\\' })
        (Invoke-RaMain (New-InstallOption $pkg $steam)) | Should Be 0
        $installed = Get-TreeHash $dir
        Set-TuiTerminal -Vt -Env @{ NO_COLOR = '1' }
        Set-TuiKeyQueue 'Escape'
        (Invoke-RaMain @{ SteamRoot = $steam }) | Should Be 0
        Set-TuiKeyQueue '4', 'Enter', 'n'
        (Invoke-RaMain @{ SteamRoot = $steam }) | Should Be 0
        $noLog = { param($tree) @($tree | Where-Object { $_ -notmatch 'bootstrap\.log=' }) -join '|' }
        (& $noLog (Get-TreeHash $dir)) | Should Be (& $noLog $installed)
        (Get-PlainOutput) | Should Match 'Cancelled\. Nothing was changed\.'
        Set-TuiKeyQueue 'Down', 'Down', 'Down', 'Enter', 'y', 'y'
        (Invoke-RaMain @{ SteamRoot = $steam }) | Should Be 0
        (Get-PlainOutput) | Should Match 'Uninstall  remove CAPIE'
        Test-Path $script:RaRoot | Should Be $false
        @(Get-TreeHash $dir | Where-Object { $_ -notmatch '\\pkg\\' }) -join '|' | Should Be ($before -join '|')
    }
    It 'a failing action shows FAIL and the log path; the exit code is 1' {
        [void](Reset-Env 'tui-fail')
        Set-TuiTerminal -Vt
        Set-TuiKeyQueue 'Down', 'Enter'
        (Invoke-RaMain @{ SteamRoot = '' }) | Should Be 1
        $out = Get-PlainOutput
        $out | Should Match 'FAIL  Update channel is not configured'
        $out | Should Match 'Details are in the log'
        $flat = $out -replace '\s', ''
        $flat.Contains([string](Get-RaLogPath)) | Should Be $true
    }
}

Describe 'Default method and the "CAPIE" switch mod in the player''s mod folder' {
    It 'without -LoadMethod a fresh install uses the launcher (nothing in the game folder); -Method Proxy is the alias' {
        $dir = Reset-Env 'default-launcher'
        $steam = Join-Path $dir 'Steam'
        New-FakeSteam $steam @{ '111' = (New-LocalConfigText (New-AppBlock $null)) }
        $pkg = New-TestPackage (Join-Path $dir 'pkg') -WithProxy
        $script:RaPinnedRelease = $pkg.Manifest
        $gameBefore = Get-TreeHash (Get-RaGameDir $steam)
        (Invoke-RaMain @{ Install = $true; Yes = $true; SteamRoot = $steam; PackagePath = $pkg.Zip }) | Should Be 0
        (Read-RaState).LoadMethod | Should Be 'Launcher'
        (Get-TreeHash (Get-RaGameDir $steam)) -join '|' | Should Be ($gameBefore -join '|')
        $scriptInfo = $ExecutionContext.InvokeCommand.GetCommand($scriptPath, [System.Management.Automation.CommandTypes]::ExternalScript)
        $param = $scriptInfo.Parameters['LoadMethod']
        $param.Aliases -contains 'Method' | Should Be $true
    }
    It '-Install places the mod from the verified bundle; -Uninstall removes it and leaves the mod folder as before' {
        $dir = Reset-Env 'mod-install'
        $steam = Join-Path $dir 'Steam'
        New-FakeSteam $steam @{ '111' = (New-LocalConfigText (New-AppBlock $null)) }
        $pkg = New-TestPackage (Join-Path $dir 'pkg') -WithMod
        $script:RaPinnedRelease = $pkg.Manifest
        $modRoot = Get-RaPlaysetModRoot
        Write-Utf8 (Join-Path $modRoot 'other.mod') 'someone else'
        $before = Get-TreeHash (Join-Path $dir 'Documents')
        (Invoke-RaMain @{ Install = $true; Yes = $true; SteamRoot = $steam; PackagePath = $pkg.Zip }) | Should Be 0
        [IO.File]::ReadAllText((Join-Path $modRoot 'capie\descriptor.mod')) | Should Match 'version="0.1.0"'
        Test-Path (Join-Path $modRoot 'capie\interface\frontendmainview.gui') | Should Be $true
        Test-Path (Join-Path $modRoot 'capie.mod') | Should Be $true
        @((Read-RaState).ModFiles).Count | Should Be 3
        (Invoke-RaMain @{ Uninstall = $true; Yes = $true; SteamRoot = $steam }) | Should Be 0
        (Get-TreeHash (Join-Path $dir 'Documents')) -join '|' | Should Be ($before -join '|')
        Test-Path (Join-Path $modRoot 'capie') | Should Be $false
        Test-Path $modRoot | Should Be $true
    }
    It 'an existing capie.mod is backed up with a byte-verified copy and restored by -Uninstall' {
        $dir = Reset-Env 'mod-foreign'
        $steam = Join-Path $dir 'Steam'
        New-FakeSteam $steam @{ '111' = (New-LocalConfigText (New-AppBlock $null)) }
        $pkg = New-TestPackage (Join-Path $dir 'pkg') -WithMod
        $script:RaPinnedRelease = $pkg.Manifest
        $existing = Join-Path (Get-RaPlaysetModRoot) 'capie.mod'
        Write-Utf8 $existing 'name="My own CAPIE build"'
        $before = Get-TreeHash (Join-Path $dir 'Documents')
        (Invoke-RaMain @{ Install = $true; Yes = $true; SteamRoot = $steam; PackagePath = $pkg.Zip }) | Should Be 0
        [IO.File]::ReadAllText($existing) | Should Match 'path="mod/capie"'
        $record = @((Read-RaState).ModFiles) | Where-Object { $_.Path -eq $existing }
        [IO.File]::ReadAllText($record.Backup) | Should Be 'name="My own CAPIE build"'
        (Invoke-RaMain @{ Uninstall = $true; Purge = $true; Yes = $true; SteamRoot = $steam }) | Should Be 0
        (Get-TreeHash (Join-Path $dir 'Documents')) -join '|' | Should Be ($before -join '|')
    }
    It 'a mod file changed by the player after installation is kept; -DryRun writes nothing' {
        $dir = Reset-Env 'mod-changed'
        $steam = Join-Path $dir 'Steam'
        New-FakeSteam $steam @{ '111' = (New-LocalConfigText (New-AppBlock $null)) }
        $pkg = New-TestPackage (Join-Path $dir 'pkg') -WithMod
        $script:RaPinnedRelease = $pkg.Manifest
        $snapshot = Get-TreeHash $dir
        (Invoke-RaMain @{ Install = $true; DryRun = $true; Yes = $true; SteamRoot = $steam; PackagePath = $pkg.Zip }) | Should Be 0
        (Get-TreeHash $dir) -join '|' | Should Be ($snapshot -join '|')
        (Invoke-RaMain @{ Install = $true; Yes = $true; SteamRoot = $steam; PackagePath = $pkg.Zip }) | Should Be 0
        $descriptor = Join-Path (Get-RaPlaysetModRoot) 'capie\descriptor.mod'
        Write-Utf8 $descriptor 'edited by the player'
        (Invoke-RaMain @{ Uninstall = $true; Yes = $true; SteamRoot = $steam }) | Should Be 0
        [IO.File]::ReadAllText($descriptor) | Should Be 'edited by the player'
        Test-Path (Join-Path (Get-RaPlaysetModRoot) 'capie.mod') | Should Be $false
    }
    It '-Update replaces our mod files with the new release; -Rollback brings the old ones back' {
        $dir = Reset-Env 'mod-update'
        $steam = Join-Path $dir 'Steam'
        New-FakeSteam $steam @{ '111' = (New-LocalConfigText (New-AppBlock $null)) }
        $key = Join-Path $dir 'update.private.xml'
        $script:RaUpdatePublicKeyXml = (& $signScript -NewKey -PrivateKeyPath $key -KeySize 2048) | Out-String
        $script:RaGitHubRepo = 'test-owner/test-repo'
        $p1 = New-TestPackage (Join-Path $dir 'v1') -Version '0.1.0' -WithMod
        $script:RaPinnedRelease = $p1.Manifest
        (Invoke-RaMain @{ Install = $true; Yes = $true; SteamRoot = $steam; PackagePath = $p1.Zip }) | Should Be 0
        $descriptor = Join-Path (Get-RaPlaysetModRoot) 'capie\descriptor.mod'
        [IO.File]::ReadAllText($descriptor) | Should Match '0\.1\.0'
        [void](New-SignedRelease -Dir $dir -Version '0.2.0' -KeyPath $key -WithMod)
        (Invoke-RaMain @{ Update = $true; Yes = $true; SteamRoot = $steam }) | Should Be 0
        [IO.File]::ReadAllText($descriptor) | Should Match '0\.2\.0'
        (Invoke-RaMain @{ Rollback = $true; Yes = $true; SteamRoot = $steam }) | Should Be 0
        [IO.File]::ReadAllText($descriptor) | Should Match '0\.1\.0'
    }
}
