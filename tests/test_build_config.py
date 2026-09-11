"""Reject mixed-core or corrupted archives before staging and distribution."""
from pathlib import Path
import hashlib
import json
import sys
import tempfile
import unittest
from unittest.mock import patch

sys.path.insert(0, str(Path(__file__).resolve().parents[1] / 'tools'))
import build_config


class ArchiveSelectionTests(unittest.TestCase):
    def setUp(self):
        self.temp = tempfile.TemporaryDirectory()
        self.addCleanup(self.temp.cleanup)
        self.root = Path(self.temp.name)
        self.core = '3.3.10'
        self.archive = self.root / 'src/esp32/libsinric_webrtc.a'
        self.archive.parent.mkdir(parents=True)
        self.archive.write_bytes(b'archive payload')
        self.manifest = {
            'arduino_core': self.core,
            'target': 'esp32',
            'profile': build_config.profile(self.core),
            'sha256': hashlib.sha256(self.archive.read_bytes()).hexdigest(),
        }
        self.patch = patch.object(build_config, 'library_dir', return_value=self.root)
        self.patch.start()
        self.addCleanup(self.patch.stop)

    def verify(self):
        self.archive.with_name('build-info.json').write_text(json.dumps(self.manifest))
        return build_config.verify_archive(self.core, 'esp32')

    def test_matching_archive(self):
        self.assertEqual(self.verify(), self.archive)

    def test_rejects_other_core(self):
        self.manifest['arduino_core'] = '3.3.11'
        with self.assertRaisesRegex(ValueError, 'arduino_core'):
            self.verify()

    def test_rejects_other_processor(self):
        self.manifest['target'] = 'esp32s3'
        with self.assertRaisesRegex(ValueError, 'target'):
            self.verify()

    def test_rejects_stale_dependency_profile(self):
        self.manifest['profile'] = build_config.profile('3.3.11')
        with self.assertRaisesRegex(ValueError, 'profile'):
            self.verify()

    def test_rejects_changed_archive_bytes(self):
        self.archive.write_bytes(b'changed archive payload')
        with self.assertRaisesRegex(ValueError, 'checksum'):
            self.verify()

    def test_package_excludes_platformio_outputs(self):
        for relative in ('examples/PlatformIO/.pio/build/firmware.bin',
                         'tools/__pycache__/cached.pyc', 'examples/Doorbell/Settings.h'):
            path = self.root / relative
            path.parent.mkdir(parents=True, exist_ok=True)
            path.write_text('test')
        files = {p.relative_to(self.root).as_posix() for p in build_config.package_files(self.root)}
        self.assertIn('examples/Doorbell/Settings.h', files)
        self.assertNotIn('examples/PlatformIO/.pio/build/firmware.bin', files)
        self.assertNotIn('tools/__pycache__/cached.pyc', files)


if __name__ == '__main__':
    unittest.main()
