#!/usr/bin/env python3
"""
Olive Video Editor - Packaging Verification Test Suite (tests/e2e/test_packaging.py)
Validates AppImage scripts, AppRun environment variables, and Flatpak JSON manifest.
"""
import json
import os
from pathlib import Path
import re
import subprocess
import unittest

ROOT = Path(__file__).resolve().parents[2]


class TestPackagingAppImage(unittest.TestCase):
    """Tier 1 and Tier 2 tests for Feature 10: Linux AppImage Packaging."""

    def setUp(self):
        self.apprun_path = ROOT / "app" / "packaging" / "linux" / "AppRun"
        self.build_script = ROOT / "packaging" / "linux" / "build_appimage.sh"
        self.desktop_file = ROOT / "app" / "packaging" / "linux" / "org.olivevideoeditor.Olive.desktop"
        self.icons_dir = ROOT / "app" / "packaging" / "linux" / "icons"

    # Tier 1 Tests
    def test_t1_10_01_apprun_syntax(self):
        """T1.10.01: AppRun script syntax check via bash -n."""
        self.assertTrue(self.apprun_path.exists(), f"{self.apprun_path} does not exist")
        res = subprocess.run(["bash", "-n", str(self.apprun_path)], capture_output=True, text=True)
        self.assertEqual(res.returncode, 0, f"AppRun syntax error: {res.stderr}")

    def test_t1_10_02_apprun_environment_exports(self):
        """T1.10.02: AppRun exports LD_LIBRARY_PATH, QT_PLUGIN_PATH, QML2_IMPORT_PATH, XDG_DATA_DIRS."""
        content = self.apprun_path.read_text(encoding="utf-8")
        required_vars = ["LD_LIBRARY_PATH", "QT_PLUGIN_PATH", "QML2_IMPORT_PATH", "XDG_DATA_DIRS"]
        for var in required_vars:
            pattern = rf"export\s+{var}="
            self.assertRegex(content, pattern, f"AppRun missing export for {var}")

    def test_t1_10_03_build_appimage_syntax(self):
        """T1.10.03: Build AppImage script syntax check via bash -n."""
        self.assertTrue(self.build_script.exists(), f"{self.build_script} does not exist")
        res = subprocess.run(["bash", "-n", str(self.build_script)], capture_output=True, text=True)
        self.assertEqual(res.returncode, 0, f"build_appimage.sh syntax error: {res.stderr}")

    def test_t1_10_04_desktop_and_icons(self):
        """T1.10.04: Desktop entry and application icons validation."""
        self.assertTrue(self.desktop_file.exists(), f"{self.desktop_file} does not exist")
        desktop_text = self.desktop_file.read_text(encoding="utf-8")
        self.assertIn("Exec=olive-editor", desktop_text)
        self.assertIn("Icon=org.olivevideoeditor.Olive", desktop_text)
        self.assertTrue(self.icons_dir.exists(), f"{self.icons_dir} does not exist")
        png_icons = list(self.icons_dir.rglob("*.png"))
        self.assertGreater(len(png_icons), 0, "No application icons found in icons directory")

    def test_t1_10_05_installed_binary_version(self):
        """T1.10.05: Binary version output validation if build artifact is present."""
        candidates = [
            ROOT / "build-linux-asan" / "app" / "olive-editor",
            ROOT / "build-linux-release" / "app" / "olive-editor",
            ROOT / "build-linux-debug" / "app" / "olive-editor",
            ROOT / "build" / "app" / "olive-editor",
        ]
        found_binary = None
        for c in candidates:
            if c.exists() and os.access(c, os.X_OK):
                found_binary = c
                break
        if found_binary:
            res = subprocess.run([str(found_binary), "--version"], capture_output=True, text=True)
            self.assertEqual(res.returncode, 0)
            output = res.stdout + res.stderr
            self.assertRegex(output, r"^\d+\.\d+\.\d+", f"Unexpected version output format: {output}")
        else:
            self.skipTest("Compiled binary not yet built in standard build directories")

    # Tier 2 Tests (Boundaries)
    def test_t2_10_01_missing_dependency_detection(self):
        """T2.10.01: Build script halts with exit code 1 when non-existent build dir is given."""
        res = subprocess.run(["bash", str(self.build_script), "non_existent_build_dir_12345"],
                             capture_output=True, text=True, cwd=str(ROOT))
        self.assertNotEqual(res.returncode, 0, "Build script did not fail on missing build dir")

    def test_t2_10_02_space_in_installation_path(self):
        """T2.10.02: AppRun handles variable quoting safely for paths with spaces."""
        content = self.apprun_path.read_text(encoding="utf-8")
        # Check that APPDIR is quoted in variable expansions
        self.assertIn('"${APPDIR}', content)

    def test_t2_10_03_symlink_integrity(self):
        """T2.10.03: Build script creates relative symlinks per AppImage spec."""
        content = self.build_script.read_text(encoding="utf-8")
        self.assertIn("ln -sf usr/share/applications", content)
        self.assertIn(".DirIcon", content)

    def test_t2_10_04_stripping_debug_symbols(self):
        """T2.10.04: Exclusion list includes core libc/graphics drivers."""
        content = self.build_script.read_text(encoding="utf-8")
        self.assertIn("EXCLUDED_PREFIXES", content)
        self.assertIn("libc.so", content)
        self.assertIn("libGL.so", content)

    def test_t2_10_05_permissions_audit(self):
        """T2.10.05: Scripts have executable permission bits set."""
        self.assertTrue(os.access(self.apprun_path, os.X_OK), "AppRun is not executable")
        self.assertTrue(os.access(self.build_script, os.X_OK), "build_appimage.sh is not executable")


