# ADR-0030: Two installation paths, and a CAPIE mod in the playset as the on/off switch

- Status: Accepted. In progress.
- Date: 2026-10-14
- Source: [distribution decision log](https://github.com/SirCypkowskyy/hoi4-rename-anything/blob/monorepo-before-split-20261014/docs/evidence/decision-log/DISTRIBUTION-DECISIONS.md),
  "2026-10-14 — instalacja i włączanie CAPIE"

## Context

Players choose mods in the Paradox Launcher. It cannot be given an extra button, so a switch for
CAPIE has to live somewhere the player already looks. Installing should take as little as
possible.

## Decision

- Two ways to install:
  1. `irm ... | iex` in a terminal, with a terminal UI: an animation that refers to HOI4 and the
     mod, in the style of the terminal UIs of "pi" coding agents;
  2. an installer from GitHub Releases of the repository. The repository becomes public when the
     mod is ready.
- Switching CAPIE on and off: a small "CAPIE" mod in the playset (from the Workshop). The launch
  wrapper injects CAPIE only when that mod is enabled in the active playset. When it is disabled,
  the game runs clean (vanilla).
- The installer also has a "disable / uninstall" command that restores the state from before the
  installation.

## Consequences

- The launcher reads the playset the Paradox Launcher writes (`dlc_load.json`) and loads CAPIE
  only when the CAPIE mod is listed (`plugin/capie/src/launcher/playset_gate.hpp`). Missing or
  unreadable playset data means vanilla with one notice.
- The proxy loader of [ADR-0024](0024-proxy-dll-loader.md) stays as the alternative
  ([ADR-0031](0031-trust-without-paid-code-signing.md)).
- The bootstrap of [ADR-0008](0008-powershell-bootstrap.md) gains the terminal UI.
