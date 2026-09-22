#!/usr/bin/env python3
"""Run the configured E2E suite; missing builds/tests are errors, never passes."""
import argparse
import datetime
import json
from pathlib import Path
import subprocess
import sys

ROOT = Path(__file__).resolve().parents[2]


def run_e2e(preset="linux-asan", tier="all", verbose=False):
    if tier not in ("all", "4"):
        print("Individual tiers 1-3 are not separately registered. Use --tier all or 4.", file=sys.stderr)
        return 1
    build_dir = ROOT / f"build-{preset}"
    if not (build_dir / "CTestTestfile.cmake").is_file():
        print(f"Configured test build missing: {build_dir}. Run cmake --preset {preset} and build it.", file=sys.stderr)
        return 1
    selection = ["ctest", "--test-dir", str(build_dir), "-L", "^E2E$"]
    if tier == "4":
        selection.extend(["-R", "^e2e-workflow-tests$"])
    try:
        inventory = subprocess.run(selection + ["--show-only=json-v1"], cwd=ROOT,
                                   capture_output=True, text=True, check=True)
        tests = json.loads(inventory.stdout).get("tests", [])
        if not tests:
            print("No E2E tests match the requested selection.", file=sys.stderr)
            return 1
        stamp = datetime.datetime.now(datetime.timezone.utc).strftime("%Y%m%dT%H%M%S.%fZ")
        output = ROOT / "qa-results" / f"e2e-{stamp}"
        output.mkdir(parents=True)
        command = selection + ["--output-on-failure", "--no-tests=error", "--output-junit", str(output / "junit.xml")]
        if verbose:
            command.append("-V")
        result = subprocess.run(command, cwd=ROOT)
        print(f"JUnit report: {output / 'junit.xml'}", flush=True)
        return result.returncode
    except (OSError, subprocess.CalledProcessError, ValueError) as error:
        print(f"E2E execution failed: {error}", file=sys.stderr)
        return 1


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--preset", default="linux-asan",
                        choices=["linux-release", "linux-debug", "linux-asan", "linux-tsan"])
    parser.add_argument("--tier", default="all", choices=["all", "4"],
                        help="all runs integration and packaging checks; 4 runs the editorial workflow")
    parser.add_argument("-v", "--verbose", action="store_true")
    args = parser.parse_args()
    return run_e2e(args.preset, args.tier, args.verbose)


if __name__ == "__main__":
    sys.exit(main())
