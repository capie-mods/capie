---
description: Common questions about CAPIE. Safety, antivirus warnings, game updates, multiplayer, achievements and uninstalling.
---

# FAQ

## Is CAPIE a virus? My antivirus complains.

CAPIE loads itself into the game's process. Some malware does that too, so heuristic warnings are
possible. Every released binary will be scanned on VirusTotal and the results linked from the
release notes. Until then the current development builds were checked only with a local Windows
Defender scan, which found no threats. The source is public and you can build it yourself.

## Does CAPIE change my game files?

No. It never writes to the game folder. It works in memory while the game runs and is gone when
you close the game. See [Safety](safety.md).

## Does CAPIE talk to the internet?

The DLL does not. It imports no networking library. The installer script goes online only when
you run it to install, check for updates or update.

## Can I download it?

Not yet. There is no release. The [install page](install.md) shows how it will work.

## What happens after a game update?

CAPIE does not start on a game build it does not know, and the game runs without it. Mods built
on CAPIE fall back: Rename Anything's states, cities and characters keep working, and a custom
country name shows as the original until a CAPIE release supports the new build. A planned
opt-in mode can switch CAPIE on for an unknown build, but only if every check passes. See
[Game updates](game-updates.md).

## Does it work in multiplayer?

No. It has never been tested with two players. Switch CAPIE off before you play multiplayer. The plan for multiplayer is on the [Multiplayer](multiplayer.md) page.

## Can I still get achievements?

No, but not because of CAPIE. Any mod that changes files under `common/` changes the game's
checksum, and achievements need an unmodified checksum. Ironman was not tested.

## Will my saves break if I remove CAPIE?

Saves stay loadable. Names set through vanilla setters (states, cities, characters, leaders,
parties) stay as they are. A custom country name stays in the save, and without CAPIE the
country shows its game name with its own flag. With the older `RN1` storage this was seen with
the mod enabled and CAPIE not loaded; for the current name layer it has not been checked yet. Loading such a save with the mod disabled
entirely has not been tested, so try it on a copy.

## How do I uninstall it?

Run `install.ps1 -Uninstall`, or remove the launcher part from Steam's launch options by hand
and delete `%LOCALAPPDATA%\RenameAnything`. To switch CAPIE off without uninstalling, disable
the CAPIE mod in your playset (in progress). Nothing in the game folder needs repairing. See
[Install](install.md#uninstall).

## Why do some files still say Rename Anything?

They were named before the library got its name. The DLLs are already `capie_*`. The launcher
and the install folder will follow; renaming them changes hashes, manifests and tests, so it
is a separate step.

## I make mods. Can I use CAPIE now?

You can build against it, but do not publish yet. The script contract and the SDK exist and the
SDK's demo mod ran in the game, but CAPIE is not released. Read [For modders](modders/index.md).

## Who decides who may rename what?

Each mod checks it in its own effects. The SDK's rule lets players rename their own country and
their subjects, or any country when the host allows it in the lobby. See
[Permissions](permissions.md).
