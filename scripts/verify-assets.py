#!/usr/bin/env python3
"""Verify all external freeware assets against the original release manifests."""
import hashlib, json
from pathlib import Path
root=Path(__file__).resolve().parent.parent
for lang in ['en','jp']:
 manifest=json.loads((root/'docs'/f'{lang}-data-manifest.json').read_text())
 for name,digest in manifest.items():
  path=root/'assets'/f'data_{lang}'/name
  assert path.is_file(), f'Missing {lang}/{name}'
  assert hashlib.sha256(path.read_bytes()).hexdigest()==digest, f'Modified {lang}/{name}'
 print(f'{lang}: all {len(manifest)} original files match SHA-256')
