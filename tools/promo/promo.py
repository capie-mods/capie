"""Promotional images of Rename Anything and CAPIE, built from in-game screenshots.

One reproducible pipeline for every picture outside the game: the README frames, the pictures of
the two documentation sites, the Steam Workshop preview images and galleries, and the LinkedIn
graphics. The look follows docs/design/DESIGN-SYSTEM.md and the colours come from
docs/design/tokens.json: a flat panel-umber panel in a window-ink frame, screenshots set in a
darker frame (frames are darker than panels), captions left-aligned in parchment and khaki under
one panel-rim hairline. No gradients, glow, shadows or emoji.

Sources. ``import`` copies only the screenshots listed in ``SOURCES`` from a folder of raw
captures into ``docs/assets/screenshots/source/<slug>.png``. Each one was reviewed by hand for
personal data (overlay names, account names, paths), debug output and raw localisation keys
before it was listed; the review notes are in ``Source.note`` and in
monorepo archive: docs/evidence/session-20261014/promo/report.txt. Importing re-encodes the pixels, so metadata
of the capture tool (text chunks, DPI) is not copied. The capture tool drew a small red cursor
dot into every screenshot; ``build`` paints it out at the listed position and fails if the dot
is not there, so a replaced source cannot be patched in the wrong place.

Outputs (``build``):

* ``docs/assets/promo/rename-anything/readme/``: ``hero.jpg`` and six feature frames.
* ``docs/assets/promo/rename-anything/workshop/``: ``preview.png`` (512 x 512, also written to
  ``mod/thumbnail.png``, the ``picture`` of ``mod/descriptor.mod``) and eight 1920 x 1080
  gallery images with captions.
* ``docs/assets/promo/rename-anything/linkedin/``: two 1200 x 627 images and one 1080 x 1080.
* ``docs/assets/promo/capie/readme/`` and ``.../capie/workshop/``: the CAPIE frames, gallery and
  ``preview.png`` (a copy of the switch mod's thumbnail drawn by
  plugin/capie/toggle-mod/make_thumbnail.py).
* ``sites/rename-anything/docs/assets/screenshots/`` and ``sites/capie/docs/assets/screenshots/``:
  copies of the README frames the sites use.

Fonts: Segoe UI, part of the design system's fallback stack and present on every Windows
installation (Ubuntu is preferred but not installed on the build machine). Output is
byte-for-byte reproducible on one machine with the same Pillow; ``--check`` compares a fresh
render with the files on disk.

Usage::

    uv run python tools/promo/promo.py import --from <folder of raw screenshots>
    uv run python tools/promo/promo.py contact-sheet --from <folder> --out <file.jpg>
    uv run python tools/promo/promo.py build
    uv run python tools/promo/promo.py build --check
"""

from __future__ import annotations

import argparse
import io
import json
import math
import re
import sys
from dataclasses import dataclass
from pathlib import Path
from typing import TYPE_CHECKING, Literal

from PIL import Image, ImageDraw, ImageFont

if TYPE_CHECKING:
    from collections.abc import Callable

HERE = Path(__file__).resolve().parent
"""Folder of this script (``tools/promo``)."""

REPO = HERE.parents[1]
"""Repository root."""

TOKENS = REPO / "docs" / "design" / "tokens.json"
"""Design tokens; every colour used here is read from this file."""

SOURCE_DIR = REPO / "docs" / "assets" / "screenshots" / "source"
"""Screenshots that passed the privacy review, one PNG per ``Source.slug``."""

PROMO_DIR = REPO / "docs" / "assets" / "promo"
"""Root of every generated promotional image."""

CAPIE_THUMBNAIL = REPO / "plugin" / "capie" / "toggle-mod" / "capie" / "thumbnail.png"
"""Preview image of the CAPIE switch mod, drawn by make_thumbnail.py and copied unchanged."""

MOD_THUMBNAIL = REPO / "mod" / "thumbnail.png"
"""Preview image named by ``picture`` in mod/descriptor.mod (Paradox Launcher and Workshop)."""

SITE_SHOTS = {
    "rename-anything": REPO / "sites" / "rename-anything" / "docs" / "assets" / "screenshots",
    "capie": REPO / "sites" / "capie" / "docs" / "assets" / "screenshots",
}
"""Screenshot folders of the two documentation sites."""

FONT_DIR = Path("C:/Windows/Fonts")
"""Windows font folder."""

FONT_BOLD = FONT_DIR / "segoeuib.ttf"
"""Bold face for names and titles (design system fallback stack)."""

FONT_REGULAR = FONT_DIR / "segoeui.ttf"
"""Regular face for body text."""

REFERENCE_WIDTH = 1920
"""Width of the design grid; every measurement below is in pixels of a 1920 px wide frame."""

CURSOR_RGB = (230, 27, 27)
"""Colour of the cursor dot the capture tool left in every screenshot."""

CURSOR_RADIUS = 4
"""Radius in pixels of the painted-out disk around a cursor dot."""

JPEG_QUALITY = 90
"""JPEG quality of every photo-like output; chroma is not subsampled so red text stays sharp."""

MAX_UPSCALE = 2.0
"""Largest factor a screenshot may be enlarged by; beyond this game text looks soft."""

PREVIEW_SIZE = 512
"""Edge of the Workshop preview images (square, as Steam and the Paradox Launcher show them)."""

PREVIEW_MAX_BYTES = 1_000_000
"""Steam rejects preview images of 1 MB and more."""

TAG_TRACKING = 0.08
"""Letter spacing of the ``tag`` text style, in em (tokens.json, typography.tag)."""

LABEL_TRACKING = 0.14
"""Letter spacing of the ``label-caps`` text style, in em (tokens.json)."""

SUPERSAMPLE = 4
"""Supersampling factor of the drawn (non-photo) parts of the preview image."""

type Box = tuple[int, int, int, int]
"""A rectangle as (left, top, right, bottom) pixels."""

type Colors = dict[str, str]
"""Colour tokens by name, for example ``"panel-umber" -> "#272421"``."""


@dataclass(frozen=True)
class Source:
    """One reviewed screenshot.

    Attributes:
        slug: File name in ``SOURCE_DIR`` without ``.png``.
        original: File name in the folder of raw captures.
        cursor: Top-left pixel of the capture tool's red cursor dot, or None.
        note: What the screenshot shows and why it passed the review.
    """

    slug: str
    original: str
    cursor: tuple[int, int] | None
    note: str


@dataclass(frozen=True)
class Shot:
    """A screenshot as placed in a frame.

    Attributes:
        slug: The ``Source.slug``.
        crop: Part of the source to show, or None for all of it.
    """

    slug: str
    crop: Box | None = None


@dataclass(frozen=True)
class Arrangement:
    """Screenshots placed together in one area.

    Attributes:
        shots: The screenshots, left to right (row) or top to bottom (column).
        layout: ``row`` or ``column``.
        top_aligned: Put the group at the top of its area instead of centring it vertically.
    """

    shots: tuple[Shot, ...]
    layout: Literal["row", "column"]
    top_aligned: bool = False


@dataclass(frozen=True)
class Frame:
    """One composed picture made of one or more screenshots.

    Attributes:
        name: File stem of the output.
        shots: The screenshots, left to right (row) or top to bottom (column).
        layout: ``row`` or ``column``.
        title: Caption title, drawn on Workshop gallery images.
        body: One caption line under the title.
        alt: English alternative text for README and site use.
    """

    name: str
    shots: tuple[Shot, ...]
    layout: Literal["row", "column"]
    title: str
    body: str
    alt: str


