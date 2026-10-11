# Offline tests for tools/bootstrap/install.ps1.
# Pester 3.4 syntax (the version bundled with Windows); also runs on PowerShell 7.
#   Invoke-Pester -Script .\tools\bootstrap\install.Tests.ps1
# Work directory: $env:RA_BOOTSTRAP_TEST_ROOT (default: %TEMP%\ra-bootstrap-tests).
# Optional: $env:RA_BOOTSTRAP_REAL_LOCALCONFIG = path to a COPY of a real localconfig.vdf.
# Nothing outside the work directory is written; Steam/game are never touched.

$here = Split-Path -Parent $MyInvocation.MyCommand.Path
$scriptPath = Join-Path $here 'install.ps1'
. $scriptPath

$testRoot = if ($env:RA_BOOTSTRAP_TEST_ROOT) { $env:RA_BOOTSTRAP_TEST_ROOT } else { Join-Path ([IO.Path]::GetTempPath()) 'ra-bootstrap-tests' }
$script:RaQuiet = $true
$L = 'C:\Users\Test User\AppData\Local\RenameAnything\0.1.0\RenameAnythingLauncher.exe'
$LOld = 'C:\Users\Test User\AppData\Local\RenameAnything\0.0.9\RenameAnythingLauncher.exe'
$utf8 = New-Object System.Text.UTF8Encoding($false)

function Assert-RaThrows {
    <# Pester 3.4 "Should Throw" does not work on PowerShell 7, so use our own. #>
    param([scriptblock]$Block, [string]$MessagePart)
    $threw = $false; $msg = ''
    try { & $Block } catch { $threw = $true; $msg = $_.Exception.Message }
    $threw | Should Be $true
    if ($MessagePart) { $msg.Contains($MessagePart) | Should Be $true }
}

function New-LocalConfigText {
    param([string]$AppBlock, [string]$Nl = "`n", [switch]$NoApp)
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
    param([string]$LaunchOptionsLine)
    $lo = if ($null -ne $LaunchOptionsLine) { "`t`t`t`t`t`t$LaunchOptionsLine`n" } else { '' }
    return "`t`t`t`t`t`"394360`"`n`t`t`t`t`t{`n`t`t`t`t`t`t`"LastPlayed`"`t`t`"1791737197`"`n`t`t`t`t`t`t`"cloud`"`n`t`t`t`t`t`t{`n`t`t`t`t`t`t`t`"last_sync_state`"`t`t`"synchronized`"`n`t`t`t`t`t`t}`n$lo`t`t`t`t`t}`n"
}

function Write-Utf8 { param($Path, $Text) [IO.File]::WriteAllText($Path, $Text, $utf8) }

