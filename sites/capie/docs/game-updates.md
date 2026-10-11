---
description: What happens to CAPIE when Hearts of Iron IV is patched. Signature table, all-or-nothing activation, versions.json, release process and VirusTotal.
---

# Game updates

CAPIE works with the code of one exact game build. A Paradox patch can move or change that code,
so CAPIE treats every unknown build as unsafe until a release says otherwise.

## What happens after a patch today <span class="st st-today">Works today</span>

The pinned `hoi4.exe` hash no longer matches, CAPIE does not start, and the game runs without it.
Every patch does this, even one that changes nothing CAPIE uses. The planned launcher also
checks the hash first and starts the game without CAPIE, with a message that says why.

For players with a mod built on CAPIE, nothing breaks: states, cities and characters still rename
through scripts, a custom country name shows as the original, and indicators say "CAPIE missing".
The names come back when a CAPIE release supports the new build.

## Opt-in verification on unknown builds <span class="st st-planned">Planned</span>

Players asked for every feature to keep working after a patch that does not touch CAPIE's code,
without waiting for a release. The decided answer is an option, off by default:

1. With the option on and an unknown `hoi4.exe` hash, CAPIE scans the running game for every
   entry of its signature table: function patterns and the structure offsets it reads from code.
2. It switches on only if every pattern matches exactly once and every byte it would hook is
   identical to the tested build.
3. If even one check fails, nothing is hooked. All features or none, never a subset.
4. On an unverified build a small, discreet mark stays visible in the game.

The goal is still a release for every build, so that most players never need the option.

## Signature table <span class="st st-planned">Planned</span>

Today the addresses and patterns CAPIE checks are constants spread over several source files.
The plan replaces them with one table per supported game build, generated offline, reviewed,
signed with the release, and compiled into the DLL (so the launcher's hash of the DLL also covers
the table). CAPIE never downloads it and never reads it from disk.

<span class="proposed-tag">Proposed, subject to change</span>

```json title="Signature table entry (shortened)"
{
  "schema": 1,
  "game": { "sha256": "7DC947BE…A031D5", "version": "1.19.3.0", "steam_buildid": 25205862 },
  "entries": [
    {
      "id": "loc.lookup_n",
      "kind": "hook",
      "module": "names",
      "required": true,
      "rva": "0x239EE20",
      "pattern": "48 8B 05 ?? ?? ?? ?? 49 B8 25 23 22 84 E4 9C F2 CB",
      "expect": "unique-in-text-at-rva"
    },
    {
      "id": "view.dirty_offset",
      "kind": "offset",
      "derive": { "from": "view.mark_dirty", "instruction": "mov byte ptr [reg+disp8], 1", "operand": "disp" },
      "value": "0x44"
    }
  ]
}
```

Patterns cover whole instructions and mask only bytes that move when code is relinked. Every
offset records how it was derived from an instruction, so it is re-derived for each new build,
never copied from the previous one.

## A new release for a new build <span class="st st-planned">Planned</span>

1. Notice the patch: a new Steam build id or a new `hoi4.exe` hash.
2. Run the offline scanner on the new executable. It sorts each table entry into one of six
   classes:

    | Class | Meaning | Action |
    |---|---|---|
    | EXACT | Same bytes, same address | Accept |
    | RELOCATED | Same function body, new address, all anchors agree | Accept after review |
    | DRIFTED | Right function, but its body or a derived offset changed | Human review, targeted reverse engineering, new tests |
    | AMBIGUOUS | More than one candidate | Short, targeted reverse engineering |
    | MISSING | No candidate | Reverse engineering; the module may ship disabled |
    | CONTRADICTED | Anchors point at different functions | Treated as missing; never pick one |

3. Build every binary and run the offline tests.
4. Run a smoke test in the game of about 30 minutes: start with CAPIE, rename a country, leader
   and party, check Enter and the 32-character limit, save, load, restart, and start once
   without CAPIE.
5. Update the main-menu snippet if Paradox changed the vanilla menu file.
6. Scan locally with Windows Defender.
7. Upload the final release files to VirusTotal, with the maintainer's consent each time, and link
   the results in the release notes.
8. Sign the release manifest (file hashes and supported game hashes) with the offline key.
9. Publish `versions.json`, signed with the same key.

A module whose entries are not all resolved ships disabled in that release (for example: names
on, caption refresh off), and the release notes say so.

## versions.json <span class="st st-planned">Planned</span>

The release manifest describes one release. `versions.json` answers a different question: which
CAPIE release do I need for my `hoi4.exe`? It lives in the repository, signed, and is attached to
each release.

<span class="proposed-tag">Proposed, subject to change</span>

```json title="versions.json"
{
  "schema": 1,
  "games": {
    "7DC947BE…A031D5": { "version": "1.19.3.0", "min_capie": "1.0.0", "status": "supported" },
    "<new sha>":        { "version": "1.19.4.0", "min_capie": "1.0.1", "status": "supported" },
    "<other sha>":      { "version": "1.20.0.0", "status": "pending" }
  },
  "api_level": { "1.0.0": 1, "1.0.1": 1 }
}
```

`install.ps1 -CheckUpdate` would read it, compare it with your local `hoi4.exe` hash and the
installed version, and suggest `-Update` or report "pending". It stays on demand: no background
checks and no telemetry. Today `-CheckUpdate` reads only the latest GitHub release; reading
`versions.json` is a planned change.

## What a patch costs

These are estimates, not measurements.

| Scenario | Effort |
|---|---|
| The patch does not touch hooked code, only addresses move | 2 to 4 hours, plus about 30 minutes for VirusTotal and signing |
| Some entries drifted (a structure field moved, a function was edited) | 1 to 3 days, mostly targeted reverse engineering and new tests |
| Paradox reworked the politics view, edit box or command system | 1 to 3 weeks; affected modules may ship disabled first |
| Only the main-menu snippet changed | 30 to 60 minutes |

## Old game builds

To compare builds, the maintainer keeps a private copy of each new `hoi4.exe` from now on. Game
files are never redistributed: they stay out of every repository, CI artifact and release.
Tools that need Steam credentials are run only by the maintainer, by hand.
