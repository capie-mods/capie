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

No. It has never been tested with two players. Remove the CAPIE launch option before you play
multiplayer. The plan for multiplayer is on the [Multiplayer](multiplayer.md) page.

## Can I still get achievements?

No, but not because of CAPIE. Any mod that changes files under `common/` changes the game's
checksum, and achievements need an unmodified checksum. Ironman was not tested.

## Will my saves break if I remove CAPIE?

Saves stay loadable. Names set through vanilla setters (states, cities, characters, leaders,
parties) stay as they are. A custom country name is expected to show as the original name; that
was seen with the mod enabled and CAPIE not loaded. Loading such a save with the mod disabled
entirely has not been tested, so try it on a copy.

## How do I uninstall it?

Run `install.ps1 -Uninstall`, or remove the launcher part from Steam's launch options by hand
and delete `%LOCALAPPDATA%\RenameAnything`. Nothing in the game folder needs repairing. See
[Install](install.md#uninstall).

## Why are the files called `rename_anything_*`?

They were built before the library got its name. Renaming the files to `capie_*` changes their
hashes, manifests, tests and reviews, so it is a separate, planned step.

## I make mods. Can I use CAPIE now?

Not yet. The public script contract is a proposal. Read [For modders](modders/index.md) to see
where it is going; nothing there should go into a published mod yet.

## Who decides who may rename what?

A player renames only their own country's things. Planned exceptions are the host, through a game
rule set in the lobby, and an overlord whose subject is another player. See
[Permissions](permissions.md).
