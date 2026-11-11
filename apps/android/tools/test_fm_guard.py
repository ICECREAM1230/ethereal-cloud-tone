"""Read-only drift checks against isolated copies of the imported contract."""
import hashlib
import json
import os
import pathlib
import shutil
import subprocess
import sys
import tempfile
import unittest
from toolchain import ROOT
from fm_contract import CONTRACT_DIRECTORY

class ContractGuardTest(unittest.TestCase):
    def setUp(self):
        scratch = ROOT / 'build/fm-current/guard'
        scratch.mkdir(parents=True, exist_ok=True)
        self.copy = pathlib.Path(tempfile.mkdtemp(dir=scratch))
        self.bundle = self.copy / CONTRACT_DIRECTORY
        shutil.copytree(ROOT / CONTRACT_DIRECTORY, self.bundle)
        self.java = self.copy / 'core/src/generated/java/cn/fpgasynth/control/generated/FmContract.java'
        self.java.parent.mkdir(parents=True)
        shutil.copy2(ROOT / self.java.relative_to(self.copy), self.java)

    def check_guard(self, expected):
        before = {str(f.relative_to(self.copy)): hashlib.sha256(f.read_bytes()).hexdigest()
                  for f in self.copy.rglob('*') if f.is_file()}
        r = subprocess.run([sys.executable, '-B', str(ROOT / 'tools/fm_contract.py'), '--root', str(self.copy)],
                           env=dict(os.environ, PYTHONDONTWRITEBYTECODE='1'), capture_output=True, text=True)
        self.assertEqual(r.returncode, expected, r.stdout + r.stderr)
        after = {str(f.relative_to(self.copy)): hashlib.sha256(f.read_bytes()).hexdigest()
                 for f in self.copy.rglob('*') if f.is_file()}
        self.assertEqual(before, after, 'check must never repair or create contract files')

    def test_valid(self):
        self.check_guard(0)

    def test_schema_drift(self):
        with (self.bundle / 'schema/fm-control.schema.json').open('a') as f: f.write(' ')
        self.check_guard(1)

    def test_profiles_drift(self):
        with (self.bundle / 'schema/backend-profiles.json').open('a') as f: f.write(' ')
        self.check_guard(1)

    def test_generator_drift(self):
        with (self.bundle / 'tools/fm_codegen_v1.py').open('a') as f: f.write('\n# drift\n')
        self.check_guard(1)

    def test_reference_generated_drift(self):
        with (self.bundle / 'generated/java/cn/fpgasynth/control/generated/FmContract.java').open('a') as f: f.write('\n')
        self.check_guard(1)

    def test_app_generated_drift(self):
        with self.java.open('a') as f: f.write('\n')
        self.check_guard(1)

    def test_app_generated_missing(self):
        self.java.unlink()
        self.check_guard(1)

    def test_app_generated_extra(self):
        (self.java.parent / 'OldContract.java').write_text('class OldContract {}\n')
        self.check_guard(1)

    def test_bundle_extra(self):
        (self.bundle / 'unexpected.txt').write_text('extra\n')
        self.check_guard(1)

    def test_manifest_drift(self):
        with (self.bundle / 'manifest.json').open('a') as f: f.write(' ')
        self.check_guard(1)

    def test_resealed_profile_and_manifest_are_not_authorized_identity(self):
        profile = self.bundle / 'schema/backend-profiles.json'
        with profile.open('a') as f: f.write(' ')
        manifest_path = self.bundle / 'manifest.json'
        manifest = json.loads(manifest_path.read_text())
        digest = hashlib.sha256(profile.read_bytes()).hexdigest()
        manifest['profiles_sha256'] = digest
        manifest['combined_contract_sha256'] = hashlib.sha256(
            b'FM_CONTROL_IDENTITY_V1\0' + bytes.fromhex(manifest['schema_sha256']) + bytes.fromhex(digest)).hexdigest()
        for item in manifest['files']:
            if item['path'] == 'schema/backend-profiles.json':
                item.update(bytes=profile.stat().st_size, sha256=digest)
        manifest_path.write_text(json.dumps(manifest) + '\n')
        self.check_guard(1)

if __name__ == '__main__':
    unittest.main(verbosity=2)