SOURCES = (
    Source(
        "rules-who",
        "screen_menu_1.png",
        (62, 254),
        "Game rules in the lobby: who may rename, with its tooltip.",
    ),
    Source(
        "rules-flags",
        "Zrzut ekranu 2026-10-14 213917.png",
        (397, 193),
        "Game rule: flag choice, with its tooltip.",
    ),
    Source(
        "leader-edit",
        "Zrzut ekranu 2026-10-14 214028.png",
        (248, 270),
        "Politics panel, leader name in the game's own text field.",
    ),
    Source(
        "country-edit",
        "Zrzut ekranu 2026-10-14 214055.png",
        (191, 352),
        "Politics panel, country name and adjective editor (Skbidi Kingdom).",
    ),
    Source(
        "flag-picker",
        "Zrzut ekranu 2026-10-14 214140.png",
        (245, 217),
        "Flag picker: every flag in the game, by letter.",
    ),
    Source(
        "leader-result",
        "Zrzut ekranu 2026-10-14 214237.png",
        (24, 240),
        "Politics panel after Apply: new country name, leader name and flag.",
    ),
    Source(
        "map-africa",
        "Zrzut ekranu 2026-10-14 214259.png",
        (248, 364),
        "Map label of the renamed country, province tooltip with the new owner name.",
    ),
    Source(
        "subject-hero",
        "Zrzut ekranu 2026-10-14 214438.png",
        (836, 646),
        "Diplomacy with a renamed subject (Province of Rizzia) and its map label.",
    ),
    Source(
        "editor-states",
        "Zrzut ekranu 2026-10-14 214520.png",
        (345, 394),
        "Name editor, States tab.",
    ),
    Source(
        "map-london",
        "Zrzut ekranu 2026-10-14 214545.png",
        (537, 394),
        "Map with a renamed city (Skibidi London).",
    ),
    Source(
        "subject-editor",
        "Zrzut ekranu 2026-10-14 214944.png",
        None,
        "Subject editor opened from Diplomacy: Country, Leader, Party and Flag tabs.",
    ),
    Source(
        "faction-dialog",
        "Zrzut ekranu 2026-10-14 215026.png",
        None,
        "Diplomatic action Rename faction and its confirmation dialog.",
    ),
    Source(
        "faction-map",
        "Zrzut ekranu 2026-10-14 215059.png",
        (1245, 413),
        "Faction map mode with a renamed faction (The Rizzlers).",
    ),
    Source(
        "subject-raj",
        "Zrzut ekranu 2026-10-14 215238.png",
        (341, 370),
        "Subject editor on the British Raj, with the map behind it.",
    ),
    Source(
        "country-gb",
        "Zrzut ekranu 2026-10-14 215352.png",
        (537, 411),
        "Country editor with the joke name Great Britian, before the news event.",
    ),
    Source(
        "news",
        "Zrzut ekranu 2026-10-14 215416.png",
        (449, 517),
        "World news event about the new country name.",
    ),
    Source(
        "rules-host",
        "Zrzut ekranu 2026-10-14 213839.png",
        (302, 44),
        "Game rule options for who may rename, with the host option's tooltip.",
    ),
    Source(
        "rules-news",
        "Zrzut ekranu 2026-10-14 213857.png",
        (346, 82),
        "Game rule: news event on or off, with its tooltip.",
    ),
    Source(
        "decision",
        "Zrzut ekranu 2026-10-14 214006.png",
        None,
        "Decision category Rename Anything with Rename characters. Only the top left part is "
        "used: the tooltip on the right lists every character and state like a debug dump.",
    ),
    Source(
        "editor-characters",
        "Zrzut ekranu 2026-10-14 214505.png",
        (348, 447),
        "Name editor, Characters tab. A tooltip covers one row; every crop stops above it.",
    ),
)
"""Every screenshot that passed the review. Rejected captures are listed in the report."""

FACTION_DIALOG = Shot("faction-dialog", (0, 22, 1030, 621))
"""The Rename faction action and its dialog, without the empty map to the right and the cut-off
panel title."""

HERO = Frame(
    "hero",
    (Shot("subject-hero", (0, 77, 1454, 895)),),
    "row",
    "Rename a subject from Diplomacy",
    "Canada becomes the Province of Rizzia: name, leader, party and flag.",
    "Hearts of Iron IV diplomacy screen with a subject renamed to Province of Rizzia, its leader "
    "Rizzler King and its Rizz Party; the map shows the new name across Canada.",
)
"""The large README image and the first gallery image."""

COUNTRY = Frame(
    "country-flag",
    (Shot("country-edit"), Shot("flag-picker")),
    "row",
    "Your country: name, adjective and flag",
    "Type into the game's own text field, or pick any flag the game has.",
    "Left: the Politics panel with a Name tab, the country name Skbidi Kingdom and the adjective "
    "Skibidish in text fields. Right: the Flag tab, a list of flags with letter filters.",
)
"""Country name, adjective and flag."""

LEADER = Frame(
    "leader",
    (Shot("leader-edit", (0, 18, 533, 470)), Shot("leader-result")),
    "row",
    "Your leader, in the Politics panel",
    "Click the pencil by the portrait, type a name, press Enter.",
    "Left: the Politics panel with a text field under the leader portrait reading Stanley "
    "Baldwin. Right: the same panel after Apply, the leader now named Stanley Egghead.",
)
"""Leader name, edited in place."""

SUBJECTS = Frame(
    "subjects",
    (Shot("subject-raj", (0, 80, 1342, 786)),),
    "row",
    "Subjects, renamed by their overlord",
    "Rename this country, in Diplomacy: name, adjective, leader, party and flag.",
    "Diplomacy screen of the British Raj with an editor open over it: Country, Leader, Party and "
    "Flag tabs, the name British Mandate of India and the adjective British-Indian.",
)
"""A subject's editor opened from Diplomacy."""

FACTIONS = Frame(
    "factions",
    (FACTION_DIALOG, Shot("faction-map", (600, 0, 1543, 643))),
    "row",
    "Faction names",
    "The faction leader renames the faction; the map and every panel follow.",
    "Left: the diplomatic action Rename faction and a dialog asking to open the editor for "
    "Allies. Right: the faction map mode with the renamed faction The Rizzlers.",
)
"""Faction rename."""

NEWS = Frame(
    "news-event",
    (Shot("country-gb", (0, 104, 1019, 573)), Shot("news")),
    "row",
    "Optional news event",
    "With the game rule on, every country hears about the new name.",
    "Left: the country editor with the name Great Britian. Right: a World News event titled New "
    "country name: Great Britian.",
)
"""News event after a country rename."""

MAP = Frame(
    "map",
    (Shot("map-africa"), Shot("map-london")),
    "row",
    "The map uses the new names",
    "Country labels, tooltips and renamed cities.",
    "Left: the map of East Africa labelled Skbidi Kingdom, a province tooltip naming the owner "
    "Skbidi Kingdom. Right: the map of England with the city Skibidi London.",
)
"""New names on the map."""

CITIES = Frame(
    "states-cities",
    (Shot("editor-states"), Shot("map-london")),
    "row",
    "States and cities",
    "Rename a state or its cities from the name editor. Scripts only, no CAPIE needed.",
    "Left: the name editor's States tab with Rename, Reset and Cities buttons for each state. "
    "Right: the map of England with the city Skibidi London.",
)
"""States and cities, the script-only part."""

RULES = Frame(
    "game-rules",
    (Shot("rules-who", (0, 50, 678, 296)), Shot("rules-flags", (0, 14, 895, 185))),
    "column",
    "Game rules",
    "Choose who may rename, whether a news event goes out, and which flags are offered.",
    "Game rules in the lobby: Rename Anything rules for who may rename, the news event and the "
    "flag choice, each with a tooltip.",
)
"""The three lobby rules."""

CAPIE_FIELD = Frame(
    "native-field",
    (Shot("leader-edit", (0, 18, 533, 470)), Shot("subject-editor", (0, 22, 502, 249))),
    "row",
    "The game's own text field",
    "CAPIE passes what the player types to the mod inside the Apply command.",
    "Two Rename Anything editors built on CAPIE: a leader name in the Politics panel and a "
    "subject's name and adjective, each in the game's own text field with Apply and Cancel.",
)
"""CAPIE's text field bridge."""

CAPIE_NAMES = Frame(
    "name-layers",
    (Shot("map-africa"), Shot("faction-map", (0, 0, 1543, 643))),
    "row",
    "Country and faction name layers",
    "A custom name is its own layer; the game draws it on the map and in every panel.",
    "Left: a country labelled Skbidi Kingdom on the map. Right: a faction labelled The Rizzlers "
    "in the faction map mode.",
)
"""CAPIE name layers."""

