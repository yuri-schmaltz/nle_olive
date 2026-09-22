#!/usr/bin/env python3
"""
Olive Video Editor - End-to-End Test Suite Runner (tests/e2e/run_e2e.py)
Unified orchestrator for packaging validation, CTest E2E targets, and report generation.
"""
import argparse
import os
from pathlib import Path
import subprocess
import sys
import time

ROOT = Path(__file__).resolve().parents[2]


def run_e2e(preset="linux-asan", tier="all", verbose=False):
    print("=" * 70)
    print("OLIVE VIDEO EDITOR - END-TO-END AUTOMATED TEST SUITE")
    print(f"Preset: {preset} | Target Tier: {tier}")
    print("=" * 70)

    start_time = time.monotonic()
    failed_steps = []

    # 1. Run packaging validation if tier is 1, 2, 3, or all
    if tier in ("1", "2", "3", "all"):
        print("\n[E2E:Step 1/2] Executing Linux Packaging Test Suite (unittest)...")
        pkg_script = ROOT / "tests" / "e2e" / "test_packaging.py"
        cmd = [sys.executable, str(pkg_script)]
        if verbose:
            cmd.append("-v")
        res = subprocess.run(cmd, cwd=str(ROOT))
        if res.returncode != 0:
            print("[E2E:ERROR] Packaging validation failed!")
            failed_steps.append("test_packaging.py")
        else:
            print("[E2E:PASS] Packaging validation succeeded.")

    # 2. Run CTest E2E executables under Sanitizer
    build_dir = ROOT / f"build-{preset}"
    if not build_dir.exists():
        # Fallback to general build if specific preset directory not found
        if (ROOT / "build").exists():
            build_dir = ROOT / "build"
        else:
            print(f"[E2E:ERROR] Build directory {build_dir} does not exist. Run cmake build first.")
            return 1

    print(f"\n[E2E:Step 2/2] Executing C++ E2E Test Targets via CTest ({build_dir})...")
    ctest_cmd = ["ctest", "--test-dir", str(build_dir), "-L", "E2E", "--output-on-failure"]
    if verbose:
        ctest_cmd.append("-V")

    # If specific tier requested, filter tests by name if applicable
    if tier == "4":
        ctest_cmd.extend(["-R", "workflow"])

    res = subprocess.run(ctest_cmd, cwd=str(ROOT))
    if res.returncode != 0:
        print("[E2E:ERROR] CTest E2E tests failed!")
        failed_steps.append("ctest -L E2E")
    else:
        print("[E2E:PASS] All CTest E2E tests succeeded.")

    elapsed = round(time.monotonic() - start_time, 2)
    print("\n" + "=" * 70)
    if not failed_steps:
        print(f"E2E SUITE RESULT: ALL TESTS PASSED (Elapsed: {elapsed}s)")
        print("=" * 70)
        return 0
    else:
        print(f"E2E SUITE RESULT: FAILED ({', '.join(failed_steps)}) (Elapsed: {elapsed}s)")
        print("=" * 70)
        return 1


def main():
    parser = argparse.ArgumentParser(description="Olive E2E Test Suite Runner")
    parser.add_argument("--preset", default="linux-asan", help="CMake preset (e.g. linux-asan, linux-release)")
    parser.add_argument("--tier", default="all", choices=["1", "2", "3", "4", "all"], help="Filter by test tier")
    parser.add_argument("-v", "--verbose", action="store_true", help="Verbose output")
    args = parser.parse_args()

    sys.exit(run_e2e(preset=args.preset, tier=args.tier, verbose=args.verbose))


if __name__ == "__main__":
    main()
