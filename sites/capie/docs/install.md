---
description: Installing CAPIE as a player. The one-line installer, Steam launch option, desktop shortcut, updates and uninstall.
---

# Install

!!! warning "Available with the first release"

    There is no release yet, so nothing on this page can be installed today. The installer
    script exists and has offline tests, but its release address, pinned package hash and
    signing key are placeholders, and it refuses to install on purpose. The launcher's wrapper
    mode, which the Steam and shortcut options need, is still in progress. This page describes
    how installation will work.

You need Windows, Hearts of Iron IV 1.19.3.0 from Steam, and Windows PowerShell 5.1 (built into
Windows) or PowerShell 7. You do not need administrator rights or extra modules.

## How the game will start

You keep your habits. You press Play in Steam, or double-click a shortcut, then pick your playset
in the Paradox Launcher as usual. The CAPIE launcher sits in between: it checks the game, starts
the Paradox Launcher, and loads CAPIE only into the `hoi4.exe` that this chain started.

<figure class="diagram" markdown="span">
  ![Launch chain: Steam Play or a shortcut starts the CAPIE launcher, which starts the Paradox Launcher. On version 1.19.3.0 CAPIE is loaded into that game only. On an unknown version the game starts without CAPIE.](assets/shared/diagrams/launch-chain.svg)
  <figcaption>Planned launch chain. Built and reviewed, not yet tested end to end.</figcaption>
</figure>

## Install with one line

Recommended: download the script, read it, then run it.

```powershell title="Download, check, run"
# 1. Download. OWNER and TAG are filled in with the first release.
Invoke-WebRequest https://raw.githubusercontent.com/OWNER/hoi4-rename-anything/TAG/tools/bootstrap/install.ps1 -OutFile install.ps1
# 2. Read it (notepad install.ps1) and compare its SHA256 with the release notes.
Get-FileHash .\install.ps1
# 3. Run it. It asks before it changes anything.
powershell -ExecutionPolicy Bypass -File .\install.ps1
```

The one-liner does the same in a single step:

```powershell title="One line (convenient, less safe)"
irm https://raw.githubusercontent.com/OWNER/hoi4-rename-anything/TAG/tools/bootstrap/install.ps1 | iex
```

!!! warning "What `irm | iex` means"

    Whatever the address serves at that moment runs right away with your user rights. You do not
    see it first and nothing checks its hash. Everything the script downloads afterwards is
    verified, but nothing can verify the script that `iex` runs. Downloading and reading it
    first is the real protection.

The script asks:

1. Whether to install the launcher and CAPIE into `%LOCALAPPDATA%\RenameAnything\<version>`.
2. How to start the game with CAPIE: Steam launch option, shortcut, both, or neither.
3. For Steam and shortcuts, a warning that the game with CAPIE is not declared multiplayer
   compatible, and a confirmation.
4. Where to put the shortcut: Desktop by default, or a folder you pick.
5. Which Steam accounts to change, if there are several.

The install folder and shortcut name still carry the Rename Anything name from before the naming
decision. They will change with the planned rename to CAPIE.

## Steam launch option

With this option, pressing Play in Steam starts the game with CAPIE. The script adds the
launcher in front of `%command%` in the game's launch options and keeps everything else you had
there:

| Before | After |
|---|---|
| (empty) | `"…\RenameAnything\current\RenameAnythingLauncher.exe" %command%` |
| `-debug_mode` | `"…\RenameAnythingLauncher.exe" %command% -debug_mode` |
| `"C:\other.exe" %command% -x` | `"C:\other.exe" "…\RenameAnythingLauncher.exe" %command% -x` (with a warning) |

Steam must be closed while the script edits its settings file (`localconfig.vdf`). The script
waits for you to close it and never closes Steam itself. It makes a byte-exact backup first,
checks the result, and restores the backup on any difference.

To undo it by hand: Steam, Library, Hearts of Iron IV, Properties, General, Launch Options.
Delete the quoted `…\RenameAnythingLauncher.exe` part.

## Shortcut

The script can also create a shortcut that starts the same chain: the CAPIE launcher with the
game's Paradox Launcher (`dowser.exe`) as its argument, which is what Steam's `%command%` points
to. Its icon comes from `hoi4.exe`, and its comment says that it starts the game with CAPIE and
that this setup is not declared multiplayer compatible. The script never overwrites a file it did
not create.

Passing switches works without questions:

```powershell
& ([scriptblock]::Create((irm https://raw.githubusercontent.com/OWNER/hoi4-rename-anything/TAG/tools/bootstrap/install.ps1))) -Install -Shortcut -Yes
```

## Updates, only when you ask

CAPIE and the launcher never check for updates in the background.

```powershell
.\install.ps1 -CheckUpdate   # one request: newest version, and whether it supports your hoi4.exe
.\install.ps1 -Update        # install the newest signed release and switch to it
.\install.ps1 -Rollback      # switch back to the previous version, kept on disk
```

`-Update` accepts a release only if its manifest is signed with the key pinned in the script,
its tag matches the manifest, and every downloaded file matches the SHA-256 in the manifest.
Steam and shortcuts always point at a `current` folder link, so an update never touches Steam
again.

## Before multiplayer

While CAPIE is in your launch options, your game is not declared multiplayer compatible. Remove
the launch option before you play multiplayer:

```powershell
.\install.ps1 -RemoveSteamLaunchOption
```

## Uninstall

```powershell
.\install.ps1 -Uninstall
```

This removes the launch option part, the shortcuts the script created, and
`%LOCALAPPDATA%\RenameAnything` (the log file stays). Nothing in the game folder needs repairing.
Add `-DryRun` to any command to see what it would do without writing anything.

Names you set with a mod built on CAPIE stay in your saves. Without CAPIE, a custom country name
is expected to show as the original name. That has been seen with CAPIE not loaded and the mod
enabled; loading such a save with the mod disabled entirely has not been tested yet, so try it on
a copy.

## Today: development builds

Testers who are comfortable with a developer setup can load CAPIE by hand. The steps are in the
repository's `plugin/README.md`: build the DLL and run the injector against the running game.
This must be repeated every time the game starts. It is a development path, not the way players
are meant to install CAPIE.