CAPIE_FLAGS = Frame(
    "flag-layer",
    (Shot("flag-picker"), Shot("leader-result")),
    "row",
    "Country flag layer (API level 6)",
    "Any flag from the game, without changing the country's name or colour.",
    "Left: a flag picker listing the game's flags by letter. Right: the Politics panel of the "
    "renamed country Skbidi Kingdom showing its new flag.",
)
"""CAPIE flag layer."""

COUNTRY_NAME = Frame(
    "country-name",
    (Shot("country-edit"), Shot("map-africa")),
    "row",
    "Your country's name and adjective",
    "Type them into the game's own text field; the map and every panel follow.",
    "Left: the Politics panel with the country name Skbidi Kingdom and the adjective Skibidish in "
    "text fields. Right: the map of East Africa labelled Skbidi Kingdom.",
)
"""Country name and adjective, editor and map."""

FLAGS = Frame(
    "flag",
    (Shot("flag-picker"), Shot("leader-result")),
    "row",
    "Any flag the game has",
    "Pick a flag by letter or from your own country's set; the name and colour stay.",
    "Left: the Flag tab with flags listed by letter. Right: the Politics panel of Skbidi Kingdom "
    "with its new flag in the top left corner.",
)
"""Flag picker and the flag in use."""

SUBJECT_TABS = Frame(
    "subjects",
    (Shot("subject-editor", (0, 22, 502, 249)), Shot("subject-raj", (0, 80, 1342, 786))),
    "row",
    "Subjects, renamed by their overlord",
    "Rename this country, in Diplomacy: name, adjective, leader, party and flag.",
    "Left: the subject editor with Country, Leader, Party and Flag tabs and the name Province of "
    "Rizzia. Right: the same editor over the British Raj, renamed British Mandate of India.",
)
"""The subject editor, close up and in context."""

CHARACTERS = Frame(
    "characters",
    (Shot("decision", (0, 0, 505, 150)), Shot("editor-characters", (0, 0, 554, 340))),
    "row",
    "Generals, admirals and advisers",
    "The decision Rename characters opens the name editor. No CAPIE needed.",
    "Left: the Rename Anything decision category with the decision Rename characters. Right: "
    "the name editor's Characters tab listing the leader, generals and admirals with Rename "
    "buttons.",
)
"""Characters, through the decision and the name editor."""

RULES_RIGHTS = Frame(
    "rules-rights",
    (Shot("rules-who", (0, 50, 678, 296)), Shot("rules-host")),
    "column",
    "Game rules: who may rename",
    "Own country and subjects, own country only, or the host renames every country.",
    "Lobby game rules: Rename Anything: who may rename, set to Own country and subjects, and its "
    "open list with the option Host renames every country and its tooltip.",
)
"""The rename rights rule."""

RULES_NEWS_FLAGS = Frame(
    "rules-news-flags",
    (Shot("rules-news"), Shot("rules-flags", (0, 14, 895, 185))),
    "column",
    "Game rules: news event and flags",
    "A news event for every country when a name changes, and which flags the picker offers.",
    "Lobby game rules: the news event rule with Off and On and its tooltip, and the flag choice "
    "rule with its three options and tooltip.",
)
"""The news and flag rules."""

CAPIE_COUNTRY = Frame(
    "country-layer",
    (Shot("country-edit"), Shot("map-africa")),
    "row",
    "Country name layer (API level 4)",
    "A custom name and adjective that the game draws everywhere; flag and colour stay.",
    "Left: a country editor with the name Skbidi Kingdom typed in. Right: the map labelled "
    "Skbidi Kingdom.",
)
"""CAPIE country name layer."""

CAPIE_FACTION = Frame(
    "faction-layer",
    (FACTION_DIALOG, Shot("faction-map", (600, 0, 1543, 643))),
    "row",
    "Faction name layer (API level 5)",
    "Stored on the faction leader; the game's own faction name stays underneath.",
    "Left: the diplomatic action Rename faction and its dialog. Right: the faction map mode with "
    "the faction The Rizzlers.",
)
"""CAPIE faction name layer."""

CAPIE_FALLBACK = Frame(
    "without-capie",
    (Shot("editor-states"), Shot("map-london")),
    "row",
    "Without CAPIE, the scripts still run",
    "A mod built on CAPIE loads without it; Rename Anything keeps its state and city editor.",
    "Left: Rename Anything's name editor, States tab, which needs no CAPIE. Right: the map with "
    "the renamed city Skibidi London.",
)
"""What a mod keeps when CAPIE is missing."""


@dataclass(frozen=True)
class Tile:
    """One screenshot on its own small panel, for two-column README galleries.

    Attributes:
        name: File stem of the output.
        shot: The screenshot.
        alt: English alternative text.
        wide: Use the wide tile (for short, wide screenshots such as game rules).
    """

    name: str
    shot: Shot
    alt: str
    wide: bool = False


RA_TILES = (
    Tile(
        "country-name",
        Shot("country-edit"),
        "The Politics panel with the country name Skbidi Kingdom and the adjective Skibidish in "
        "the game's own text fields.",
    ),
    Tile(
        "country-map",
        Shot("map-africa"),
        "The map of East Africa labelled Skbidi Kingdom, with a province tooltip naming the owner "
        "Skbidi Kingdom.",
    ),
    Tile(
        "flag-picker",
        Shot("flag-picker"),
        "The Flag tab of the country editor: flags listed by letter, with This country and All.",
    ),
    Tile(
        "flag-result",
        Shot("leader-result"),
        "The Politics panel of Skbidi Kingdom with its new flag in the top left corner and the "
        "leader Stanley Egghead.",
    ),
    Tile(
        "leader-edit",
        Shot("leader-edit", (0, 18, 533, 470)),
        "The Politics panel with a text field under the leader portrait reading Stanley Baldwin.",
    ),
    Tile(
        "party-diplomacy",
        Shot("subject-hero", (0, 77, 530, 400)),
        "Diplomacy view of the Province of Rizzia: leader Rizzler King and ruling party Rizz "
        "Party, both renamed.",
    ),
    Tile(
        "subject-editor",
        Shot("subject-editor", (0, 22, 502, 249)),
        "The subject editor with Country, Leader, Party and Flag tabs, the name Province of Rizzia "
        "and the adjective Rizzian.",
    ),
    Tile(
        "subject-raj",
        Shot("subject-raj", (0, 80, 1342, 786)),
        "The subject editor over the British Raj, renamed British Mandate of India, with the map "
        "behind it.",
    ),
    Tile(
        "faction-dialog",
        FACTION_DIALOG,
        "The diplomatic action Rename faction and a dialog asking to open the rename editor for "
        "Allies.",
    ),
    Tile(
        "faction-map",
        Shot("faction-map", (600, 0, 1543, 643)),
        "The faction map mode with the renamed faction The Rizzlers across Africa and India.",
    ),
    Tile(
        "states",
        Shot("editor-states"),
        "The name editor's States tab with Rename, Reset and Cities buttons for each state.",
    ),
    Tile("cities", Shot("map-london"), "The map of England with the renamed city Skibidi London."),
    Tile(
        "characters-decision",
        Shot("decision", (0, 0, 505, 150)),
        "The Rename Anything decision category with the decision Rename characters.",
        wide=True,
    ),
    Tile(
        "characters-editor",
        Shot("editor-characters", (0, 0, 554, 340)),
        "The name editor's Characters tab listing the leader, generals and admirals with Rename "
        "buttons.",
        wide=True,
    ),
    Tile(
        "news-country",
        Shot("country-gb", (0, 104, 1019, 573)),
        "The country editor with the name Great Britian, the map of the British Isles behind it.",
    ),
    Tile("news-event", Shot("news"), "A World News event titled New country name: Great Britian."),
    Tile(
        "rules-rights",
        Shot("rules-who", (0, 50, 678, 296)),
        "Lobby game rules with Rename Anything: who may rename and its tooltip.",
        wide=True,
    ),
    Tile(
        "rules-host",
        Shot("rules-host"),
        "The who may rename rule opened: Own country and subjects, Host renames every country, "
        "Own country only.",
        wide=True,
    ),
    Tile(
        "rules-news",
        Shot("rules-news"),
        "The news event rule opened: Off and On, with its tooltip.",
        wide=True,
    ),
    Tile(
        "rules-flags",
        Shot("rules-flags", (0, 14, 895, 185)),
        "The flag choice rule opened: every flag in the game, the country's own flags, or own "
        "flags and the overlord's.",
        wide=True,
    ),
)
"""Two-column gallery of the Rename Anything README, in reading order (pairs)."""