function New-FakeSteam {
    param([string]$Root, [hashtable]$Accounts)
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
    Write-Utf8 (Join-Path $Root 'steamapps\libraryfolders.vdf') "`"libraryfolders`"`n{`n`t`"0`"`n`t{`n`t`t`"path`"`t`t`"$esc`"`n`t`t`"apps`"`n`t`t{`n`t`t`t`"394360`"`t`t`"1`"`n`t`t}`n`t}`n}`n"
    Write-Utf8 (Join-Path $Root 'steamapps\appmanifest_394360.acf') "`"AppState`"`n{`n`t`"appid`"`t`t`"394360`"`n`t`"installdir`"`t`t`"Hearts of Iron IV`"`n}`n"
    $game = Join-Path $Root 'steamapps\common\Hearts of Iron IV'
    Copy-Item -LiteralPath (Join-Path $env:WINDIR 'System32\notepad.exe') -Destination (Join-Path $game 'hoi4.exe')
    Write-Utf8 (Join-Path $game 'dowser.exe') 'dummy'
}

function New-TestPackage {
    <# Returns @{ Zip; Manifest } with real hashes. $Tamper: none|filehash|extra|zipslip|missing #>
    param([string]$Dir, [string]$Tamper = 'none', [string]$Version = '0.1.0')
    $src = Join-Path $Dir 'pkgsrc'
    New-Item -ItemType Directory -Force -Path (Join-Path $src 'runtime') | Out-Null
    $files = [ordered]@{
        'RenameAnythingLauncher.exe'          = 'launcher-bytes'
        'LICENSE.MinHook.txt'                 = 'license'
        'runtime/rename_anything_country.dll' = 'dll-bytes'
    }
    $manifest = [ordered]@{ Version = $Version; ZipName = "RA-$Version.zip"; ZipSha256 = ''; Files = [ordered]@{} }
    foreach ($k in $files.Keys) {
        $bytes = $utf8.GetBytes($files[$k])
        $manifest.Files[$k] = Get-RaSha256OfBytes $bytes
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
            $s = $e.Open(); $b = $utf8.GetBytes($content); $s.Write($b, 0, $b.Length); $s.Dispose()
        }
        if ($Tamper -eq 'extra') { $e = $z.CreateEntry('runtime/extra.dll'); $s = $e.Open(); $s.WriteByte(1); $s.Dispose() }
        if ($Tamper -eq 'zipslip') { $e = $z.CreateEntry('../evil.txt'); $s = $e.Open(); $s.WriteByte(1); $s.Dispose() }
    } finally { $z.Dispose() }
    $manifest.ZipSha256 = (Get-FileHash -LiteralPath $zip -Algorithm SHA256).Hash
    return @{ Zip = $zip; Manifest = $manifest }
}

function Reset-Env {
    param([string]$Name)
    $dir = Join-Path $testRoot $Name
    if (Test-Path $dir) { Remove-Item -LiteralPath $dir -Recurse -Force }
    New-Item -ItemType Directory -Force -Path $dir | Out-Null
    $script:RaRoot = Join-Path $dir 'LocalAppData\RenameAnything'
    $script:RaDry = $false
    $script:RaAssumeYes = $true
    $script:RaQuiet = $true
    $script:RaLang = 'en'
    $script:RaAfterWriteHook = $null
    $script:RaGetSteamProcess = { @() }
    $desk = Join-Path $dir 'Desktop'
    New-Item -ItemType Directory -Force -Path $desk | Out-Null
    $script:RaDesktopPath = [scriptblock]::Create("'$desk'")
    $script:RaReadHost = { param($p) throw "Unexpected prompt: $p" }
    return $dir
}

function Get-TreeHashes {
    param([string]$Dir)
    if (-not (Test-Path $Dir)) { return @() }
    return @(Get-ChildItem -LiteralPath $Dir -Recurse -File | Sort-Object FullName | ForEach-Object { $_.FullName + '=' + (Get-FileHash -LiteralPath $_.FullName).Hash })
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
    It 'decodes Polish messages' {
        $script:RaLang = 'pl'
        (Get-RaText 'MpWarning') | Should Match 'OSTRZE\u017bENIE'.Replace('\u017b', [string][char]0x17B)
        (Get-RaText 'MpWarning') | Should Match 'wieloosobowym'
        $script:RaLang = 'en'
        (Get-RaText 'MpWarning') | Should Match 'NOT declared multiplayer-compatible'
    }
    It 'Assert-RaThrows itself fails for a non-throwing block (negative control)' {
        $failed = $false
        try { Assert-RaThrows { 1 } } catch { $failed = $true }
        $failed | Should Be $true
    }
    It 'runs as a downloaded scriptblock (irm-style) in a child process: -Uninstall -DryRun writes nothing' {
        $dir = Reset-Env 'child-scriptblock'
        $steam = Join-Path $dir 'Steam'
        New-FakeSteam $steam @{ '111' = (New-LocalConfigText (New-AppBlock '"LaunchOptions"		"-debug_mode"')) }
        $before = Get-TreeHashes $dir
        $exe = (Get-Process -Id $PID).Path
        $cmd = "`$env:LOCALAPPDATA = '$dir\LocalAppData'; `$t = [IO.File]::ReadAllText('$scriptPath'); & ([scriptblock]::Create(`$t)) -Uninstall -DryRun -Yes -SteamRoot '$steam' -Language en; 'CHILD-EXIT-OK'"
        $out = & $exe -NoProfile -NonInteractive -Command $cmd 2>&1 | Out-String
        $out | Should Match 'CHILD-EXIT-OK'
        $out | Should Match 'DRY RUN'
        (Get-TreeHashes $dir) -join '|' | Should Be ($before -join '|')
    }
    It 'has a single placeholder release URL constant and refuses an unpinned manifest' {
        $script:RaReleaseBaseUrl | Should Match 'PLACEHOLDER'
        Assert-RaThrows { Test-RaPinnedManifest $script:RaPinnedRelease } 'not configured'
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
        Assert-RaThrows { ConvertFrom-RaVdf ((New-LocalConfigText (New-AppBlock $null)) + "}`n") }
        Assert-RaThrows { ConvertFrom-RaVdf ((New-LocalConfigText (New-AppBlock $null)).TrimEnd().TrimEnd('}')) }
    }
    It 'rejects an unterminated string' {
        Assert-RaThrows { ConvertFrom-RaVdf "`"a`"`n{`n`t`"b`"`t`t`"oops`n}`n" }
    }
    It 'rejects a truncated (corrupted) file' {
        $t = New-LocalConfigText (New-AppBlock $null)
        Assert-RaThrows { Get-RaAppLaunchInfo $t.Substring(0, [int]($t.Length / 2)) }
    }
    It 'rejects a file without the apps block' {
        Assert-RaThrows { Get-RaAppLaunchInfo "`"UserLocalConfigStore`"`n{`n`t`"x`"`t`t`"1`"`n}`n" } 'No UserLocalConfigStore'
    }
    It 'rejects duplicate 394360 blocks' {
        $b = New-AppBlock $null
        Assert-RaThrows { Get-RaAppLaunchInfo (New-LocalConfigText ($b + $b)) } 'Multiple'
    }
    It 'rejects duplicate LaunchOptions keys' {
        $b = New-AppBlock ("`"LaunchOptions`"`t`t`"a`"`n`t`t`t`t`t`t`"LaunchOptions`"`t`t`"b`"")
        Assert-RaThrows { Get-RaAppLaunchInfo (New-LocalConfigText $b) } 'Multiple LaunchOptions'
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
    It 'refuses two %command%' { Assert-RaThrows { Add-RaWrapper '%command% %command%' $L } }
    It 'refuses a launcher path with quotes' { Assert-RaThrows { Add-RaWrapper '' 'C:\a"b\RenameAnythingLauncher.exe' } }
    It 'removes only our fragment (flags)' { Remove-RaWrapper (Add-RaWrapper '-debug_mode' $L) | Should Be '-debug_mode' }
    It 'removes to empty' { Remove-RaWrapper (Add-RaWrapper '' $L) | Should Be '' }
    It 'removes and keeps another wrapper' { Remove-RaWrapper (Add-RaWrapper '"C:\Tools\other.exe" %command% -x' $L) | Should Be '"C:\Tools\other.exe" %command% -x' }
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
        $r.Text | Should Match ([regex]::Escape('"\"C:\\Users\\Test User\\AppData\\Local\\RenameAnything\\0.1.0\\RenameAnythingLauncher.exe\" %command% -debug_mode"'))
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
        Assert-RaThrows { Update-RaLocalConfig -ConfigPath $p -Compute { param($o) Add-RaWrapper $o $L } } 'restored from backup'
        Test-RaBytesEqual ([IO.File]::ReadAllBytes($p)) $orig | Should Be $true
        $script:RaAfterWriteHook = $null
    }
    It 'refuses to edit a corrupted file and leaves it untouched' {
        $dir = Reset-Env 'file-corrupt'
        $p = Join-Path $dir 'localconfig.vdf'
        $t = New-LocalConfigText (New-AppBlock $null)
        Write-Utf8 $p $t.Substring(0, $t.Length - 3)
        $orig = [IO.File]::ReadAllBytes($p)
        Assert-RaThrows { Update-RaLocalConfig -ConfigPath $p -Compute { param($o) 'x' } }
        Test-RaBytesEqual ([IO.File]::ReadAllBytes($p)) $orig | Should Be $true
        @(Get-ChildItem $dir -Filter '*.renameanything-*').Count | Should Be 0
    }
    It 'refuses invalid UTF-8' {
        $dir = Reset-Env 'file-utf8'
        $p = Join-Path $dir 'localconfig.vdf'
        [IO.File]::WriteAllBytes($p, [byte[]](0x22, 0xC3, 0x28, 0x22))
        Assert-RaThrows { Read-RaTextFile $p } 'UTF-8'
    }
}

Describe 'Steam process, accounts and end-to-end flows' {
    It 'Steam running + -Yes: refuses, never writes' {
        $dir = Reset-Env 'steam-running'
        $steam = Join-Path $dir 'Steam'
        New-FakeSteam $steam @{ '111' = (New-LocalConfigText (New-AppBlock '"LaunchOptions"		"-debug_mode"')) }
        $before = Get-TreeHashes $steam
        $script:RaGetSteamProcess = { @('fake-steam') }
        $state = Read-RaState
        Assert-RaThrows { Set-RaSteamLaunchOption $state $steam '' $L } 'Steam is running'
        (Get-TreeHashes $steam) -join '|' | Should Be ($before -join '|')
    }
    It 'Steam running interactive: Q skips, Enter after closing proceeds' {
        [void](Reset-Env 'steam-wait')
        $script:RaAssumeYes = $false
        $script:RaGetSteamProcess = { @('fake-steam') }
        $script:RaReadHost = { param($p) 'q' }
        Wait-RaSteamClosed | Should Be $false
        $script:steamCalls = 0
        $script:RaGetSteamProcess = { $script:steamCalls++; if ($script:steamCalls -le 2) { @('fake') } else { @() } }
        $script:RaReadHost = { param($p) '' }
        Wait-RaSteamClosed | Should Be $true
    }
    It 'multiple accounts: -Yes without selector fails; selector and interactive choice work' {
        $dir = Reset-Env 'accounts'
        $steam = Join-Path $dir 'Steam'
        $cfg = New-LocalConfigText (New-AppBlock $null)
        New-FakeSteam $steam @{ '111' = $cfg; '222' = $cfg }
        $acc = @(Get-RaSteamAccounts $steam)
        $acc.Count | Should Be 2
        ($acc | Where-Object Id -eq '222').Name | Should Be 'Persona 222'
        Assert-RaThrows { Select-RaSteamAccounts $acc '' } 'Several Steam accounts'
        @(Select-RaSteamAccounts $acc 'all').Count | Should Be 2
        @(Select-RaSteamAccounts $acc '222')[0].Id | Should Be '222'
        Assert-RaThrows { Select-RaSteamAccounts $acc '999' } 'Unknown'
        $script:RaAssumeYes = $false
        $script:RaReadHost = { param($p) '2' }
        $sel = @(Select-RaSteamAccounts $acc '')
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
            Assert-RaThrows { Install-RaPackage $state $pkg.Zip }
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
        $gameBefore = Get-TreeHashes (Join-Path $steam 'steamapps')

        $code = Invoke-RaMain @{ Install = $true; SteamLaunchOption = $true; ShortcutPath = $custom; Yes = $true; PackagePath = $pkg.Zip; SteamRoot = $steam; Language = 'en' }
        $code | Should Be 0
        $launcher = Join-Path $script:RaRoot '0.1.0\RenameAnythingLauncher.exe'
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
        (Get-TreeHashes (Join-Path $steam 'steamapps')) -join '|' | Should Be ($gameBefore -join '|')
        Test-Path (Join-Path $script:RaRoot 'bootstrap.log') | Should Be $true

        # A foreign shortcut with the same name elsewhere must survive uninstall.
        $desk = & $script:RaDesktopPath
        $foreign = Join-Path $desk $script:RaShortcutFileName
        $sh = New-Object -ComObject WScript.Shell; $s = $sh.CreateShortcut($foreign); $s.TargetPath = (Join-Path $env:WINDIR 'System32\notepad.exe'); $s.Save()
        # Uninstall with Steam running must not remove anything.
        $script:RaGetSteamProcess = { @('fake') }
        (Invoke-RaMain @{ Uninstall = $true; Yes = $true; SteamRoot = $steam; Language = 'en' }) | Should Be 1
        Test-Path $launcher | Should Be $true
        Test-Path $lnk | Should Be $true
        $script:RaGetSteamProcess = { @() }
        (Invoke-RaMain @{ Uninstall = $true; Yes = $true; SteamRoot = $steam; Language = 'en' }) | Should Be 0
        Test-RaBytesEqual ([IO.File]::ReadAllBytes($cfg)) $origCfg | Should Be $true
        Test-Path $lnk | Should Be $false
        Test-Path $foreign | Should Be $true
        @(Get-ChildItem $script:RaRoot -Force | ForEach-Object Name) -join ',' | Should Be 'bootstrap.log'
        (Get-TreeHashes (Join-Path $steam 'steamapps')) -join '|' | Should Be ($gameBefore -join '|')
    }

    It 'uninstall without state removes only our fragment (user edited options afterwards)' {
        $dir = Reset-Env 'uninstall-nostate'
        $steam = Join-Path $dir 'Steam'
        $val = ConvertTo-RaVdfString ((Add-RaWrapper '-debug_mode' $L) + ' -nomusic')
        New-FakeSteam $steam @{ '111' = (New-LocalConfigText (New-AppBlock ('"LaunchOptions"		"' + $val + '"'))); '222' = (New-LocalConfigText (New-AppBlock '"LaunchOptions"		"-x"')) }
        $cfg222 = Join-Path $steam 'userdata\222\config\localconfig.vdf'
        $orig222 = [IO.File]::ReadAllBytes($cfg222)
        (Invoke-RaMain @{ RemoveSteamLaunchOption = $true; Yes = $true; SteamRoot = $steam; Language = 'en' }) | Should Be 0
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
        (Invoke-RaMain @{ Install = $true; SteamLaunchOption = $true; SteamAccount = 'all'; Yes = $true; PackagePath = $pkg.Zip; SteamRoot = $steam; Language = 'en' }) | Should Be 0
        $launcher = Join-Path $script:RaRoot '0.1.0\RenameAnythingLauncher.exe'
        foreach ($id in '111', '222') {
            (Get-RaAppLaunchInfo (Read-RaTextFile (Join-Path $steam "userdata\$id\config\localconfig.vdf")).Text).Value | Should Be "`"$launcher`" %command%"
        }
        (Invoke-RaMain @{ Uninstall = $true; Yes = $true; SteamRoot = $steam; Language = 'en' }) | Should Be 0
        foreach ($id in '111', '222') {
            (Get-RaAppLaunchInfo (Read-RaTextFile (Join-Path $steam "userdata\$id\config\localconfig.vdf")).Text).Value | Should Be ''
        }
    }

    It 'update to a new version re-points Steam entry and shortcut and prunes the old version' {
        $dir = Reset-Env 'update'
        $steam = Join-Path $dir 'Steam'
        New-FakeSteam $steam @{ '111' = (New-LocalConfigText (New-AppBlock '"LaunchOptions"		"-debug_mode"')) }
        $p1 = New-TestPackage (Join-Path $dir 'v1') -Version '0.1.0'
        $script:RaPinnedRelease = $p1.Manifest
        (Invoke-RaMain @{ Install = $true; SteamLaunchOption = $true; Shortcut = $true; Yes = $true; PackagePath = $p1.Zip; SteamRoot = $steam; Language = 'en' }) | Should Be 0
        $p2 = New-TestPackage (Join-Path $dir 'v2') -Version '0.2.0'
        $script:RaPinnedRelease = $p2.Manifest
        (Invoke-RaMain @{ Install = $true; Yes = $true; PackagePath = $p2.Zip; SteamRoot = $steam; Language = 'en' }) | Should Be 0
        $l2 = Join-Path $script:RaRoot '0.2.0\RenameAnythingLauncher.exe'
        (Get-RaAppLaunchInfo (Read-RaTextFile (Join-Path $steam 'userdata\111\config\localconfig.vdf')).Text).Value | Should Be "`"$l2`" %command% -debug_mode"
        (Get-RaShortcutInfo (Join-Path (& $script:RaDesktopPath) $script:RaShortcutFileName)).TargetPath | Should Be $l2
        Test-Path (Join-Path $script:RaRoot '0.1.0') | Should Be $false
        @(Read-RaState).SteamEntries[0].OriginalValue | Should Be '-debug_mode'
    }

    It 'dry run writes nothing anywhere' {
        $dir = Reset-Env 'dryrun'
        $steam = Join-Path $dir 'Steam'
        New-FakeSteam $steam @{ '111' = (New-LocalConfigText (New-AppBlock '"LaunchOptions"		"-debug_mode"')) }
        $pkg = New-TestPackage $dir
        $script:RaPinnedRelease = $pkg.Manifest
        # Install for real first so the Steam/shortcut steps have a launcher, then dry-run those.
        (Invoke-RaMain @{ Install = $true; Yes = $true; PackagePath = $pkg.Zip; SteamRoot = $steam; Language = 'en' }) | Should Be 0
        $before = Get-TreeHashes $dir
        (Invoke-RaMain @{ SteamLaunchOption = $true; Shortcut = $true; DryRun = $true; Yes = $true; SteamRoot = $steam; Language = 'en' }) | Should Be 0
        (Invoke-RaMain @{ Uninstall = $true; DryRun = $true; Yes = $true; SteamRoot = $steam; Language = 'en' }) | Should Be 0
        (Get-TreeHashes $dir) -join '|' | Should Be ($before -join '|')
    }

    It 'refuses to overwrite a foreign file with the shortcut name' {
        $dir = Reset-Env 'foreign-lnk'
        $steam = Join-Path $dir 'Steam'
        New-FakeSteam $steam @{ '111' = (New-LocalConfigText (New-AppBlock $null)) }
        $pkg = New-TestPackage $dir
        $script:RaPinnedRelease = $pkg.Manifest
        $desk = & $script:RaDesktopPath
        Write-Utf8 (Join-Path $desk $script:RaShortcutFileName) 'not a shortcut'
        (Invoke-RaMain @{ Install = $true; Yes = $true; PackagePath = $pkg.Zip; SteamRoot = $steam; Language = 'en' }) | Should Be 0
        $lnkResult = Invoke-RaMain @{ Shortcut = $true; Yes = $true; SteamRoot = $steam; Language = 'en' }
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
        foreach ($a in 't', '3', 'n', '2', 't') { $script:answers.Enqueue($a) }  # install, both, decline Steam, picker, confirm shortcut
        $script:RaAssumeYes = $false
        $script:RaReadHost = { param($p) $script:answers.Dequeue() }
        (Invoke-RaMain @{ PackagePath = $pkg.Zip; SteamRoot = $steam; Language = 'pl' }) | Should Be 0
        $script:answers.Count | Should Be 0
        Test-Path (Join-Path $picked $script:RaShortcutFileName) | Should Be $true
        Test-RaBytesEqual ([IO.File]::ReadAllBytes($cfg)) $origCfg | Should Be $true
        (Get-RaShortcutInfo (Join-Path $picked $script:RaShortcutFileName)).Description | Should Match 'wieloosobowym'
        $script:RaFolderPicker = $null
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
            $r = Update-RaLocalConfig -ConfigPath $p -Compute { param($o) Add-RaWrapper $o $L }
            $newText = (Read-RaTextFile $p).Text
            $a = $origText -split "`n"; $b = $newText -split "`n"
            $diff = @(Compare-Object $a $b)
            ($diff.Count -le 2) | Should Be $true
            @($diff | Where-Object { $_.InputObject -notmatch '"LaunchOptions"' }).Count | Should Be 0
            [void](Update-RaLocalConfig -ConfigPath $p -Compute { param($o) if ($null -eq $before) { '' } else { $before } })
            if ($null -ne $before) { Test-RaBytesEqual ([IO.File]::ReadAllBytes($p)) $orig | Should Be $true }
        }
    } else {
        It 'skipped (RA_BOOTSTRAP_REAL_LOCALCONFIG not set)' { $true | Should Be $true }
    }
}
