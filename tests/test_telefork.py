import argparse
import importlib.util
import json
from pathlib import Path
import subprocess
import tempfile
import unittest
from unittest.mock import patch

spec = importlib.util.spec_from_file_location('telefork', Path(__file__).parents[1] / 'tools/telefork.py')
tf = importlib.util.module_from_spec(spec)
spec.loader.exec_module(tf)
INFO = {'format': 1, 'machine': 'x86_64', 'page_size': 4096, 'criu': 'Version: 4.2'}


class CheckpointTests(unittest.TestCase):
    def test_unsupported_platform(self):
        with patch.object(tf.platform, 'system', return_value='Darwin'):
            with self.assertRaisesRegex(ValueError, 'Linux'):
                tf.check()

    def test_destination_validation(self):
        for host in ('-oProxyCommand=bad', 'host;id', 'x:y', 'user@-host', '$(id)'):
            with self.assertRaises(argparse.ArgumentTypeError):
                tf.destination(host)
        self.assertEqual(tf.destination('alice@vm-2.local'), 'alice@vm-2.local')

    def test_incompatible_metadata(self):
        for key in INFO:
            with self.assertRaises(ValueError):
                tf.compatible(INFO, {**INFO, key: 'different'})

    def test_malformed_metadata(self):
        for metadata in ([], None, {}, {'format': 1}):
            with self.assertRaises(ValueError):
                tf.compatible(metadata, INFO)

    def test_clone_success_orders_transfer_before_restore(self):
        args = argparse.Namespace(host='vm', remote_helper='/opt/telefork/tools/telefork.py', pid=12345, images='unused')
        events = []
        def remote(host, helper, command, *rest):
            events.append(command)
            return subprocess.CompletedProcess([], 0, json.dumps(INFO) if command == 'check' else '{"pid":12345}\n')
        def run(command, **kwargs):
            events.append(command[0])
        with tempfile.TemporaryDirectory() as tmp, patch.object(tf, 'remote', side_effect=remote), patch.object(tf, 'dump', return_value=Path(tmp)) as dump, patch.object(tf, 'run', side_effect=run):
            (Path(tmp) / 'image.img').touch()
            tf.clone(args, INFO)
            dump.assert_called_once_with(12345, 'unused', INFO)
        self.assertEqual(events, ['check', 'ssh', 'scp', 'restore'])

    def test_failed_dump_has_no_manifest(self):
        with tempfile.TemporaryDirectory() as tmp, patch.object(tf.os, 'kill'), patch.object(tf, 'run', side_effect=subprocess.CalledProcessError(1, 'criu')):
            path = Path(tmp) / 'images'
            with self.assertRaises(subprocess.CalledProcessError):
                tf.dump(12345, path, INFO)
            self.assertTrue(path.is_dir())
            self.assertFalse((path / 'telefork.json').exists())

    def test_dump_preserves_source_and_refuses_overwrite(self):
        with tempfile.TemporaryDirectory() as tmp, patch.object(tf.os, 'kill'), patch.object(tf, 'run') as run:
            path = tf.dump(12345, Path(tmp) / 'images', INFO)
            self.assertIn('--leave-running', run.call_args.args[0])
            self.assertEqual(json.loads((path / 'telefork.json').read_text()), INFO)
            self.assertEqual(path.stat().st_mode & 0o777, 0o700)
            with self.assertRaises(FileExistsError):
                tf.dump(12345, path, INFO)

    def test_restore_validates_before_criu(self):
        with tempfile.TemporaryDirectory() as tmp, patch.object(tf, 'run') as run:
            (Path(tmp) / 'telefork.json').write_text(json.dumps(INFO))
            with self.assertRaises(ValueError):
                tf.restore(tmp, {**INFO, 'machine': 'aarch64'})
            run.assert_not_called()

    def test_restore_reads_result(self):
        with tempfile.TemporaryDirectory() as tmp:
            (Path(tmp) / 'telefork.json').write_text(json.dumps(INFO))
            def fake_run(command, **kwargs):
                (Path(tmp) / 'restored.pid').write_text('12345\n')
            with patch.object(tf, 'run', side_effect=fake_run):
                self.assertEqual(tf.restore(tmp, INFO), 12345)

    def test_remote_failure_prevents_dump(self):
        args = argparse.Namespace(host='vm', remote_helper='/opt/telefork/tools/telefork.py', pid=12345, images='unused')
        with patch.object(tf, 'remote', side_effect=subprocess.CalledProcessError(1, 'ssh')), patch.object(tf, 'dump') as dump:
            with self.assertRaises(subprocess.CalledProcessError):
                tf.clone(args, INFO)
            dump.assert_not_called()

    def test_transfer_failure_prevents_restore(self):
        args = argparse.Namespace(host='vm', remote_helper='/opt/telefork/tools/telefork.py', pid=12345, images='unused')
        with tempfile.TemporaryDirectory() as tmp, patch.object(tf, 'remote', return_value=subprocess.CompletedProcess([], 0, json.dumps(INFO))) as remote, patch.object(tf, 'dump', return_value=Path(tmp)), patch.object(tf, 'run', side_effect=[None, subprocess.CalledProcessError(1, 'scp')]):
            (Path(tmp) / 'image.img').touch()
            with self.assertRaises(subprocess.CalledProcessError):
                tf.clone(args, INFO)
            self.assertEqual(remote.call_count, 1)


if __name__ == '__main__':
    unittest.main()
