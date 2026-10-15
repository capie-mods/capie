# ADR-0037: The installer closes Steam on request, never by force

- Status: Accepted. Changes the "never closes Steam" rule of the bootstrap.
- Date: 2026-10-15

## Context

The Steam launch option lives in `localconfig.vdf`, which Steam rewrites when it exits, so the
installer can only change it while Steam is closed. Until now the installer refused to close
Steam and asked the player to do it ("Steam menu > Exit, then Enter"). The maintainer asked
for the installer to offer closing and restarting Steam itself, without ever forcing it.

## Decision

- When Steam is running and the launch option must change, the installer asks "Steam is
  running. Close Steam now so the launch option can be changed? [Y/n]".
- Yes runs `steam.exe -shutdown`, Steam's own clean exit. The path comes from
  `HKCU\Software\Valve\Steam` (`SteamExe`, else `SteamPath\steam.exe`). The installer waits at
  most 60 s until `steam.exe` and every `steamwebhelper` are gone and shows progress.
- Steam is never killed. After a timeout, or when the player says no, the old prompt follows
  (close Steam yourself, or Q to skip the Steam step).
- If this run closed Steam, the player is asked "Start Steam again? [Y/n]" after the change; yes
  starts the same `steam.exe` without arguments and does not wait for it.
- With `-Yes` Steam is closed only with the explicit `-CloseSteam` switch (then restarted without
  a question). `-DryRun` prints the plan and starts nothing.
- Process start and process listing go through adapters, so the tests never touch a real Steam.

## Consequences

- The common path needs no manual Steam step, in the terminal interface and in plain prompts.
- `-shutdown` lets Steam finish writing its own files, so a forced kill can never leave
  `localconfig.vdf` half-written by Steam.
- If Steam ignores `-shutdown` (for example, a dialog is open), the player sees the old
  manual step after 60 s.
