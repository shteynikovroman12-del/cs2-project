import hashlib
from pathlib import Path
import sys
import tempfile
import unittest

from PIL import Image

sys.path.insert(0, str(Path(__file__).resolve().parents[1] / 'tools'))
from prepare_lite_previews import resize_preview


class LitePreviewTests(unittest.TestCase):
    def test_alpha_aspect_and_original_preserved(self):
        with tempfile.TemporaryDirectory() as directory:
            root = Path(directory)
            source, output = root / 'source', root / 'output'
            source.mkdir(); output.mkdir()
            original = Image.new('RGBA', (1024, 512), (0, 0, 0, 0))
            original.paste((205, 30, 60, 255), (128, 64, 896, 448))
            path = source / 'sample.png'
            original.save(path)
            before = hashlib.sha256(path.read_bytes()).hexdigest()
            resize_preview('sample.png', source, output, 384)
            self.assertEqual(hashlib.sha256(path.read_bytes()).hexdigest(), before)
            with Image.open(output / 'sample.png') as smaller:
                self.assertEqual(smaller.size, (384, 192))
                self.assertEqual(smaller.getpixel((0, 0))[3], 0)
                self.assertEqual(smaller.getpixel((192, 96)), (205, 30, 60, 255))

    def test_small_images_are_not_upscaled(self):
        with tempfile.TemporaryDirectory() as directory:
            root = Path(directory)
            output = root / 'output'; output.mkdir()
            original = Image.new('RGBA', (64, 32), (40, 70, 140, 127))
            original.save(root / 'small.png')
            resize_preview('small.png', root, output, 384)
            with Image.open(output / 'small.png') as smaller:
                self.assertEqual(smaller.size, original.size)
                self.assertEqual(smaller.tobytes(), original.tobytes())

    def test_bad_input_cannot_replace_existing_output(self):
        with tempfile.TemporaryDirectory() as directory:
            root = Path(directory)
            output = root / 'output'; output.mkdir()
            (root / 'broken.png').write_bytes(b'not a PNG')
            (output / 'broken.png').write_bytes(b'existing output')
            with self.assertRaises(ValueError):
                resize_preview('broken.png', root, output, 384)
            self.assertEqual((output / 'broken.png').read_bytes(), b'existing output')
            with self.assertRaises(ValueError):
                resize_preview('../broken.png', root, output, 384)
            with self.assertRaises(ValueError):
                resize_preview('broken.png', root, root, 384)


if __name__ == '__main__':
    unittest.main()