CAPIE_TILES = (
    Tile(
        "field-leader",
        Shot("leader-edit", (0, 18, 533, 470)),
        "A leader name in the game's own text field in the Politics panel.",
    ),
    Tile(
        "field-subject",
        Shot("subject-editor", (0, 22, 502, 249)),
        "A two-field editor: name Province of Rizzia and adjective Rizzian, in the game's own "
        "text fields.",
    ),
    Tile(
        "country-edit",
        Shot("country-edit"),
        "A country editor with the name Skbidi Kingdom and the adjective Skibidish typed in.",
    ),
    Tile(
        "country-map",
        Shot("map-africa"),
        "The map labelled Skbidi Kingdom, drawn from the country name layer.",
    ),
    Tile("faction-dialog", FACTION_DIALOG, "The diplomatic action Rename faction and its dialog."),
    Tile(
        "faction-map",
        Shot("faction-map", (600, 0, 1543, 643)),
        "The faction map mode with the faction The Rizzlers, drawn from the faction name layer.",
    ),
    Tile("flag-picker", Shot("flag-picker"), "A flag picker listing the game's flags by letter."),
    Tile(
        "flag-result",
        Shot("leader-result"),
        "The Politics panel with the chosen flag in the top left corner, drawn from the flag "
        "layer.",
    ),
)
"""Two-column gallery of the CAPIE repository README, in pairs."""

RA_README = (HERO, COUNTRY, LEADER, SUBJECTS, FACTIONS, NEWS, MAP)
"""Frames of the Rename Anything site (the README uses the tiles)."""

RA_GALLERY = (
    HERO,
    COUNTRY_NAME,
    FLAGS,
    LEADER,
    SUBJECT_TABS,
    FACTIONS,
    NEWS,
    CITIES,
    CHARACTERS,
    RULES_RIGHTS,
    RULES_NEWS_FLAGS,
)
"""Rename Anything Workshop gallery, in upload order."""

CAPIE_FRAMES = (CAPIE_FIELD, CAPIE_NAMES, CAPIE_FLAGS)
"""Frames of the CAPIE site."""

CAPIE_GALLERY = (CAPIE_FIELD, CAPIE_COUNTRY, CAPIE_FACTION, CAPIE_FLAGS, CAPIE_FALLBACK)
"""CAPIE Workshop gallery, in upload order."""

PROFILE_FRAMES = (COUNTRY, FACTIONS, MAP)
"""Extra frames for the capie-mods organisation profile README."""

README_SIZE = (1600, 900)
"""Size of README and site frames (shown 880 px wide, sharp on high-density screens)."""

GALLERY_SIZE = (1920, 1080)
"""Size of Workshop gallery images (16:9)."""

TILE_SIZE = (960, 720)
"""Size of a README gallery tile (shown about 436 px wide, two per row)."""

WIDE_TILE_SIZE = (960, 420)
"""Size of a wide README gallery tile, for short and wide screenshots."""

TILE_QUALITY = 85
"""JPEG quality of README tiles; there are many, so they are compressed a little more."""


@dataclass(frozen=True)
class Header:
    """One Workshop description graphic: the banner or a section header.

    Attributes:
        name: File stem.
        title: Section title as printed (the banner leaves it empty).
    """

    name: str
    title: str


RA_HEADERS = (
    Header("what-you-can-rename", "What you can rename"),
    Header("game-rules", "Game rules"),
    Header("requirements", "Requirements"),
    Header("installing", "Installing CAPIE"),
    Header("limits", "Limits"),
    Header("removing", "Removing it"),
    Header("links", "Source and links"),
)
"""Section headers of the Rename Anything Workshop description."""

CAPIE_HEADERS = (
    Header("status-line", "The status line in the main menu"),
    Header("requirements", "Requirements"),
    Header("installing", "Installing"),
    Header("how-it-works", "How it works"),
    Header("safety", "Safety and limits"),
    Header("clean-game", "Back to the clean game"),
    Header("links", "Source and links"),
)
"""Section headers of the CAPIE Workshop description (English)."""

CAPIE_HEADERS_PL = (
    Header("status-line-pl", "Linia stanu w menu głównym"),
    Header("requirements-pl", "Wymagania"),
    Header("installing-pl", "Instalacja"),
    Header("how-it-works-pl", "Jak to działa"),
    Header("safety-pl", "Bezpieczeństwo i ograniczenia"),
    Header("clean-game-pl", "Powrót do czystej gry"),
    Header("links-pl", "Kod i linki"),
)
"""Section headers of the Polish CAPIE Workshop description."""

HEADER_SIZE = (1260, 120)
"""Size of a section header: twice the 630 px Workshop description column, for sharp screens."""

BANNER_SIZE = (1260, 440)
"""Size of the banner at the top of a Workshop description (2x the 630 px column)."""

ORG_RAW = "https://raw.githubusercontent.com/capie-mods/.github/main"
"""Public base URL of files in the capie-mods/.github repository (Workshop description images)."""


def load_colors() -> Colors:
    """Read the colour tokens.

    Returns:
        Every colour token by name.
    """
    tokens = json.loads(TOKENS.read_text(encoding="utf-8"))
    return {
        name: token["$value"] for group in tokens["color"].values() for name, token in group.items()
    }


def font(path: Path, size: float) -> ImageFont.FreeTypeFont:
    """Load a font.

    Args:
        path: Font file.
        size: Size in output pixels.

    Returns:
        The font.
    """
    return ImageFont.truetype(str(path), max(1, round(size)))


def load_source(slug: str) -> Image.Image:
    """Open a reviewed screenshot and paint out its cursor dot.

    Args:
        slug: The ``Source.slug``.

    Returns:
        The cleaned RGB image.

    Raises:
        ValueError: The slug is unknown or the cursor dot is not where the list says.
    """
    source = next((s for s in SOURCES if s.slug == slug), None)
    if source is None:
        msg = f"unknown source {slug!r}"
        raise ValueError(msg)
    image = Image.open(SOURCE_DIR / f"{slug}.png").convert("RGB")
    if source.cursor is not None:
        x, y = source.cursor
        if image.getpixel((x, y)) != CURSOR_RGB:
            msg = f"{slug}: no cursor dot at {source.cursor}"
            raise ValueError(msg)
        paint_out(image, (x + 0.5, y + 1.5))
    return image


def paint_out(image: Image.Image, center: tuple[float, float]) -> None:
    """Replace a small disk with the mean colour of the ring around it.

    Args:
        image: RGB image, changed in place.
        center: Centre of the disk.
    """
    cx, cy = center
    ring: list[tuple[int, int, int]] = []
    disk: list[tuple[int, int]] = []
    reach = CURSOR_RADIUS + 3
    for y in range(math.floor(cy - reach), math.ceil(cy + reach) + 1):
        for x in range(math.floor(cx - reach), math.ceil(cx + reach) + 1):
            if not (0 <= x < image.width and 0 <= y < image.height):
                continue
            distance = math.hypot(x - cx, y - cy)
            if distance <= CURSOR_RADIUS:
                disk.append((x, y))
            elif distance <= reach:
                pixel = image.getpixel((x, y))
                if isinstance(pixel, tuple):
                    ring.append((int(pixel[0]), int(pixel[1]), int(pixel[2])))
    mean = tuple(round(sum(p[i] for p in ring) / len(ring)) for i in range(3))
    for point in disk:
        image.putpixel(point, mean)


def shot_image(shot: Shot) -> Image.Image:
    """Load and crop one shot.

    Args:
        shot: The shot.

    Returns:
        The cropped screenshot.
    """
    image = load_source(shot.slug)
    return image.crop(shot.crop) if shot.crop else image


