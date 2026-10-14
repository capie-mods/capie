<#
.SYNOPSIS
    Rename Anything (HOI4) bootstrap: installs the native launcher bundle and
    optionally wires it into Steam launch options and/or a game shortcut.

.DESCRIPTION
    Runs on Windows PowerShell 5.1 and PowerShell 7, without administrator rights
    and without external modules. The default Launcher method writes nothing into the HOI4
    installation; the explicit Proxy alternative adds one version.dll there. Saves are
    untouched. See tools/bootstrap/README.md.

    Interactive (no action switches): a terminal interface (menu: install, update, disable,
    uninstall) when the console is interactive, plain prompts otherwise or with -Plain.
        irm <release-url>/install.ps1 | iex
    Noninteractive:
        & ([scriptblock]::Create((irm <release-url>/install.ps1))) -Install -SteamLaunchOption -Yes
        .\install.ps1 -Install -LoadMethod Launcher -Shortcut -ShortcutPath "$env:USERPROFILE\Desktop" -Yes

    This file is intentionally English-only and pure ASCII. ASCII bytes read the same in
    UTF-8, with or without a BOM, and in every legacy code page that Windows PowerShell 5.1
    or `irm` may use, and every message prints the same in any console encoding. The
    script therefore never changes the console encoding of the caller's session.

    install.ps1 is GENERATED from tools/bootstrap/src by tools/bootstrap/build-install.ps1.
    Edit the fragments in src/ and rebuild; never edit install.ps1 by hand.

.PARAMETER Install
    Install or verify the release pinned in this script (with -Latest: the newest signed release).

.PARAMETER Latest
    With -Install: download the newest release from GitHub Releases, verify its signed manifest
    and every SHA-256, and install it into %LOCALAPPDATA%\RenameAnything (never the game folder).

.PARAMETER LoadMethod
    Launcher (default): the CAPIE launcher in the Steam launch options; nothing in the game folder;
    CAPIE loads only when the "CAPIE" mod is enabled in the playset. Proxy (explicit alternative,
    alias -Method): game-local version.dll activated by -capie; it does not read the playset.

.PARAMETER ProxyActivation
    Flag (default): requires -capie. Config: explicit persistent activation fallback.

.PARAMETER Shortcut
    Create the launcher shortcut; requires -LoadMethod Launcher (Desktop by default).

.PARAMETER ShortcutPath
    Folder for the shortcut; implies -Shortcut.

.PARAMETER SteamLaunchOption
    Add the launcher to the HOI4 Steam launch options.

.PARAMETER SteamAccount
    Steam account id(s), comma separated, or "all".

.PARAMETER RemoveSteamLaunchOption
    Remove only our part of the HOI4 Steam launch options.

.PARAMETER Uninstall
    Remove the launch option part, our shortcuts and the installed files (the log is kept).

.PARAMETER Purge
    With -Uninstall: also delete the log and the localconfig.vdf backups this script made, so
    nothing of the installation is left.

.PARAMETER Disable
    Remove only our part of the Steam launch options and the proxy activation marker; files stay.

.PARAMETER NoAnimation
    Terminal interface without the splash animation (also: RA_NO_ANIMATION=1, NO_COLOR, CI).

.PARAMETER Plain
    Plain question-and-answer prompts instead of the terminal interface.

.PARAMETER DryRun
    Show what would happen and write nothing (alias -WhatIf).

.PARAMETER Yes
    Ask no questions; the actions must be given as switches.

.PARAMETER PackagePath
    Already downloaded release ZIP to install instead of downloading it.

.PARAMETER SteamRoot
    Steam folder override (default: HKCU\Software\Valve\Steam\SteamPath).

.PARAMETER CheckUpdate
    Ask the GitHub Releases API for the newest version (one request, on demand only).

.PARAMETER Update
    Install the newest signed release and switch to it.

.PARAMETER Rollback
    Switch back to the previous version kept on disk.

.PARAMETER ManifestPath
    Signed release-manifest.json for an offline -Update or -CheckUpdate.

.OUTPUTS
    System.Int32. Exit code 0 on success, 1 on failure, 2 when no action was given and the
    console cannot answer questions (also the process exit code with -File).

.EXAMPLE
    powershell -ExecutionPolicy Bypass -File .\install.ps1

.EXAMPLE
    .\install.ps1 -Install -SteamLaunchOption -SteamAccount all -Yes
#>
[Diagnostics.CodeAnalysis.SuppressMessageAttribute('RaStrictMode', '',
    Justification = 'Under irm | iex the top level runs in the caller session; Invoke-RaMain enables Set-StrictMode -Version Latest instead.')]
[Diagnostics.CodeAnalysis.SuppressMessageAttribute('PSReviewUnusedParameter', '',
    Justification = 'The parameters are consumed by the entry point at the end of the generated install.ps1 (src/Main.ps1).')]
[CmdletBinding()]
[OutputType([int])]
param(
    [switch]$Install,
    [switch]$Latest,
    [Alias('Method')][ValidateSet('Proxy', 'Launcher')][string]$LoadMethod,
    [ValidateSet('Flag', 'Config')][string]$ProxyActivation,
    [switch]$Shortcut,
    [string]$ShortcutPath,
    [switch]$SteamLaunchOption,
    [string]$SteamAccount,
    [switch]$RemoveSteamLaunchOption,
    [switch]$Uninstall,
    [switch]$Purge,
    [switch]$Disable,
    [switch]$NoAnimation,
    [switch]$Plain,
    [Alias('WhatIf')][switch]$DryRun,
    [switch]$Yes,
    [string]$PackagePath,
    [string]$SteamRoot,
    [switch]$CheckUpdate,
    [switch]$Update,
    [switch]$Rollback,
    [string]$ManifestPath
)
