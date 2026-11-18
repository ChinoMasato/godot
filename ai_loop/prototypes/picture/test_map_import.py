"""Real headless engine checks using synthetic maps; no renderer or AI model needed."""
import json
import math
import os
from pathlib import Path
import subprocess
import sys
import tempfile
import unittest

from PIL import Image

HERE = Path(__file__).resolve().parent
DEFAULT = HERE.parents[3] / 'godot/bin/uzu-engine.windows.editor.x86_64.exe'
BINARY = os.environ.get('GODOT', str(DEFAULT))
if os.name == 'nt' and BINARY.endswith('.console.exe'):
    direct = BINARY.replace('.console.exe', '.exe')
    if Path(direct).is_file():
        BINARY = direct


@unittest.skipUnless(Path(BINARY).is_file(), 'set GODOT to a built editor')
class MapImport(unittest.TestCase):
    def setUp(self):
        self.temp = tempfile.TemporaryDirectory(prefix='uzu_map_check_')
        self.addCleanup(self.temp.cleanup)
        self.project = Path(self.temp.name)
        (self.project / 'project.godot').write_text('config_version=5\n', encoding='utf-8')
        Image.new('RGB', (32, 32), (128, 128, 128)).save(self.project / 'picture.png')
        Image.new('RGB', (32, 32), (192, 192, 223)).save(self.project / 'normal.png')
        Image.new('L', (32, 32), 64).save(self.project / 'flat.png')
        image = Image.new('L', (32, 32))
        image.putdata([round(x * 255 / 31) for y in range(32) for x in range(32)])
        image.save(self.project / 'horizontal.png')

    def command(self, *args):
        completed = subprocess.run([BINARY, '--headless', '--path', str(self.project), '--script',
                                    str(HERE / 'map_tool.gd'), '--', *args], capture_output=True,
                                   text=True, encoding='utf-8', errors='replace', timeout=45)
        lines = [line[11:] for line in completed.stdout.splitlines() if line.startswith('UZU_RESULT ')]
        self.assertEqual(len(lines), 1, (completed.stdout + completed.stderr)[-1500:])
        result = json.loads(lines[0])
        self.assertEqual(completed.returncode == 0, result['ok'], result)
        return result

    def import_map(self, source='horizontal.png', kind='depth', out='out.png', extra=()):
        return self.command('import', '--kind', kind, '--in', 'res://' + source,
                            '--for', 'res://picture.png', '--out', 'res://' + out, '--no-align', *extra)

    def pixels(self, name):
        with Image.open(self.project / name) as image:
            return image.copy()

    def test_ambiguous_depth_does_not_invert(self):
        result = self.import_map()
        self.assertTrue(result['ok'])
        self.assertEqual(result['found']['white_is'], 'unsure')
        self.assertTrue(result['warnings'])
        self.assertLess(self.pixels('out.png').getpixel((0, 16)), 3)
        self.assertGreater(self.pixels('out.png').getpixel((31, 16)), 252)

    def test_flat_depth_keeps_value_instead_of_becoming_white(self):
        result = self.import_map('flat.png')
        self.assertTrue(result['ok'])
        self.assertTrue(result['warnings'])
        self.assertAlmostEqual(self.pixels('out.png').getpixel((16, 16)), 64, delta=1)

    def test_auto_depth_direction_still_handles_ground_and_sky(self):
        for white_is in ['near', 'far']:
            with self.subTest(white_is=white_is):
                values = [round(y * 255 / 31) for y in range(32) for x in range(32)]
                if white_is == 'far':
                    values = [255 - value for value in values]
                source = Image.new('L', (32, 32))
                source.putdata(values)
                source.save(self.project / 'vertical.png')
                result = self.import_map('vertical.png')
                self.assertTrue(result['ok'])
                self.assertEqual(result['found']['white_is'], white_is)
                self.assertEqual(result['found']['output_white_is'], 'near')
                self.assertLess(self.pixels('out.png').getpixel((16, 0)), 3)
                self.assertGreater(self.pixels('out.png').getpixel((16, 31)), 252)

    def test_explicit_depth_far_inverts_and_near_does_not(self):
        for direction, left in [('far', 255), ('near', 0)]:
            with self.subTest(direction=direction):
                result = self.import_map(out=direction + '.png', extra=('--white-is', direction))
                self.assertTrue(result['ok'])
                self.assertEqual(result['found']['direction_source'], 'explicit')
                self.assertAlmostEqual(self.pixels(direction + '.png').getpixel((0, 16)), left, delta=1)

    def test_explicit_normal_convention_overrides_weak_shading(self):
        for convention, positive in [('opengl', True), ('directx', False)]:
            with self.subTest(convention=convention):
                result = self.import_map('normal.png', 'normal', convention + '.png',
                                         ('--normal-convention', convention))
                self.assertTrue(result['ok'])
                self.assertEqual(result['found']['convention_source'], 'explicit')
                color = self.pixels(convention + '.png').getpixel((16, 16))
                self.assertEqual(color[1] > 128, positive)
                self.assertGreater(color[0], 128)

    def test_auto_normal_convention_still_detects_both_formats(self):
        def height(x, y):
            return 0.5 + 0.15 * math.sin(x * 0.3) + 0.12 * math.cos(y * 0.25)
        albedo = Image.new('L', (32, 32))
        albedo.putdata([round(255 * height(x, y)) for y in range(32) for x in range(32)])
        albedo.save(self.project / 'picture.png')
        for convention in ['opengl', 'directx']:
            with self.subTest(convention=convention):
                colors = []
                for y in range(32):
                    for x in range(32):
                        dx = -3 * (height(x + 1, y) - height(x - 1, y))
                        dy = 3 * (height(x, y + 1) - height(x, y - 1))
                        length = math.sqrt(dx * dx + dy * dy + 1)
                        color = [round(127.5 * (dx / length + 1)), round(127.5 * (dy / length + 1)), round(127.5 * (1 / length + 1))]
                        if convention == 'directx':
                            color[1] = 255 - color[1]
                        colors.append(tuple(color))
                image = Image.new('RGB', (32, 32))
                image.putdata(colors)
                image.save(self.project / 'auto_normal.png')
                result = self.import_map('auto_normal.png', 'normal')
                self.assertTrue(result['ok'])
                self.assertEqual(result['found']['convention'], convention)
                self.assertEqual(result['found']['convention_source'], 'auto')

    def test_unknown_options_and_conflicting_directions_fail(self):
        for extra in [('--depth-kind', 'typo'), ('--white-is', 'typo'),
                      ('--depth-kind', 'disparity', '--white-is', 'far'),
                      ('--normal-convention', 'typo')]:
            with self.subTest(extra=extra):
                self.assertFalse(self.import_map(extra=extra)['ok'])
                self.assertFalse((self.project / 'out.png').exists())

    def test_output_cannot_overwrite_source_or_picture(self):
        for name in ['horizontal.png', 'picture.png']:
            before = (self.project / name).read_bytes()
            result = self.import_map(out=name)
            self.assertFalse(result['ok'])
            self.assertEqual((self.project / name).read_bytes(), before)

    def test_output_directory_and_non_png_extension_fail(self):
        (self.project / 'out.png').mkdir()
        self.assertFalse(self.import_map()['ok'])
        self.assertFalse(self.import_map(out='map.webp')['ok'])

    def test_output_parent_that_is_a_file_fails_cleanly(self):
        before = (self.project / 'flat.png').read_bytes()
        result = self.import_map(out='flat.png/output.png')
        self.assertFalse(result['ok'])
        self.assertIn(result['error'], ['could not create output directory', 'could not save output PNG'])
        self.assertNotEqual(result['code'], 0)
        self.assertEqual((self.project / 'flat.png').read_bytes(), before)

    def estimator(self, code):
        stub = self.project / 'model stub.py'
        stub.write_text(code, encoding='utf-8')
        config = {'depth': f'"{sys.executable}" "{stub}" {{in}} {{out}}'}
        (self.project / 'estimators.json').write_text(json.dumps(config), encoding='utf-8')
        return self.command('estimate', '--kind', 'depth', '--in', 'res://picture.png',
                            '--out', 'res://estimated.png', '--white-is', 'near', '--no-align')

    def test_estimator_cannot_reuse_a_previous_raw_image(self):
        Image.new('L', (32, 32), 99).save(self.project / 'estimated_raw.png')
        Image.new('L', (32, 32), 77).save(self.project / 'estimated.png')
        original = (self.project / 'estimated.png').read_bytes()
        result = self.estimator('pass\n')
        self.assertFalse(result['ok'])
        self.assertEqual(result['exit_code'], 0)
        self.assertIn('no output', result['error'])
        self.assertEqual((self.project / 'estimated.png').read_bytes(), original)
        self.assertEqual(self.pixels('estimated_raw.png').getpixel((0, 0)), 99)

    def test_estimator_creates_output_and_forwards_import_options(self):
        result = self.estimator("import sys\nfrom PIL import Image\nImage.new('L',(16,16),64).save(sys.argv[2])\n")
        self.assertTrue(result['ok'], result)
        self.assertEqual(result['found']['direction_source'], 'explicit')
        self.assertFalse(result['found']['alignment_enabled'])
        self.assertEqual(result['found']['applied_offset_px'], [0, 0])
        self.assertTrue(Path(result['raw']).is_file())
        self.assertNotEqual(Path(result['raw']).name, 'estimated_raw.png')
        self.assertEqual(self.pixels('estimated.png').size, (32, 32))
        self.assertAlmostEqual(self.pixels('estimated.png').getpixel((0, 0)), 64, delta=1)

    def test_estimator_failure_preserves_previous_final_image(self):
        Image.new('L', (32, 32), 77).save(self.project / 'estimated.png')
        before = (self.project / 'estimated.png').read_bytes()
        result = self.estimator('import sys\nsys.exit(7)\n')
        self.assertFalse(result['ok'])
        self.assertEqual((self.project / 'estimated.png').read_bytes(), before)

    def test_non_string_estimator_config_returns_json_error(self):
        (self.project / 'estimators.json').write_text('{"depth":123}', encoding='utf-8')
        result = self.command('estimate', '--kind', 'depth', '--in', 'res://picture.png', '--out', 'res://estimated.png')
        self.assertFalse(result['ok'])


if __name__ == '__main__':
    unittest.main()
