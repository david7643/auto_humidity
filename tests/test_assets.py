from pathlib import Path
import gzip
import re
import unittest

ROOT = Path(__file__).resolve().parent.parent

class Assets(unittest.TestCase):
    def test_embedded_page_matches_source(self):
        header = (ROOT / 'WebPage.h').read_text()
        packed = bytes(int(n,16) for n in re.findall(r'0x([0-9a-f]{2})', header))
        self.assertEqual(gzip.decompress(packed), (ROOT / 'web/index.html').read_bytes())

if __name__ == '__main__':
    unittest.main()
