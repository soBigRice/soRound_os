#!/usr/bin/env python3
"""Validate the actual bundled files, dimensions, baseline JPEG format, and pinned font inputs."""
import hashlib
import json
from pathlib import Path
from PIL import Image
ROOT=Path(__file__).resolve().parents[2]
assets=ROOT/'artwork/watchfaces'
manifest=json.loads((assets/'fonts/manifest.json').read_text())
for name,expected in manifest['files'].items():
    assert hashlib.sha256((assets/'fonts'/name).read_bytes()).hexdigest()==expected,name
for theme in ('type','orbit','shift'):
    with Image.open(assets/f'{theme}.jpg') as image:
        assert image.format=='JPEG' and image.size==(466,466)
        assert not image.info.get('progressive')
        image.load()
assert 'SIL OPEN FONT LICENSE Version 1.1' in (assets/'fonts/OFL.txt').read_text()
print('3 actual 466px baseline JPEG assets and pinned OFL font checksums passed.')
