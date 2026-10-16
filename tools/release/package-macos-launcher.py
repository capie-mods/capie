"""Package only the native macOS launcher component; never publish or claim runtime readiness."""

import argparse
from pathlib import Path

from releasekit.macos_component import package_component


def main() -> None:
    """Read the common CAPIE version and package the explicitly supplied build directory."""
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("build", type=Path)
    parser.add_argument("destination", type=Path)
    args = parser.parse_args()
    header = Path(__file__).resolve().parents[2] / "plugin/capie/src/core/game_build.hpp"
    print(package_component(args.build, header, args.destination))


if __name__ == "__main__":
    main()
