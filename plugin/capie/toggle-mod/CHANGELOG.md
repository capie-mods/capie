# "CAPIE" switch mod changelog

Change notes of the "CAPIE" Steam Workshop item (`capie/`). `tools/release/release.py` turns
the section of the version in `capie/descriptor.mod` into the Workshop change note (BBCode).
Format: [Keep a Changelog](https://keepachangelog.com/en/1.1.0/).

## [Unreleased]

- The main-menu status line is now the CAPIE mark under the game logo, above the DLC column,
  with the status in its tooltip (CAPIE version, API level and build, or "CAPIE missing" and what
  to check). It comes from a minimal copy of the vanilla `interface/frontendmainview.gui` (one
  added icon, marked `CAPIE BEGIN/END`) instead of the `frontend_alpha_stamp` window, which the
  game does not create in release builds. New: `interface/capie_menu.gfx` and the 2-frame sprite
  `gfx/interface/capie_menu_mark.dds` (frame 1 shown; frame 2, "not loaded", prepared).

## [1.0.0] - unreleased

- First Workshop release: the switch that turns CAPIE on and off through the playset, and the
  CAPIE status line in the main menu (in preparation).
