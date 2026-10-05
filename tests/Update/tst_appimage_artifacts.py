import hashlib
import importlib.util
from pathlib import Path
import struct
import tempfile
import unittest
import sys
sys.dont_write_bytecode = True

spec = importlib.util.spec_from_file_location("artifacts", Path(__file__).resolve().parents[2] / "packaging/linux/verify-update-artifacts.py")
artifacts = importlib.util.module_from_spec(spec)
spec.loader.exec_module(artifacts)


class UpdateArtifacts(unittest.TestCase):
    def setUp(self):
        self.temp = tempfile.TemporaryDirectory()
        self.addCleanup(self.temp.cleanup)
        self.image = Path(self.temp.name) / "SnapTray-1.2.3-x86_64.AppImage"
        data = bytearray(1024)
        data[:6] = b"\x7fELF\x02\x01"
        data[8:11] = b"AI\x02"
        struct.pack_into("<Q", data, 40, 64)
        struct.pack_into("<HHH", data, 58, 64, 3, 1)
        names = b"\0.shstrtab\0.upd_info\0"
        struct.pack_into("<IIQQQQIIQQ", data, 128, 1, 3, 0, 0, 256, len(names), 0, 0, 1, 0)
        struct.pack_into("<IIQQQQIIQQ", data, 192, 11, 1, 0, 0, 300, 256, 0, 0, 1, 0)
        # .upd_info starts at byte 11 in this string table.
        data[256:256 + len(names)] = names
        data[300:300 + len(artifacts.UPDATE_INFORMATION)] = artifacts.UPDATE_INFORMATION
        self.image.write_bytes(data)
        self.sync()

    def sync(self):
        data = self.image.read_bytes()
        Path(str(self.image) + ".zsync").write_text(f"Filename: {self.image.name}\nURL: {self.image.name}\nLength: {len(data)}\nSHA-1: {hashlib.sha1(data).hexdigest()}\n\n")

    def test_valid(self):
        artifacts.verify(self.image)

    def test_modified_after_zsync(self):
        data = bytearray(self.image.read_bytes()); data[-1] = 1; self.image.write_bytes(data)
        with self.assertRaisesRegex(ValueError, "checksum"):
            artifacts.verify(self.image)

    def test_wrong_update_source(self):
        self.image.write_bytes(self.image.read_bytes().replace(b"victorfu", b"somebody")); self.sync()
        with self.assertRaisesRegex(ValueError, "update information"):
            artifacts.verify(self.image)

    def test_wrong_length(self):
        self.image.write_bytes(self.image.read_bytes() + b"extra")
        with self.assertRaisesRegex(ValueError, "length"):
            artifacts.verify(self.image)


if __name__ == "__main__":
    unittest.main()