def place_shots(
    canvas: Image.Image, group: Arrangement, area: Box, scale: float, colors: Colors
) -> None:
    """Fit the shots into an area at one common scale, side by side (row) or stacked (column).

    The screenshots were all taken at the game's native resolution, so one scale keeps the
    game's text the same size in every shot of a frame. Each shot gets a window-ink frame,
    because frames are darker than panels.

    Args:
        canvas: Target image, changed in place.
        group: The shots and how they are arranged.
        area: Free area of the canvas.
        scale: Size of one design-grid pixel in output pixels.
        colors: Colour tokens.
    """
    layout = group.layout
    images = [shot_image(shot) for shot in group.shots]
    gap = round(24 * scale)
    border = max(1, round(3 * scale))
    left, top, right, bottom = area
    width, height = right - left, bottom - top
    count = len(images)
    room = (
        (width - 2 * border * count - gap * (count - 1), height - 2 * border)
        if layout == "row"
        else (width - 2 * border, height - 2 * border * count - gap * (count - 1))
    )
    if layout == "row":
        natural = (sum(im.width for im in images), max(im.height for im in images))
    else:
        natural = (max(im.width for im in images), sum(im.height for im in images))
    factor = min(room[0] / natural[0], room[1] / natural[1], MAX_UPSCALE)
    sizes = [(round(im.width * factor), round(im.height * factor)) for im in images]
    outer = [(w + 2 * border, h + 2 * border) for w, h in sizes]
    if layout == "row":
        total = (sum(w for w, _ in outer) + gap * (count - 1), max(h for _, h in outer))
    else:
        total = (max(w for w, _ in outer), sum(h for _, h in outer) + gap * (count - 1))
    x = left + (width - total[0]) // 2
    y = top if group.top_aligned else top + (height - total[1]) // 2
    pen = ImageDraw.Draw(canvas)
    for image, (w, h), (ow, oh) in zip(images, sizes, outer, strict=True):
        ox = x if layout == "row" else x + (total[0] - ow) // 2
        oy = y + (total[1] - oh) // 2 if layout == "row" else y
        pen.rectangle((ox, oy, ox + ow - 1, oy + oh - 1), fill=colors["window-ink"])
        resized = image.resize((w, h), Image.Resampling.LANCZOS)
        canvas.paste(resized, (ox + border, oy + border))
        if layout == "row":
            x += ow + gap
        else:
            y += oh + gap


def panel(size: tuple[int, int], colors: Colors) -> tuple[Image.Image, float, Box]:
    """Draw the empty panel every frame starts from.

    Args:
        size: Output size.
        colors: Colour tokens.

    Returns:
        The canvas, the design-grid scale and the inner area inside the frame and padding.
    """
    width, height = size
    scale = width / REFERENCE_WIDTH
    canvas = Image.new("RGB", size, colors["window-ink"])
    pen = ImageDraw.Draw(canvas)
    edge = max(4, round(12 * scale))
    pen.rectangle((edge, edge, width - edge - 1, height - edge - 1), fill=colors["panel-umber"])
    rim = max(1, round(2 * scale))
    pen.rectangle(
        (edge, edge, width - edge - 1, height - edge - 1), outline=colors["panel-rim"], width=rim
    )
    pad = edge + round(40 * scale)
    return canvas, scale, (pad, pad, width - pad, height - pad)


def draw_caption(canvas: Image.Image, frame: Frame, area: Box, scale: float, colors: Colors) -> Box:
    """Draw the caption under a hairline at the bottom of an area.

    Args:
        canvas: Target image, changed in place.
        frame: Frame whose title and body are drawn.
        area: Inner area of the panel.
        scale: Size of one design-grid pixel in output pixels.
        colors: Colour tokens.

    Returns:
        The area left above the caption.
    """
    left, top, right, bottom = area
    pen = ImageDraw.Draw(canvas)
    title = font(FONT_BOLD, 46 * scale)
    body = font(FONT_REGULAR, 30 * scale)
    body_top = bottom - round(40 * scale)
    title_top = body_top - round(62 * scale)
    rule = title_top - round(32 * scale)
    pen.rectangle(
        (left, rule, right, rule + max(1, round(2 * scale)) - 1), fill=colors["panel-rim"]
    )
    pen.text((left, title_top), frame.title, font=title, fill=colors["parchment"])
    pen.text((left, body_top), frame.body, font=body, fill=colors["khaki"])
    return (left, top, right, rule - round(32 * scale))


def render_frame(
    frame: Frame, size: tuple[int, int], colors: Colors, *, caption: bool
) -> Image.Image:
    """Compose one frame.

    Args:
        frame: The frame.
        size: Output size.
        colors: Colour tokens.
        caption: Whether to draw the caption (Workshop gallery) or leave it to the page (README).

    Returns:
        The composed image.
    """
    canvas, scale, area = panel(size, colors)
    if caption:
        area = draw_caption(canvas, frame, area, scale, colors)
    place_shots(canvas, Arrangement(frame.shots, frame.layout), area, scale, colors)
    return canvas


def tracked(
    pen: ImageDraw.ImageDraw,
    origin: tuple[float, float],
    text: str,
    face: ImageFont.FreeTypeFont,
    fill: str,
) -> float:
    """Draw text in the design system's ``tag`` style: 0.08em letter spacing.

    Args:
        pen: Drawing context.
        origin: Top-left of the text.
        text: The text, already in capitals.
        face: Bold font at the tag size.
        fill: Colour.

    Returns:
        The x coordinate after the last character.
    """
    x, y = origin
    tracking = face.size * TAG_TRACKING
    for char in text:
        pen.text((x, y), char, font=face, fill=fill)
        x += face.getlength(char) + tracking
    return x


def wrap(text: str, face: ImageFont.FreeTypeFont, width: float) -> list[str]:
    """Break text into lines that fit a width.

    Args:
        text: The text.
        face: Font.
        width: Largest line width in pixels.

    Returns:
        The lines.
    """
    lines: list[str] = []
    line = ""
    for word in text.split():
        candidate = f"{line} {word}".strip()
        if line and face.getlength(candidate) > width:
            lines.append(line)
            line = word
        else:
            line = candidate
    if line:
        lines.append(line)
    return lines


def draw_pencil_mark(
    pen: ImageDraw.ImageDraw, origin: tuple[float, float], unit: float, colors: Colors
) -> None:
    """Draw the Rename Anything mark (sites/shared marks/rename-anything-mark.svg, 20 px grid).

    Args:
        pen: Drawing context.
        origin: Top-left corner.
        unit: Output pixels per grid pixel.
        colors: Colour tokens.
    """
    ox, oy = origin

    def at(x: float, y: float) -> tuple[float, float]:
        """Map a mark-grid point to the canvas.

        Args:
            x: Grid x.
            y: Grid y.

        Returns:
            The canvas point.
        """
        return (ox + x * unit, oy + y * unit)

    pen.rounded_rectangle(
        (*at(0.5, 0.5), *at(19.5, 19.5)),
        radius=unit,
        fill=colors["steel-shadow"],
        outline=colors["steel-bevel"],
        width=max(1, round(unit)),
    )
    pen.rectangle((*at(4, 3.5), *at(14, 16.5)), fill=colors["pencil-green"])
    pen.polygon(
        [at(7, 15.5), at(7, 13), at(14.5, 5.5), at(17, 8), at(9.5, 15.5)], fill=colors["khaki"]
    )


@dataclass(frozen=True)
class MarkShape:
    """One flat shape of the Overprint mark on its 500 px grid.

    Attributes:
        kind: ``rect`` or ``circle``.
        fill: Hex colour.
        a: Rect left or circle centre x.
        b: Rect top or circle centre y.
        c: Rect right or circle radius.
        d: Rect bottom (0 for circles).
        tilted: True for the overprint shapes, which turn by the mark's angle.
    """

    kind: str
    fill: str
    a: float
    b: float
    c: float
    d: float
    tilted: bool


MARK_GRID = 500.0
"""Edge of the Overprint mark's drawing grid."""

MARK_CANDIDATES = (
    REPO / "sites" / "shared" / "theme" / "marks" / "capie-mark-detail.svg",
    REPO / "sites" / "capie" / "docs" / "assets" / "shared" / "marks" / "capie-mark-detail.svg",
)
"""Where the detail drawing of the CAPIE mark lives: the shared theme in the monorepo and the
design repository, the site copy in the capie repository."""

_SVG_RECT = re.compile(
    r'<rect x="([\d.]+)" y="([\d.]+)" width="([\d.]+)" height="([\d.]+)" fill="(#[0-9a-f]{6})"/>'
)
"""One positioned rectangle of the mark SVG (the full-grid ground has no x and y)."""