class TestPackagingFlatpak(unittest.TestCase):
    """Tier 1 and Tier 2 tests for Feature 11: Linux Flatpak Packaging."""

    def setUp(self):
        self.manifest_path = ROOT / "packaging" / "flatpak" / "org.olivevideoeditor.Olive.json"

    # Tier 1 Tests
    def test_t1_11_01_flatpak_manifest_json_syntax(self):
        """T1.11.01: Flatpak manifest parses as valid JSON."""
        self.assertTrue(self.manifest_path.exists(), f"{self.manifest_path} does not exist")
        try:
            data = json.loads(self.manifest_path.read_text(encoding="utf-8"))
            self.assertIsInstance(data, dict)
        except json.JSONDecodeError as err:
            self.fail(f"Flatpak manifest JSON decoding error: {err}")

    def test_t1_11_02_flatpak_app_id_and_command(self):
        """T1.11.02: Flatpak manifest app-id and command match Olive identity."""
        data = json.loads(self.manifest_path.read_text(encoding="utf-8"))
        self.assertEqual(data.get("app-id"), "org.olivevideoeditor.Olive")
        self.assertEqual(data.get("command"), "olive-editor")

    def test_t1_11_03_flatpak_runtime_specs(self):
        """T1.11.03: Flatpak runtime targets KDE Platform/Sdk 6.8+."""
        data = json.loads(self.manifest_path.read_text(encoding="utf-8"))
        self.assertEqual(data.get("runtime"), "org.kde.Platform")
        self.assertEqual(data.get("sdk"), "org.kde.Sdk")
        version = float(data.get("runtime-version", 0))
        self.assertGreaterEqual(version, 6.8, f"Runtime version {version} is less than 6.8")

    def test_t1_11_04_flatpak_sandbox_permissions(self):
        """T1.11.04: Finish-args sandbox permissions include GUI, audio, DRI, IPC."""
        data = json.loads(self.manifest_path.read_text(encoding="utf-8"))
        finish_args = data.get("finish-args", [])
        expected = ["--share=ipc", "--socket=x11", "--socket=wayland", "--socket=pulseaudio", "--device=dri"]
        for exp in expected:
            self.assertIn(exp, finish_args, f"Missing required finish-arg: {exp}")

    def test_t1_11_05_flatpak_dependency_modules(self):
        """T1.11.05: Module recipes include required dependencies."""
        data = json.loads(self.manifest_path.read_text(encoding="utf-8"))
        module_names = [m.get("name") for m in data.get("modules", [])]
        required = ["portaudio", "imath", "openexr", "opencolorio", "openimageio", "olive"]
        for req in required:
            self.assertIn(req, module_names, f"Missing required module: {req}")

    # Tier 2 Tests (Boundaries)
    def test_t2_11_01_malformed_json_detection(self):
        """T2.11.01: Parser correctly detects and rejects malformed JSON."""
        bad_json = '{ "app-id": "bad", }'
        with self.assertRaises(json.JSONDecodeError):
            json.loads(bad_json)

    def test_t2_11_02_duplicate_module_names(self):
        """T2.11.02: Manifest has no duplicate module names."""
        data = json.loads(self.manifest_path.read_text(encoding="utf-8"))
        names = [m.get("name") for m in data.get("modules", [])]
        self.assertEqual(len(names), len(set(names)), "Duplicate module names found in Flatpak manifest")

    def test_t2_11_03_source_archive_hash_verification(self):
        """T2.11.03: All archive sources specify 64-character SHA256 hashes."""
        data = json.loads(self.manifest_path.read_text(encoding="utf-8"))
        for module in data.get("modules", []):
            for source in module.get("sources", []):
                if source.get("type") == "archive":
                    sha256 = source.get("sha256", "")
                    self.assertEqual(len(sha256), 64, f"Invalid SHA256 for module {module.get('name')}")
                    self.assertRegex(sha256, r"^[0-9a-fA-F]{64}$", f"Non-hex SHA256 in module {module.get('name')}")

    def test_t2_11_04_buildsystem_types_valid(self):
        """T2.11.04: Every module specifies a valid Flatpak buildsystem."""
        valid_systems = {"cmake-ninja", "autotools", "simple", "meson"}
        data = json.loads(self.manifest_path.read_text(encoding="utf-8"))
        for module in data.get("modules", []):
            bs = module.get("buildsystem")
            self.assertIn(bs, valid_systems, f"Invalid buildsystem '{bs}' in module {module.get('name')}")

    def test_t2_11_05_cleanup_rules_audit(self):
        """T2.11.05: Cleanup rules strip include headers and pkgconfig."""
        data = json.loads(self.manifest_path.read_text(encoding="utf-8"))
        cleanup = data.get("cleanup", [])
        self.assertIn("/include", cleanup)
        self.assertIn("/lib/pkgconfig", cleanup)


class TestPackagingCrossFeature(unittest.TestCase):
    """Tier 3 tests for Packaging cross-feature interactions."""

    def test_t3_13_appimage_environment_dsp(self):
        """T3.13: AppRun environment exports ensure library lookup for PortAudio/audio DSP."""
        apprun = ROOT / "app" / "packaging" / "linux" / "AppRun"
        content = apprun.read_text(encoding="utf-8")
        self.assertIn("LD_LIBRARY_PATH", content)
        self.assertIn("${APPDIR}/usr/lib", content)

    def test_t3_14_flatpak_environment_codecs(self):
        """T3.14: Flatpak sandbox and modules support video hardware acceleration and audio."""
        manifest = ROOT / "packaging" / "flatpak" / "org.olivevideoeditor.Olive.json"
        data = json.loads(manifest.read_text(encoding="utf-8"))
        finish_args = data.get("finish-args", [])
        self.assertIn("--device=dri", finish_args)
        self.assertIn("--socket=pulseaudio", finish_args)


if __name__ == "__main__":
    unittest.main(verbosity=2)
