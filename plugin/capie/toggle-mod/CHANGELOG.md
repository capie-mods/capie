# "CAPIE" switch mod changelog

Change notes of the "CAPIE" Steam Workshop item (`capie/`). `tools/release/release.py` turns
the section of the version in `capie/descriptor.mod` into the Workshop change note (BBCode).
Format: [Keep a Changelog](https://keepachangelog.com/en/1.1.0/).

## [Unreleased]

- The main-menu status line is now the CAPIE mark under the game logo, above the DLC column. It
  means only that the CAPIE switch is on: the menu is built and localised before CAPIE loads, so
  it cannot show a live status (Rename Anything's editors show the CAPIE version in game). Its
  tooltip is static text. It comes from a minimal copy of the vanilla
  `interface/frontendmainview.gui` (one added icon, marked `CAPIE BEGIN/END`, stored byte for byte
  with `-text` in `.gitattributes`) instead of the `frontend_alpha_stamp` window, which the game
  does not create in release builds. New: `interface/capie_menu.gfx` and the one-image sprite
  `gfx/interface/capie_menu_mark.dds`.

## [1.0.0] - unreleased

- First Workshop release: the switch that turns CAPIE on and off through the playset, and the
  CAPIE status line in the main menu (in preparation).