_SVG_CIRCLE = re.compile(
    r'<circle cx="([\d.]+)" cy="([\d.]+)" r="([\d.]+)" fill="(#[0-9a-f]{6})"/>'
)
"""One circle (a perforation tooth) of the mark SVG."""

_SVG_TILT = re.compile(r'<g transform="rotate\((-?[\d.]+) [\d.]+ [\d.]+\)">(.*?)</g>', re.DOTALL)
"""The tilted overprint group of the mark SVG and its angle."""


def overprint_shapes() -> tuple[list[MarkShape], float]:
    """Read the Overprint mark from its SVG, so the geometry is not drawn a second time.

    Returns:
        The shapes back to front, without the square ground, and the overprint angle in degrees.

    Raises:
        FileNotFoundError: No copy of capie-mark-detail.svg was found.
    """
    path = next((p for p in MARK_CANDIDATES if p.is_file()), None)
    if path is None:
        msg = "capie-mark-detail.svg not found"
        raise FileNotFoundError(msg)
    text = path.read_text(encoding="utf-8")
    tilt = _SVG_TILT.search(text)
    angle = float(tilt.group(1)) if tilt else 0.0
    plain = text[: tilt.start()] if tilt else text
    shapes: list[MarkShape] = []
    for body, tilted in ((plain, False), (tilt.group(2) if tilt else "", True)):
        for match in re.finditer(f"{_SVG_RECT.pattern}|{_SVG_CIRCLE.pattern}", body):
            if match.group(1) is not None:
                x, y, w, h = (float(match.group(i)) for i in range(1, 5))
                shapes.append(MarkShape("rect", match.group(5), x, y, x + w, y + h, tilted))
            else:
                cx, cy, r = (float(match.group(i)) for i in range(6, 9))
                shapes.append(MarkShape("circle", match.group(9), cx, cy, r, 0.0, tilted))
    return shapes, angle


def draw_overprint(
    pen: ImageDraw.ImageDraw, origin: tuple[float, float], height: float, ground: str
) -> float:
    """Draw the Overprint mark so that its stamp is ``height`` pixels tall.

    Args:
        pen: Drawing context.
        origin: Top-left corner of the stamp (not of the 500 px grid).
        height: Stamp height in output pixels.
        ground: Colour behind the mark; the perforation teeth are cut in this colour.

    Returns:
        The stamp width in output pixels.
    """
    shapes, angle = overprint_shapes()
    stamp = shapes[0]
    k = height / (stamp.d - stamp.b)
    ox = origin[0] - stamp.a * k
    oy = origin[1] - stamp.b * k
    t = math.radians(angle)
    half = MARK_GRID / 2

    def at(x: float, y: float, *, tilted: bool) -> tuple[float, float]:
        """Map a grid point to the canvas, turning overprint points around the grid centre.

        Args:
            x: Grid x.
            y: Grid y.
            tilted: Whether to apply the overprint angle.

        Returns:
            The canvas point.
        """
        if tilted:
            dx, dy = x - half, y - half
            x, y = (
                half + dx * math.cos(t) - dy * math.sin(t),
                half + dx * math.sin(t) + dy * math.cos(t),
            )
        return (ox + x * k, oy + y * k)

    for shape in shapes:
        fill = ground if shape.fill == shapes[1].fill and shape.kind == "circle" else shape.fill
        if shape.kind == "circle":
            cx, cy = at(shape.a, shape.b, tilted=shape.tilted)
            r = shape.c * k
            pen.ellipse((cx - r, cy - r, cx + r, cy + r), fill=fill)
        else:
            corners = [
                (shape.a, shape.b),
                (shape.c, shape.b),
                (shape.c, shape.d),
                (shape.a, shape.d),
            ]
            pen.polygon([at(x, y, tilted=shape.tilted) for x, y in corners], fill=fill)
    return (stamp.c - stamp.a) * k


def fill_box(canvas: Image.Image, image: Image.Image, box: Box, border: int, ink: str) -> None:
    """Scale an image to cover a box exactly (cropping the overflow) and frame it.

    Args:
        canvas: Target image, changed in place.
        image: The screenshot, already cropped close to the box's aspect ratio.
        box: Outer box including the frame.
        border: Frame width in pixels.
        ink: Frame colour.
    """
    left, top, right, bottom = box
    ImageDraw.Draw(canvas).rectangle((left, top, right - 1, bottom - 1), fill=ink)
    width, height = right - left - 2 * border, bottom - top - 2 * border
    factor = max(width / image.width, height / image.height)
    scaled = image.resize(
        (math.ceil(image.width * factor), math.ceil(image.height * factor)),
        Image.Resampling.LANCZOS,
    )
    x = (scaled.width - width) // 2
    y = (scaled.height - height) // 2
    canvas.paste(scaled.crop((x, y, x + width, y + height)), (left + border, top + border))


def preview_canvas(shot: Shot, colors: Colors) -> tuple[Image.Image, ImageDraw.ImageDraw, int]:
    """Start a 512 x 512 Workshop preview: frame, panel and a real screenshot in the top part.

    Both previews (Rename Anything here, CAPIE in make_thumbnail.py) share this layout, so they
    read as one family in a Workshop list. The lower part, under the screenshot, is left for the
    mark and the name.

    Args:
        shot: Screenshot, cropped close to 464 x 266 (1.74:1).
        colors: Colour tokens.

    Returns:
        The supersampled canvas, its drawing context and the supersampling factor.
    """
    s = SUPERSAMPLE
    size = PREVIEW_SIZE * s
    canvas = Image.new("RGB", (size, size), colors["window-ink"])
    pen = ImageDraw.Draw(canvas)
    pen.rectangle((12 * s, 12 * s, size - 12 * s, size - 12 * s), fill=colors["panel-umber"])
    pen.rectangle(
        (12 * s, 12 * s, size - 12 * s, size - 12 * s), outline=colors["panel-rim"], width=2 * s
    )
    fill_box(
        canvas, shot_image(shot), (24 * s, 24 * s, 488 * s, 290 * s), 3 * s, colors["window-ink"]
    )
    return canvas, pen, s


def render_preview(colors: Colors) -> Image.Image:
    """Draw the 512 x 512 Workshop preview of Rename Anything.

    A real screenshot (the renamed subject on the map) on top, the pencil mark and the name
    below it in letters large enough to read at the 200 px Steam shows in lists.

    Args:
        colors: Colour tokens.

    Returns:
        The preview image.
    """
    canvas, pen, s = preview_canvas(Shot("subject-hero", (560, 200, 1336, 645)), colors)
    draw_pencil_mark(pen, (30 * s, 318 * s), 7.4 * s, colors)
    name = font(FONT_BOLD, 56 * s)
    pen.text((196 * s, 300 * s), "Rename", font=name, fill=colors["parchment"])
    pen.text((196 * s, 362 * s), "Anything", font=name, fill=colors["parchment"])
    tracked(pen, (199 * s, 446 * s), "BUILT ON CAPIE", font(FONT_BOLD, 17 * s), colors["khaki"])
    return canvas.resize((PREVIEW_SIZE, PREVIEW_SIZE), Image.Resampling.LANCZOS)


def render_tile(tile: Tile, colors: Colors) -> Image.Image:
    """Draw one README gallery tile: a single screenshot on a small panel.

    Args:
        tile: The tile.
        colors: Colour tokens.

    Returns:
        The tile image.
    """
    canvas, scale, area = panel(WIDE_TILE_SIZE if tile.wide else TILE_SIZE, colors)
    place_shots(canvas, Arrangement((tile.shot,), "row"), area, scale, colors)
    return canvas


def perforate(pen: ImageDraw.ImageDraw, band: Box, pitch: float, radius: float, ink: str) -> None:
    """Cut stamp perforations (half circles) into the top and bottom edge of a band.

    Args:
        pen: Drawing context.
        band: The band.
        pitch: Distance between tooth centres.
        radius: Tooth radius.
        ink: Colour of the ground the teeth show.
    """
    left, top, right, bottom = band
    x = left + pitch / 2
    while x < right:
        for y in (top, bottom):
            pen.ellipse((x - radius, y - radius, x + radius, y + radius), fill=ink)
        x += pitch


