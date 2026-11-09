"""Integration checks against a built helper and a signed AppImage fixture.

Usage: python3 tst_appimage_helper.py HELPER SIGNED_APPIMAGE
Fixtures and keys are disposable; never use a production signing key here.
"""
import hashlib
import json
import os
from pathlib import Path
import shutil
import subprocess
import sys
import tempfile
import unittest
import time
import uuid

HELPER, SIGNED = map(lambda p: str(Path(p).resolve()), sys.argv[1:3])
del sys.argv[1:3]
TEST_HELPER = os.environ.get("SNAPTRAY_TEST_HELPER")


class HelperIntegration(unittest.TestCase):
    def setUp(self):
        self.temp = tempfile.TemporaryDirectory(prefix="snaptray-helper-test-")
        self.addCleanup(self.temp.cleanup)
        self.image = Path(self.temp.name) / "中文 renamed image.AppImage"
        shutil.copy2(SIGNED, self.image)
        self.stage = Path(str(self.image) + ".snaptray-update")
        self.stage.mkdir(mode=0o700)
        self.candidate = self.stage / "candidate.AppImage"
        shutil.copy2(SIGNED, self.candidate)
        digest = hashlib.sha256(self.image.read_bytes()).hexdigest()
        self.pending = {"version": "1.2.3", "oldHash": digest, "newHash": digest}
        self.write_pending()

    def write_pending(self):
        (self.stage / "pending.json").write_text(json.dumps(self.pending))

    def run_helper(self, command):
        result = subprocess.run([HELPER, command, str(self.image)], capture_output=True, text=True, timeout=15)
        events = [json.loads(line) for line in result.stdout.splitlines()]
        return result.returncode, events[-1]

    def test_signed_pending(self):
        code, event = self.run_helper("pending")
        self.assertEqual(code, 0, event)
        self.assertEqual(event, {"event": "ready", "version": "1.2.3"})

    def test_changed_download_rejected(self):
        with self.candidate.open("ab") as file:
            file.write(b"corrupt")
        code, event = self.run_helper("pending")
        self.assertNotEqual(code, 0)
        self.assertIn("changed", event["message"])
        self.assertEqual(hashlib.sha256(self.image.read_bytes()).hexdigest(), self.pending["oldHash"])

    def test_matching_hash_does_not_bypass_signature(self):
        with self.candidate.open("ab") as file:
            file.write(b"corrupt")
        self.pending["newHash"] = hashlib.sha256(self.candidate.read_bytes()).hexdigest()
        self.write_pending()
        code, event = self.run_helper("pending")
        self.assertNotEqual(code, 0)
        self.assertIn("signature", event["message"])

    def test_changed_installed_file_rejected(self):
        with self.image.open("ab") as file:
            file.write(b"external update")
        code, event = self.run_helper("pending")
        self.assertNotEqual(code, 0)
        self.assertIn("installed AppImage changed", event["message"])

    def test_symlink_staging_rejected(self):
        shutil.rmtree(self.stage)
        elsewhere = Path(self.temp.name) / "elsewhere"
        elsewhere.mkdir()
        self.stage.symlink_to(elsewhere, target_is_directory=True)
        code, event = self.run_helper("pending")
        self.assertNotEqual(code, 0)
        self.assertIn("Unsafe", event["message"])

    def test_discard_keeps_original(self):
        code, event = self.run_helper("discard")
        self.assertEqual(code, 0)
        self.assertEqual(event["event"], "discarded")
        self.assertFalse(self.candidate.exists())
        self.assertFalse((self.stage / "pending.json").exists())
        self.assertTrue(self.image.exists())

    def test_apply_waits_for_application_exit(self):
        token = str(uuid.uuid4())
        process = subprocess.Popen([HELPER, "apply", str(self.image), str(os.getpid()), token],
                                   stdout=subprocess.PIPE, stderr=subprocess.PIPE, text=True)
        try:
            armed = self.stage / ("armed-" + token)
            deadline = time.monotonic() + 10
            while not armed.exists() and process.poll() is None and time.monotonic() < deadline:
                time.sleep(0.05)
            self.assertTrue(armed.exists())
            self.assertEqual(hashlib.sha256(self.image.read_bytes()).hexdigest(), self.pending["oldHash"])
            self.assertFalse(Path(str(self.image) + ".snaptray-backup").exists())
            process.terminate()
            output, _ = process.communicate(timeout=5)
            self.assertIn("did not exit", output)
            self.assertFalse((self.stage / "transaction.json").exists())
        finally:
            if process.poll() is None:
                process.kill()
                process.communicate()

    def apply_with_runner(self, fail):
        runner = Path(self.temp.name) / "runner"
        runner.write_text("#!/usr/bin/python3\n" +
            "import os,sys,socket,time\n" +
            "from pathlib import Path\n" +
            "if not os.environ.get('SNAPTRAY_UPDATE_ACK'): sys.exit(0)\n" +
            "Path(sys.argv[1]+'.test-pid').write_text(str(os.getpid()))\n" +
            ("sys.exit(1)\n" if fail else
             "s=socket.socket(socket.AF_UNIX); s.connect('/tmp/'+os.environ['SNAPTRAY_UPDATE_ACK']); s.sendall(os.environ['SNAPTRAY_UPDATE_TOKEN'].encode()); s.close(); time.sleep(60)\n"))
        runner.chmod(0o700)
        parent = subprocess.Popen(["/bin/true"])
        parent.wait()
        environment = dict(os.environ, SNAPTRAY_TEST_RUNNER=str(runner))
        try:
            result = subprocess.run([TEST_HELPER, "apply", str(self.image), str(parent.pid), str(uuid.uuid4())],
                                    env=environment, capture_output=True, text=True, timeout=45)
            return result
        finally:
            pidfile = Path(str(self.image) + ".test-pid")
            if pidfile.exists():
                try:
                    os.kill(int(pidfile.read_text()), 15)
                except ProcessLookupError:
                    pass

    @unittest.skipUnless(TEST_HELPER, "Set SNAPTRAY_TEST_HELPER to test restart transactions without FUSE")
    def test_apply_success_retains_backup(self):
        result = self.apply_with_runner(False)
        self.assertEqual(result.returncode, 0, result.stdout + result.stderr)
        self.assertEqual(json.loads(result.stdout.splitlines()[-1])["event"], "installed")
        self.assertTrue(Path(str(self.image) + ".snaptray-backup").exists())
        self.assertFalse((self.stage / "transaction.json").exists())
        self.assertFalse((self.stage / "pending.json").exists())

    @unittest.skipUnless(TEST_HELPER, "Set SNAPTRAY_TEST_HELPER to test restart transactions without FUSE")
    def test_failed_startup_restores_old_file(self):
        result = self.apply_with_runner(True)
        self.assertNotEqual(result.returncode, 0)
        self.assertIn("previous version was restored", result.stdout)
        self.assertEqual(hashlib.sha256(self.image.read_bytes()).hexdigest(), self.pending["oldHash"])
        self.assertFalse((self.stage / "transaction.json").exists())

    def test_interrupted_install_restores_backup(self):
        backup = Path(str(self.image) + ".snaptray-backup")
        self.image.rename(backup)
        self.image.write_bytes(b"interrupted new version")
        self.pending["newHash"] = hashlib.sha256(self.image.read_bytes()).hexdigest()
        (self.stage / "transaction.json").write_text(json.dumps(self.pending))
        code, event = self.run_helper("pending")
        self.assertNotEqual(code, 0)
        self.assertIn("rolled back", event["message"])
        self.assertEqual(hashlib.sha256(self.image.read_bytes()).hexdigest(), self.pending["oldHash"])
        self.assertFalse((self.stage / "transaction.json").exists())


if __name__ == "__main__":
    unittest.main()
