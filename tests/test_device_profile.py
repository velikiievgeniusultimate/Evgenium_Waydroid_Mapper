from __future__ import annotations

import importlib.util
import json
from pathlib import Path
import tempfile
import unittest
from unittest import mock


SCRIPT_PATH = Path(__file__).parents[1] / "scripts" / "device-profile.py"
SPEC = importlib.util.spec_from_file_location("ewm_device_profile", SCRIPT_PATH)
assert SPEC is not None and SPEC.loader is not None
device_profile = importlib.util.module_from_spec(SPEC)
SPEC.loader.exec_module(device_profile)


class DeviceProfileTest(unittest.TestCase):
    def setUp(self) -> None:
        self.temporary = tempfile.TemporaryDirectory()
        self.root = Path(self.temporary.name)
        self.config = self.root / "waydroid.cfg"
        self.base = self.root / "waydroid_base.prop"
        self.state = self.root / "ewm-device-profile.json"
        self.backup = self.root / "waydroid.cfg.ewm-backup"
        self.config.write_text(
            "[waydroid]\narch = arm64\n\n[properties]\n"
            "ro.product.brand = waydroid\n"
            "ro.product.manufacturer = Waydroid\n"
            "ro.product.model = ARM64 test host\n"
            "ro.product.device = waydroid_arm64_only\n"
            "ro.product.name = lineage_waydroid_arm64_only\n"
            "ro.product.first_api_level = 28\n"
            "ro.build.fingerprint = waydroid/native/device:13/id/build:userdebug/test-keys\n"
            "unmanaged.value = preserved\n",
            encoding="utf-8",
        )
        self.base.write_text(
            "ro.product.brand=waydroid\n"
            "ro.product.manufacturer=Waydroid\n"
            "ro.product.model=ARM64 test host\n"
            "ro.product.device=waydroid_arm64_only\n"
            "ro.product.name=lineage_waydroid_arm64_only\n"
            "ro.product.first_api_level=28\n"
            "ro.build.fingerprint=waydroid/native/device:13/id/build:userdebug/test-keys\n"
            "unmanaged.value=preserved\n",
            encoding="utf-8",
        )
        self.path_patch = mock.patch.multiple(
            device_profile,
            WAYDROID_ROOT=self.root,
            CONFIG_PATH=self.config,
            BASE_PROP_PATH=self.base,
            STATE_PATH=self.state,
            BACKUP_PATH=self.backup,
        )
        self.path_patch.start()
        self.root_patch = mock.patch.object(device_profile.os, "geteuid", return_value=0)
        self.root_patch.start()

    def tearDown(self) -> None:
        self.root_patch.stop()
        self.path_patch.stop()
        self.temporary.cleanup()

    def test_google_profile_is_complete_and_reversible(self) -> None:
        original_config = device_profile.extract_config_values(
            self.config.read_text(encoding="utf-8"))
        original_base = device_profile.extract_prop_values(
            self.base.read_text(encoding="utf-8"))

        self.assertEqual(device_profile.apply("poco-f5-google"), 0)
        config_values = device_profile.extract_config_values(
            self.config.read_text(encoding="utf-8"))
        base_values = device_profile.extract_prop_values(
            self.base.read_text(encoding="utf-8"))
        expected = device_profile.PROFILES["poco-f5-google"]
        self.assertEqual(config_values, expected)
        self.assertEqual(base_values, expected)
        self.assertEqual(config_values["ro.build.type"], "user")
        self.assertEqual(config_values["ro.build.tags"], "release-keys")
        self.assertNotIn("ro.product.first_api_level", expected)
        self.assertTrue(config_values["ro.build.fingerprint"].endswith(
            ":user/release-keys"))

        self.assertEqual(device_profile.apply("native"), 0)
        restored_config_text = self.config.read_text(encoding="utf-8")
        restored_base_text = self.base.read_text(encoding="utf-8")
        self.assertEqual(device_profile.extract_config_values(restored_config_text),
                         original_config)
        self.assertEqual(device_profile.extract_prop_values(restored_base_text),
                         original_base)
        self.assertIn("unmanaged.value = preserved", restored_config_text)
        self.assertIn("unmanaged.value=preserved", restored_base_text)
        self.assertFalse(self.state.exists())

    def test_version_one_state_migrates_without_losing_new_native_keys(self) -> None:
        legacy_keys = tuple(device_profile.PROFILES["poco-f5"])
        original_config = device_profile.extract_config_values(
            self.config.read_text(encoding="utf-8"))
        original_base = device_profile.extract_prop_values(
            self.base.read_text(encoding="utf-8"))
        legacy_state = {
            "version": 1,
            "active": "poco-f5",
            "original_config": {key: original_config[key] for key in legacy_keys},
            "original_base": {key: original_base[key] for key in legacy_keys},
        }
        self.state.write_text(json.dumps(legacy_state), encoding="utf-8")

        self.assertEqual(device_profile.apply("poco-f5-google"), 0)
        migrated = json.loads(self.state.read_text(encoding="utf-8"))
        self.assertEqual(migrated["version"], device_profile.STATE_VERSION)
        self.assertEqual(
            migrated["original_config"]["ro.build.fingerprint"],
            original_config["ro.build.fingerprint"],
        )
        self.assertEqual(
            migrated["original_base"]["ro.build.fingerprint"],
            original_base["ro.build.fingerprint"],
        )

        self.assertEqual(device_profile.apply("native"), 0)
        restored_config = device_profile.extract_config_values(
            self.config.read_text(encoding="utf-8"))
        restored_base = device_profile.extract_prop_values(
            self.base.read_text(encoding="utf-8"))
        self.assertEqual(restored_config, original_config)
        self.assertEqual(restored_base, original_base)

    def test_version_025_dangerous_first_api_override_is_retired(self) -> None:
        original_config = device_profile.extract_config_values(
            self.config.read_text(encoding="utf-8"))
        original_base = device_profile.extract_prop_values(
            self.base.read_text(encoding="utf-8"))
        state = {
            "version": device_profile.STATE_VERSION,
            "active": "poco-f5-google",
            "original_config": original_config,
            "original_base": original_base,
        }
        self.state.write_text(json.dumps(state), encoding="utf-8")
        current = dict(device_profile.PROFILES["poco-f5-google"])
        current["ro.product.first_api_level"] = "33"
        self.config.write_text(device_profile.replace_config_values(
            self.config.read_text(encoding="utf-8"), current), encoding="utf-8")
        self.base.write_text(device_profile.replace_prop_values(
            self.base.read_text(encoding="utf-8"), current), encoding="utf-8")

        self.assertEqual(device_profile.apply("poco-f5-google"), 0)
        applied_config = device_profile.extract_config_values(
            self.config.read_text(encoding="utf-8"))
        applied_base = device_profile.extract_prop_values(
            self.base.read_text(encoding="utf-8"))
        self.assertNotIn("ro.product.first_api_level", applied_config)
        self.assertNotIn("ro.product.first_api_level", applied_base)

        self.assertEqual(device_profile.apply("native"), 0)
        restored_config = device_profile.extract_config_values(
            self.config.read_text(encoding="utf-8"))
        restored_base = device_profile.extract_prop_values(
            self.base.read_text(encoding="utf-8"))
        self.assertEqual(restored_config["ro.product.first_api_level"], "28")
        self.assertEqual(restored_base["ro.product.first_api_level"], "28")


if __name__ == "__main__":
    unittest.main()