def render_header(header: Header, colors: Colors) -> Image.Image:
    """Draw one Workshop section header: a perforated stamp band with the section title.

    The title is in label caps; two thin vermilion rules run from the title to the right edge,
    the strike bars of the Overprint mark laid flat.

    Args:
        header: The header.
        colors: Colour tokens.

    Returns:
        The header image (twice the 630 px description column).
    """
    width, height = HEADER_SIZE
    s = width / 630
    canvas = Image.new("RGB", (width, height), colors["window-ink"])
    pen = ImageDraw.Draw(canvas)
    band = (0, round(8 * s), width, round(52 * s))
    pen.rectangle(band, fill=colors["panel-umber"])
    perforate(pen, band, 12 * s, 3 * s, colors["window-ink"])
    face = font(FONT_BOLD, 16 * s)
    text = header.title.upper()
    y = (band[1] + band[3]) / 2
    end = 20 * s
    for char in text:  # label caps: 0.14em tracking (tokens.json, typography.label-caps)
        pen.text((end, y - 11 * s), char, font=face, fill=colors["parchment"])
        end += face.getlength(char) + face.size * LABEL_TRACKING
    gap = 3 * s
    rule = 2.5 * s
    for top in (y - gap / 2 - rule, y + gap / 2):
        pen.rectangle((end + 16 * s, top, width - 20 * s, top + rule), fill=colors["vermilion"])
    return canvas


def render_banner(product: str, colors: Colors) -> Image.Image:
    """Draw the banner at the top of a Workshop description.

    Name, one line on what the item does and a real screenshot, on the same perforated band as
    the section headers.

    Args:
        product: ``rename-anything`` or ``capie``.
        colors: Colour tokens.

    Returns:
        The banner image (twice the 630 px description column).
    """
    width, height = BANNER_SIZE
    s = width / 630
    canvas = Image.new("RGB", (width, height), colors["window-ink"])
    pen = ImageDraw.Draw(canvas)
    band = (0, round(8 * s), width, round(212 * s))
    pen.rectangle(band, fill=colors["panel-umber"])
    perforate(pen, band, 12 * s, 3 * s, colors["window-ink"])
    if product == "capie":
        shot = Shot("subject-editor", (44, 22, 387, 249))
        stamp_w = draw_overprint(pen, (24 * s, 30 * s), 74 * s, colors["panel-umber"])
        name, x_name = "CAPIE", 24 * s + stamp_w + 14 * s
        line = "Cyprian's API Extender: typed text, custom names and flags for HOI4 mods."
        tag = "THE PLAYSET SWITCH"
    else:
        shot = Shot("subject-hero", (560, 180, 1320, 680))
        draw_pencil_mark(pen, (24 * s, 34 * s), 3.4 * s, colors)
        name, x_name = "Rename Anything", 24 * s + 80 * s
        line = "Rename your country, leader, parties, subjects, factions, states and cities."
        tag = "BUILT ON CAPIE"
    size = 40
    title = font(FONT_BOLD, size * s)
    while title.getlength(name) > 326 * s - x_name:
        size -= 1
        title = font(FONT_BOLD, size * s)
    pen.text((x_name, 36 * s), name, font=title, fill=colors["parchment"])
    tracked(pen, (x_name + 2 * s, 82 * s), tag, font(FONT_BOLD, 10 * s), colors["khaki"])
    body = font(FONT_REGULAR, 14 * s)
    y = 122 * s
    for text_line in wrap(line, body, 296 * s):
        pen.text((24 * s, y), text_line, font=body, fill=colors["parchment"])
        y += 21 * s
    fill_box(
        canvas,
        shot_image(shot),
        (round(342 * s), round(24 * s), round(608 * s), round(196 * s)),
        round(2 * s),
        colors["window-ink"],
    )
    return canvas


def render_linkedin(shot: Shot, size: tuple[int, int], colors: Colors) -> Image.Image:
    """Draw a LinkedIn image: text column on the left, one screenshot on the right.

    In the square format the text sits above the screenshot.

    Args:
        shot: The screenshot.
        size: Output size (1200 x 627 or 1080 x 1080).
        colors: Colour tokens.

    Returns:
        The image.
    """
    width, height = size
    canvas = Image.new("RGB", size, colors["window-ink"])
    pen = ImageDraw.Draw(canvas)
    pen.rectangle((8, 8, width - 9, height - 9), fill=colors["panel-umber"])
    pen.rectangle((8, 8, width - 9, height - 9), outline=colors["panel-rim"], width=1)
    square = width == height
    left, top = 48, 48
    column = width - 96 if square else 392
    name = font(FONT_BOLD, 44)
    body = font(FONT_REGULAR, 22)
    small = font(FONT_REGULAR, 16)
    draw_pencil_mark(pen, (left, top + 6), 2.2, colors)
    pen.text((left + 60, top), "Rename Anything", font=name, fill=colors["parchment"])
    tracked(pen, (left + 62, top + 60), "BUILT ON CAPIE", font(FONT_BOLD, 13), colors["khaki"])
    y = top + 110
    text = (
        "Rename your country, its leader and parties, your subjects, factions, states and "
        "cities in Hearts of Iron IV. You type into the game's own text field, where the "
        "name is shown."
    )
    for line in wrap(text, body, column):
        pen.text((left, y), line, font=body, fill=colors["parchment"])
        y += 32
    y += 14
    note = (
        "CAPIE (Cyprian's API Extender) is the small native library underneath. "
        "Pre-release; tested in single player on HOI4 1.19.3.0."
    )
    for line in wrap(note, small, column):
        pen.text((left, y), line, font=small, fill=colors["ash"])
        y += 24
    area = (left, y + 40, width - 48, height - 48) if square else (512, 48, width - 48, height - 48)
    place_shots(canvas, Arrangement((shot,), "row", top_aligned=square), area, 0.6, colors)
    return canvas


def jpeg(image: Image.Image, quality: int = JPEG_QUALITY) -> bytes:
    """Encode an image as JPEG without metadata.

    Args:
        image: RGB image.
        quality: JPEG quality.

    Returns:
        The encoded bytes.
    """
    buffer = io.BytesIO()
    image.save(buffer, format="JPEG", quality=quality, subsampling=0, optimize=True)
    return buffer.getvalue()


def png(image: Image.Image) -> bytes:
    """Encode an image as PNG without timestamps or other variable chunks.

    Args:
        image: RGB image.

    Returns:
        The encoded bytes.
    """
    buffer = io.BytesIO()
    image.save(buffer, format="PNG", optimize=True)
    return buffer.getvalue()


def plan(colors: Colors) -> dict[Path, Callable[[], bytes]]:
    """List every output file and how to make it.

    Args:
        colors: Colour tokens.

    Returns:
        A renderer per output path.
    """
    outputs: dict[Path, Callable[[], bytes]] = {}
    ra = PROMO_DIR / "rename-anything"
    capie = PROMO_DIR / "capie"

    def frame_job(frame: Frame, size: tuple[int, int], *, caption: bool) -> Callable[[], bytes]:
        """Bind one frame render.

        Args:
            frame: The frame.
            size: Output size.
            caption: Whether to draw the caption.

        Returns:
            A function that renders and encodes the frame.
        """
        return lambda: jpeg(render_frame(frame, size, colors, caption=caption))

    def tile_job(tile: Tile) -> Callable[[], bytes]:
        """Bind one tile render.

        Args:
            tile: The tile.

        Returns:
            A function that renders and encodes the tile.
        """
        return lambda: jpeg(render_tile(tile, colors), TILE_QUALITY)

    for frame in RA_README:
        outputs[ra / "readme" / f"{frame.name}.jpg"] = frame_job(frame, README_SIZE, caption=False)
        outputs[SITE_SHOTS["rename-anything"] / f"{frame.name}.jpg"] = frame_job(
            frame, README_SIZE, caption=False
        )
    workshop = PROMO_DIR / "workshop"
    for number, frame in enumerate(RA_GALLERY, start=1):
        outputs[workshop / "rename-anything" / "gallery" / f"{number:02d}-{frame.name}.jpg"] = (
            frame_job(frame, GALLERY_SIZE, caption=True)
        )
    for tile in RA_TILES:
        outputs[ra / "readme" / "tiles" / f"{tile.name}.jpg"] = tile_job(tile)
    outputs[workshop / "rename-anything" / "preview.png"] = lambda: png(render_preview(colors))
    outputs[MOD_THUMBNAIL] = lambda: png(render_preview(colors))
    linkedin = ra / "linkedin"
    outputs[linkedin / "linkedin-1200x627-subject.jpg"] = lambda: jpeg(
        render_linkedin(Shot("subject-hero", (0, 77, 1454, 895)), (1200, 627), colors)
    )
    outputs[linkedin / "linkedin-1200x627-editor.jpg"] = lambda: jpeg(
        render_linkedin(Shot("country-edit"), (1200, 627), colors)
    )
    outputs[linkedin / "linkedin-1080x1080.jpg"] = lambda: jpeg(
        render_linkedin(Shot("subject-hero", (0, 77, 1454, 895)), (1080, 1080), colors)
    )
    for frame in CAPIE_FRAMES:
        outputs[capie / "readme" / f"{frame.name}.jpg"] = frame_job(
            frame, README_SIZE, caption=False
        )
        outputs[SITE_SHOTS["capie"] / f"{frame.name}.jpg"] = frame_job(
            frame, README_SIZE, caption=False
        )
    for number, frame in enumerate(CAPIE_GALLERY, start=1):
        outputs[workshop / "capie" / "gallery" / f"{number:02d}-{frame.name}.jpg"] = frame_job(
            frame, GALLERY_SIZE, caption=True
        )
    for tile in CAPIE_TILES:
        outputs[capie / "readme" / "tiles" / f"{tile.name}.jpg"] = tile_job(tile)
    outputs[workshop / "capie" / "preview.png"] = CAPIE_THUMBNAIL.read_bytes
    return outputs


