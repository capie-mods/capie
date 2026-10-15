# "CAPIE" switch mod changelog

Change notes of the "CAPIE" Steam Workshop item (`capie/`). `tools/release/release.py` turns
the section of the version in `capie/descriptor.mod` into the Workshop change note (BBCode).
Format: [Keep a Changelog](https://keepachangelog.com/en/1.1.0/).

## [Unreleased]

- Main-menu status line moved under the game logo. It now comes from a minimal copy of the vanilla
  `interface/frontendmainview.gui` (one added text box, marked `CAPIE BEGIN/END`) instead of the
  `frontend_alpha_stamp` window, which the game does not create in release builds.

## [1.0.0] - unreleased

- First Workshop release: the switch that turns CAPIE on and off through the playset, and the
  CAPIE status line in the main menu (in preparation).
