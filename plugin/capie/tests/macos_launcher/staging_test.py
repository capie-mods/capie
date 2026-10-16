"""Behavior tests for external staging; never launches Paradox or HOI4."""

# Standard-library unittest runner; no pytest dependency.
# ruff: noqa: PT009, PT027
import importlib.util
import tempfile
import unittest
from pathlib import Path

repo = Path(__file__).resolve().parents[4]
spec = importlib.util.spec_from_file_location("staging", repo / "tools/stage-macos-child.py")
staging = importlib.util.module_from_spec(spec)
spec.loader.exec_module(staging)


class StagingTests(unittest.TestCase):
    """Verify real data routing and rejection without starting a launcher."""

    def setUp(self) -> None:
        """SetUp."""
        self.settings = {
            "gameId": "hoi4",
            "distPlatform": "steam",
            "rawVersion": "1.19.3.0",
            "gameDataPath": "~/Documents/Paradox Interactive/Hearts of Iron IV",
            "exePath": "./hoi4.app/Contents/MacOS/hoi4",
            "exeArgs": ["-gdpr-compliant"],
            "dlcPath": ".",
            "themeFile": "./assets/theme-settings.json",
            "ingameSettingsLayoutPath": "settings-layout.json",
            "ingameSettingsPath": "pdx_settings.json",
            "gameCachePaths": ["mod/"],
            "alternativeExecutables": [
                {
                    "label": {"pl": "Debug"},
                    "exePath": "./hoi4.app/Contents/MacOS/hoi4",
                    "exeArgs": ["-debug_mode"],
                }
            ],
            "untouched": {"future": True},
        }

    def build(self) -> dict:
        """Build the fixture settings.

        Returns:
            Independent staged configuration.
        """
        return staging.prepare(
            self.settings,
            Path("/game"),
            Path("/user"),
            Path("/owned/child"),
            Path("/owned/capie.dylib"),
            "a" * 64,
        )

    def test_shared_real_data_root_and_both_launch_modes(self) -> None:
        """Test shared real data root and both launch modes."""
        out = self.build()
        data = "/user/Documents/Paradox Interactive/Hearts of Iron IV"
        self.assertEqual(out["gameDataPath"], data)
        for mode in [out, *out["alternativeExecutables"]]:
            self.assertEqual(mode["exePath"], "/owned/child")
            self.assertEqual(
                mode["exeArgs"][:7],
                [
                    "--capie-dev-child",
                    "/game/hoi4.app/Contents/MacOS/hoi4",
                    "/game/hoi4.app/Contents/MacOS",
                    data + "/dlc_load.json",
                    "/owned/capie.dylib",
                    "a" * 64,
                    "--",
                ],
            )
        self.assertEqual(out["exeArgs"][7:], ["-gdpr-compliant"])
        self.assertEqual(out["alternativeExecutables"][0]["exeArgs"][7:], ["-debug_mode"])
        self.assertEqual(out["alternativeExecutables"][0]["label"], {"pl": "Debug"})
        self.assertEqual(out["dlcPath"], "/game")
        self.assertEqual(out["themeFile"], "/game/assets/theme-settings.json")
        self.assertEqual(out["ingameSettingsLayoutPath"], "/game/settings-layout.json")
        self.assertEqual(out["ingameSettingsPath"], "pdx_settings.json")
        self.assertEqual(out["gameCachePaths"], ["mod/"])
        self.assertEqual(out["untouched"], {"future": True})
        self.assertEqual(self.settings["exePath"], "./hoi4.app/Contents/MacOS/hoi4")

    def test_unknown_alternative_is_rejected_not_dropped(self) -> None:
        """Test unknown alternative is rejected not dropped."""
        self.settings["alternativeExecutables"][0]["exePath"] = "./different-game"
        with self.assertRaises(ValueError):
            self.build()

    def test_ambiguous_paths_and_arguments_rejected(self) -> None:
        """Test ambiguous paths and arguments rejected."""
        for field, value in [
            ("gameDataPath", "relative/data"),
            ("exeArgs", "-debug"),
            ("exeArgs", ["--capie-dev-child"]),
            ("gameId", "fixture"),
        ]:
            with self.subTest(field=field, value=value):
                original = self.settings[field]
                self.settings[field] = value
                with self.assertRaises(ValueError):
                    self.build()
                self.settings[field] = original

    def test_existing_destination_never_overwritten(self) -> None:
        """Test existing destination never overwritten."""
        with tempfile.TemporaryDirectory() as directory:
            destination = Path(directory) / "staging"
            staging.write_new(destination, self.build())
            original = (destination / "launcher-settings.json").read_bytes()
            with self.assertRaises(FileExistsError):
                staging.write_new(destination, {})
            self.assertEqual((destination / "launcher-settings.json").read_bytes(), original)

    def test_paired_mode_preserves_runtime_named_game_argument(self) -> None:
        """Keep runtime configuration before delimiter and genuine game args after it."""
        self.settings["exeArgs"] = ["--capie-runtime", "game-only"]
        result = staging.prepare(
            self.settings,
            Path("/game"),
            Path("/user"),
            Path("/child"),
            Path("/bootstrap"),
            "a" * 64,
            (Path("/runtime"), "b" * 64),
        )
        self.assertEqual(
            result["exeArgs"][6:],
            ["--capie-runtime", "/runtime", "b" * 64, "--", "--capie-runtime", "game-only"],
        )
        with self.assertRaises(ValueError):
            staging.prepare(
                self.settings,
                Path("/game"),
                Path("/user"),
                Path("/child"),
                Path("/bootstrap"),
                "a" * 64,
                (Path("/runtime"), "bad"),
            )


if __name__ == "__main__":
    unittest.main()