def org_plan(root: Path, colors: Colors) -> dict[Path, Callable[[], bytes]]:
    """List the files for the public capie-mods/.github repository.

    That repository is public while the product repositories are not, so it hosts the pictures
    of the Workshop descriptions (Steam does not host description images) and the extra frames
    of the organisation profile.

    Args:
        root: Checkout of capie-mods/.github.
        colors: Colour tokens.

    Returns:
        A renderer per output path.
    """
    outputs: dict[Path, Callable[[], bytes]] = {}
    for frame in PROFILE_FRAMES:
        path = root / "profile" / "assets" / f"rename-anything-{frame.name}.jpg"
        outputs[path] = _frame_bytes(frame, colors)
    for product, headers in (
        ("rename-anything", RA_HEADERS),
        ("capie", CAPIE_HEADERS + CAPIE_HEADERS_PL),
    ):
        folder = root / "workshop" / product
        outputs[folder / "banner.jpg"] = _banner_bytes(product, colors)
        for header in headers:
            outputs[folder / f"h-{header.name}.png"] = _header_bytes(header, colors)
    return outputs


def _frame_bytes(frame: Frame, colors: Colors) -> Callable[[], bytes]:
    """Bind a README-size frame render without caption.

    Args:
        frame: The frame.
        colors: Colour tokens.

    Returns:
        A function that renders and encodes the frame.
    """
    return lambda: jpeg(render_frame(frame, README_SIZE, colors, caption=False), TILE_QUALITY)


def _banner_bytes(product: str, colors: Colors) -> Callable[[], bytes]:
    """Bind a Workshop banner render.

    Args:
        product: ``rename-anything`` or ``capie``.
        colors: Colour tokens.

    Returns:
        A function that renders and encodes the banner.
    """
    return lambda: jpeg(render_banner(product, colors), TILE_QUALITY)


def _header_bytes(header: Header, colors: Colors) -> Callable[[], bytes]:
    """Bind a Workshop section header render.

    Args:
        header: The header.
        colors: Colour tokens.

    Returns:
        A function that renders and encodes the header.
    """
    return lambda: png(render_header(header, colors))


def build(*, check: bool, org: Path | None = None) -> int:
    """Render every output and write it, or compare it with the file on disk.

    Args:
        check: Compare only; write nothing.
        org: Checkout of capie-mods/.github; when given, its files are rendered instead of the
            monorepo's.

    Returns:
        The process exit code.
    """
    stale = 0
    colors = load_colors()
    base = org if org is not None else REPO
    outputs = org_plan(org, colors) if org is not None else plan(colors)
    for path, job in outputs.items():
        data = job()
        relative = path.relative_to(base).as_posix()
        if path.suffix == ".png" and "preview" in path.name and len(data) >= PREVIEW_MAX_BYTES:
            print(f"TOO LARGE {relative} ({len(data)} bytes, Steam needs under 1 MB)")
            stale += 1
            continue
        if check:
            same = path.is_file() and path.read_bytes() == data
            if not same:
                print(f"STALE {relative}")
                stale += 1
            continue
        path.parent.mkdir(parents=True, exist_ok=True)
        path.write_bytes(data)
        print(f"wrote {relative} ({len(data) // 1024} KiB)")
    if check:
        print("UP-TO-DATE" if stale == 0 else f"{stale} file(s) stale")
    return 1 if stale else 0


def import_sources(folder: Path) -> int:
    """Copy the reviewed screenshots into ``SOURCE_DIR`` as pixel-only PNGs.

    Args:
        folder: Folder of the raw captures.

    Returns:
        The process exit code.
    """
    SOURCE_DIR.mkdir(parents=True, exist_ok=True)
    missing = 0
    for source in SOURCES:
        original = folder / source.original
        if not original.is_file():
            print(f"MISSING {source.original}")
            missing += 1
            continue
        pixels = Image.open(original).convert("RGB")
        clean = Image.new("RGB", pixels.size)
        clean.paste(pixels)
        target = SOURCE_DIR / f"{source.slug}.png"
        target.write_bytes(png(clean))
        print(f"imported {source.slug} <- {source.original}")
    return 1 if missing else 0


def contact_sheet(folder: Path, out: Path) -> int:
    """Write a labelled overview of every PNG in a folder, for the privacy review.

    Args:
        folder: Folder of raw captures.
        out: Output image (JPEG).

    Returns:
        The process exit code.
    """
    colors = load_colors()
    files = sorted(folder.glob("*.png"))
    cell, label, columns = 360, 28, 4
    rows = max(1, math.ceil(len(files) / columns))
    sheet = Image.new("RGB", (columns * cell, rows * (cell + label)), colors["window-ink"])
    pen = ImageDraw.Draw(sheet)
    face = font(FONT_REGULAR, 14)
    for index, path in enumerate(files):
        image = Image.open(path).convert("RGB")
        image.thumbnail((cell - 8, cell - 8), Image.Resampling.LANCZOS)
        x = (index % columns) * cell
        y = (index // columns) * (cell + label)
        sheet.paste(image, (x + 4, y + 4))
        pen.text((x + 4, y + cell), f"{index + 1:02d} {path.name}", font=face, fill=colors["khaki"])
    out.parent.mkdir(parents=True, exist_ok=True)
    out.write_bytes(jpeg(sheet))
    print(f"wrote {out} ({len(files)} screenshots)")
    return 0


def main() -> int:
    """Parse the command line and run one command.

    Returns:
        The process exit code.
    """
    parser = argparse.ArgumentParser(description=(__doc__ or "").splitlines()[0])
    commands = parser.add_subparsers(dest="command", required=True)
    build_parser = commands.add_parser("build", help="render every promotional image")
    build_parser.add_argument(
        "--check", action="store_true", help="exit 1 when a file differs from a fresh render"
    )
    build_parser.add_argument(
        "--org",
        type=Path,
        help="checkout of capie-mods/.github: render its Workshop and profile pictures instead",
    )
    import_parser = commands.add_parser("import", help="copy the reviewed screenshots")
    import_parser.add_argument("--from", dest="folder", type=Path, required=True)
    sheet_parser = commands.add_parser("contact-sheet", help="overview of a folder of captures")
    sheet_parser.add_argument("--from", dest="folder", type=Path, required=True)
    sheet_parser.add_argument("--out", type=Path, required=True)
    args = parser.parse_args()
    if args.command == "build":
        return build(check=args.check, org=args.org)
    if args.command == "import":
        return import_sources(args.folder)
    return contact_sheet(args.folder, args.out)


if __name__ == "__main__":
    sys.exit(main())
