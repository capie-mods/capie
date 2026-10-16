"""Parent command/refusal tests, without invoking the real launcher."""

# Standard-library unittest keeps the offline fixture independent of pytest.
# ruff: noqa: PT009, PT027
import importlib.util
import json
import os
import subprocess
import sys
import tempfile
import unittest
from pathlib import Path

repo = Path(__file__).resolve().parents[4]
spec = importlib.util.spec_from_file_location("parent", repo / "tools/launch-macos-child.py")
parent = importlib.util.module_from_spec(spec)
spec.loader.exec_module(parent)


class ParentTests(unittest.TestCase):
    """Verify original dowser routing and refusal before staging is launched."""

    def test_preserves_argument_boundaries(self) -> None:
        """Keep spaced paths separate and use original dowser, not a staged copy."""
        self.assertEqual(
            parent.command(Path("/game with spaces"), Path("/own/stage"), ""),
            ["/game with spaces/dowser", "--pdxlGameDir", "/own/stage", "--gameDir", "/own/stage"],
        )

    def test_existing_launcher_refused(self) -> None:
        """Never deliver staged requests to an existing unknown launcher instance."""
        with self.assertRaises(RuntimeError):
            parent.command(
                Path("/game"),
                Path("/stage"),
                "/user/Paradox Launcher.app/Contents/MacOS/Paradox Launcher\n",
            )

    def test_running_game_is_not_a_launcher_collision(self) -> None:
        """A recorder-only test may coexist with a game, without stopping it."""
        self.assertTrue(parent.command(Path("/game"), Path("/stage"), "/game/hoi4\n"))

    def test_parent_execs_only_original_fixture_with_environment(self) -> None:
        """Run the actual parent against an owned fake dowser and synthetic settings."""
        with tempfile.TemporaryDirectory() as directory:
            base = Path(directory)
            game = base / "game with spaces"
            game.mkdir()
            (game / "launcher-settings.json").write_text(
                json.dumps(
                    {
                        "gameId": "hoi4",
                        "distPlatform": "steam",
                        "rawVersion": "1.19.3.0",
                        "gameDataPath": str(base / "data"),
                        "exePath": "./hoi4.app/Contents/MacOS/hoi4",
                        "exeArgs": ["-debug_mode"],
                    }
                )
            )
            report = base / "report.json"
            dowser = game / "dowser"
            dowser.write_text(
                "#!"
                + sys.executable
                + "\n"
                + "import os,sys,json\n"
                + "open(os.environ['CAPIE_PARENT_REPORT'],'x').write(json.dumps({"
                + "'argv':sys.argv,'cwd':os.getcwd(),'steam':os.environ['SteamAppId']}))\n"
            )
            dowser.chmod(0o700)
            destination = base / "staging"
            result = subprocess.run(  # noqa: S603 -- own parent and fake dowser only
                [
                    sys.executable,
                    str(repo / "tools/launch-macos-child.py"),
                    str(game),
                    "/owned/recorder",
                    "/owned/library",
                    "a" * 64,
                    str(destination),
                ],
                env=dict(os.environ, CAPIE_PARENT_REPORT=str(report), SteamAppId="394360"),
                capture_output=True,
                text=True,
                check=False,
            )
            self.assertEqual(result.returncode, 0, result.stderr)
            observed = json.loads(report.read_text())
            self.assertEqual(
                observed["argv"],
                [
                    str(dowser.resolve()),
                    "--pdxlGameDir",
                    str(destination.resolve()),
                    "--gameDir",
                    str(destination.resolve()),
                ],
            )
            self.assertEqual(observed["cwd"], str(game.resolve()))
            self.assertEqual(observed["steam"], "394360")


if __name__ == "__main__":
    unittest.main()
