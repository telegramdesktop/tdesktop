from pathlib import Path
import subprocess
import tempfile
import unittest

import watch


class VersionTests(unittest.TestCase):
    def test_stable_ordering_and_prefixes(self):
        cases = [
            ('v1.23.5', 'v1.23.10', 'patch'),
            ('n6.1.6', 'n6.2.0', 'minor'),
            ('openssl-3.2.1', 'openssl-4.0.0', 'major'),
            ('1.2.9', '1.2.8', 'older'),
            ('v1.2.3', '1.2.3', 'same'),
            ('v1.2.3', 'v1.2.4-rc1', 'unknown'),
            ('e2d0e88d', '48bc60bdb1', 'unknown'),
            ('M123', 'M140', 'unknown'),
            ('1.2.3', '1.2.3.4', 'unknown'),
            ('1.1.1w', '3.0.0', 'unknown'),
        ]
        for before, after, expected in cases:
            with self.subTest(before=before, after=after):
                self.assertEqual(watch.compare(before, after), expected)


class SnapshotTests(unittest.TestCase):
    def setUp(self):
        self.temp = tempfile.TemporaryDirectory()
        self.addCleanup(self.temp.cleanup)
        self.root = Path(self.temp.name)
        self.remote = self.root / 'remote'
        self.repo = self.root / 'checkout'
        self.remote.mkdir()
        watch.git(self.remote, 'init', '-b', 'dev')
        watch.git(self.remote, 'config', 'user.name', 'Fixture')
        watch.git(self.remote, 'config', 'user.email', 'fixture@example.invalid')
        for path in watch.REQUIRED:
            target = self.remote / path
            target.parent.mkdir(parents=True, exist_ok=True)
            target.write_text('fixture\n', encoding='utf-8')
        self.commit()
        watch.git(self.root, 'clone', str(self.remote), str(self.repo))

    def commit(self):
        watch.git(self.remote, 'add', '.')
        watch.git(self.remote, 'commit', '-m', 'Fixture')

    def test_fetches_remote_without_touching_working_tree(self):
        manifest = 'Telegram/build/prepare/prepare.py'
        pinned = "git clone -b v1.2.3 https://example.invalid/dependency.git\n"
        (self.remote / manifest).write_text(pinned, encoding='utf-8')
        oid = watch.git(self.remote, 'rev-parse', 'HEAD').decode().strip()
        watch.git(self.remote, 'add', manifest)
        watch.git(self.remote, 'update-index', '--add', '--cacheinfo', '160000,' + oid + ',third-party')
        watch.git(self.remote, 'commit', '-m', 'Fixture')
        (self.repo / manifest).write_text('local unfinished edit\n', encoding='utf-8')
        before = watch.git(self.repo, 'status', '--porcelain')
        head = watch.git(self.repo, 'rev-parse', 'HEAD')
        result = watch.snapshot(self.repo)
        source = Path(result['run_directory']) / 'sources' / manifest
        self.assertEqual(source.read_text(), pinned)
        self.assertTrue(result['fresh_fetch'])
        self.assertEqual(result['commit'], watch.git(self.remote, 'rev-parse', 'HEAD').decode().strip())
        self.assertEqual(result['gitlinks'], [{'path': 'third-party', 'object': oid}])
        self.assertEqual(watch.git(self.repo, 'status', '--porcelain'), before)
        self.assertEqual(watch.git(self.repo, 'rev-parse', 'HEAD'), head)
        self.assertFalse((Path(result['storage_root']) / 'latest.md').exists())

    def test_fetch_failure_never_uses_stale_ref(self):
        watch.git(self.repo, 'remote', 'set-url', 'origin', str(self.root / 'missing'))
        with self.assertRaises(subprocess.CalledProcessError):
            watch.snapshot(self.repo)
        self.assertFalse((self.repo / '.git/dependency-watch').exists())

    def test_missing_required_manifest_fails(self):
        (self.remote / 'snap/snapcraft.yaml').unlink()
        self.commit()
        with self.assertRaises(ValueError):
            watch.snapshot(self.repo)

    def test_offline_snapshot_is_marked_unverified(self):
        result = watch.snapshot(self.repo, fetch=False)
        self.assertFalse(result['fresh_fetch'])
        self.assertEqual(result['audit_status'], 'snapshot-only')

    def test_linked_worktree_uses_common_report_storage(self):
        linked = self.root / 'linked'
        watch.git(self.repo, 'worktree', 'add', '--detach', str(linked))
        result = watch.snapshot(linked, fetch=False)
        self.assertEqual(
            Path(result['storage_root']),
            (self.repo / '.git/dependency-watch').resolve(),
        )


if __name__ == '__main__':
    unittest.main()
