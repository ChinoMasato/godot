"""Failure-path and exact-frame tests. No renderer is started by this suite."""
import contextlib
import io
import json
import os
from pathlib import Path
import subprocess
import tempfile
import unittest
from unittest.mock import patch, Mock

import picture
from PIL import Image


def process_result(stdout='', code=0, stderr=''):
    return {'ok': code == 0 and 'ERROR:' not in stderr, 'exit_code': code,
            'stdout': stdout, 'stderr': stderr, 'errors': [stderr] if stderr else []}


class PictureCLI(unittest.TestCase):
    def setUp(self):
        self.temp = tempfile.TemporaryDirectory(prefix='uzu_cli_test_')
        self.addCleanup(self.temp.cleanup)
        self.project = Path(self.temp.name)
        (self.project / 'project.godot').write_text('config_version=5\n', encoding='utf-8')

    def cli(self, *args):
        stream = io.StringIO()
        with contextlib.redirect_stdout(stream):
            code = picture.main([str(self.project), *args])
        lines = stream.getvalue().splitlines()
        self.assertEqual(len(lines), 1)
        return code, json.loads(lines[0])

    def test_crash_after_success_json_is_failure(self):
        with patch.object(picture, 'run_process', return_value=process_result('UZU_RESULT {"ok":true}\n', 7)):
            code, result = self.cli('map', 'import')
        self.assertEqual(code, 1)
        self.assertFalse(result['ok'])
        self.assertEqual(result['exit_code'], 7)

    def test_stderr_error_overrides_success_json(self):
        with patch.object(picture, 'run_process', return_value=process_result('UZU_RESULT {"ok":true}\n', stderr='ERROR: device lost')):
            code, result = self.cli('pbr', 'make')
        self.assertEqual(code, 1)
        self.assertFalse(result['ok'])

    def test_domain_errors_stay_structured(self):
        text = 'UZU_RESULT {"ok":false,"problems":["missing depth"]}\n'
        with patch.object(picture, 'run_process', return_value=process_result(text, 1)):
            code, result = self.cli('validate')
        self.assertEqual(code, 1)
        self.assertEqual(result['problems'], ['missing depth'])

    def test_bad_json_missing_result_and_nonboolean_are_failures(self):
        for text in ['', 'UZU_RESULT {oops}', 'UZU_RESULT []', 'UZU_RESULT {"ok":"yes"}',
                     'UZU_RESULT {"ok":true}\nUZU_RESULT {"ok":true}']:
            with self.subTest(text=text), patch.object(picture, 'run_process', return_value=process_result(text)):
                code, result = self.cli('schema')
                self.assertEqual(code, 1)
                self.assertFalse(result['ok'])

    def test_scripts_resolve_from_tool_not_target_project(self):
        with patch.object(picture, 'run_process', return_value=process_result('UZU_RESULT {"ok":true}')) as run:
            self.cli('map', 'import')
        command = run.call_args.args[0]
        self.assertEqual(command[command.index('--script') + 1], str(picture.HERE / 'map_tool.gd'))

    def test_missing_executable_is_json_error(self):
        with patch.object(picture.subprocess, 'Popen', side_effect=FileNotFoundError('not installed')):
            code, result = self.cli('schema')
        self.assertEqual(code, 1)
        self.assertEqual(result['error'], 'could not start engine')

    def test_timeout_terminates_owned_process(self):
        child = Mock(pid=12345, returncode=-9)
        child.communicate.side_effect = [subprocess.TimeoutExpired('engine', 1), ('partial output', '')]
        child.poll.return_value = None
        with patch.object(picture.subprocess, 'Popen', return_value=child), \
                patch.object(picture.subprocess, 'run') as kill_tree, patch.object(picture.os, 'killpg', create=True) as kill_group:
            code, result = self.cli('schema')
        self.assertEqual(code, 1)
        self.assertEqual(result['error'], 'engine timed out')
        child.kill.assert_called_once()
        if os.name == 'nt':
            self.assertIn('12345', kill_tree.call_args.args[0])
        else:
            self.assertEqual(kill_group.call_args.args[0], 12345)
        self.assertIn('partial output', result['log'])

    def test_bad_times_and_missing_values_do_not_start_engine(self):
        for args in [('shot', '--fps', '0'), ('shot', '--seconds', '-1'), ('shot', '--seconds', 'nan'),
                     ('shot', '--seconds', 'inf'), ('shot', '--fps'), ('shot', '--out', '--seconds', '1')]:
            with self.subTest(args=args), patch.object(picture, 'run_process') as run:
                code, result = self.cli(*args)
                self.assertEqual(code, 1)
                run.assert_not_called()

    @unittest.skipUnless(os.name == 'nt', 'Windows process-tree cleanup')
    def test_tree_cleanup_failure_still_kills_parent(self):
        child = Mock(pid=12345, returncode=-9)
        child.communicate.side_effect = [subprocess.TimeoutExpired('engine', 1), ('', '')]
        child.poll.return_value = None
        with patch.object(picture.subprocess, 'Popen', return_value=child), \
                patch.object(picture.subprocess, 'run', side_effect=FileNotFoundError('taskkill unavailable')):
            code, result = self.cli('schema')
        self.assertEqual(code, 1)
        self.assertIn('taskkill', result['cleanup_warning'])
        child.kill.assert_called_once()

    def fake_render(self, indices, code=0, corrupt=False):
        def render(command):
            folder = Path(command[command.index('--write-movie') + 1]).parent
            for index in indices:
                path = folder / f'f{index:08d}.png'
                if corrupt:
                    path.write_bytes(b'not a PNG')
                else:
                    Image.new('RGB', (4, 4), (index, 0, 0)).save(path)
            return process_result(code=code)
        return render

    def test_incomplete_render_is_not_clamped_to_last_frame(self):
        with patch.object(picture, 'tool', return_value={'ok': True}), \
                patch.object(picture, 'run_process', side_effect=self.fake_render([0])):
            code, result = self.cli('shot', '--seconds', '0,1', '--out', str(self.project / 'shots'))
        self.assertEqual(code, 1)
        self.assertEqual(result['missing_frames'], [30])
        self.assertFalse((self.project / 'shots/t1s.png').exists())

    def test_render_exit_failure_does_not_publish_images(self):
        with patch.object(picture, 'tool', return_value={'ok': True}), \
                patch.object(picture, 'run_process', side_effect=self.fake_render([0, 30], code=7)):
            code, result = self.cli('shot', '--seconds', '0,1', '--out', str(self.project / 'shots'))
        self.assertEqual(code, 1)
        self.assertFalse(result['ok'])
        self.assertFalse((self.project / 'shots/t0s.png').exists())

    def test_exact_frames_and_motion_file_exist_on_success(self):
        with patch.object(picture, 'tool', return_value={'ok': True}), \
                patch.object(picture, 'run_process', side_effect=self.fake_render([0, 30])):
            code, result = self.cli('shot', '--seconds', '0,1', '--out', str(self.project / 'shots'))
        self.assertEqual(code, 0)
        self.assertEqual(result['frame_indices'], [0, 30])
        self.assertTrue(all(Path(path).is_file() for path in result['images']))
        self.assertTrue(Path(result['motion']).is_file())
        with Image.open(result['images'][1]) as image:
            self.assertEqual(image.getpixel((0, 0)), (30, 0, 0))

    def test_corrupt_png_does_not_publish_images(self):
        with patch.object(picture, 'tool', return_value={'ok': True}), \
                patch.object(picture, 'run_process', side_effect=self.fake_render([0], corrupt=True)):
            code, result = self.cli('shot', '--seconds', '0', '--out', str(self.project / 'shots'))
        self.assertEqual(code, 1)
        self.assertFalse(result['ok'])
        self.assertFalse((self.project / 'shots/t0s.png').exists())

    def test_doctor_reports_missing_binary_without_crashing(self):
        with patch.object(picture, 'GODOT', str(self.project / 'absent.exe')):
            code, result = self.cli('doctor')
        self.assertEqual(code, 1)
        self.assertTrue(result['project_found'])
        self.assertEqual(result['missing_scripts'], [])
        self.assertIn('GODOT', result['error'])

    def test_invalid_timeout_is_json_error(self):
        with patch.dict(os.environ, {'UZU_PICTURE_TIMEOUT_SECONDS': 'nan'}):
            code, result = self.cli('schema')
        self.assertEqual(code, 1)
        self.assertIn('finite', result['error'])

    def test_missing_pillow_never_returns_a_fake_motion_path(self):
        with patch.dict('sys.modules', {'PIL': None}), patch.object(picture, 'run_process') as run:
            code, result = self.cli('shot', '--seconds', '0')
        self.assertEqual(code, 1)
        self.assertIn('Pillow', result['error'])
        self.assertNotIn('motion', result)
        run.assert_not_called()


if __name__ == '__main__':
    unittest.main()
