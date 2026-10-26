"""Fetch all catalog images for an offline package. Requires Python 3.9+."""
import argparse
from concurrent.futures import ThreadPoolExecutor, as_completed
import json
from pathlib import Path
import struct
import time
import urllib.request
import zlib

ROOT = Path(__file__).resolve().parents[1]
MAX_BYTES = 8 * 1024 * 1024


def validate_png(data):
    if len(data) > MAX_BYTES or data[:8] != b'\x89PNG\r\n\x1a\n':
        raise ValueError('Expected a PNG image smaller than 8 MiB')
    offset, dimensions, pixels, finished = 8, None, False, False
    while offset + 12 <= len(data):
        length = struct.unpack('>I', data[offset:offset + 4])[0]
        tag = data[offset + 4:offset + 8]
        end = offset + 8 + length
        if end + 4 > len(data):
            raise ValueError('Truncated PNG')
        if zlib.crc32(data[offset + 4:end]) & 0xffffffff != struct.unpack('>I', data[end:end + 4])[0]:
            raise ValueError('PNG checksum mismatch')
        if offset == 8:
            if tag != b'IHDR' or length != 13:
                raise ValueError('Missing PNG header')
            dimensions = struct.unpack('>II', data[offset + 8:offset + 16])
            if not all(0 < value <= 2048 for value in dimensions):
                raise ValueError('Image dimensions outside limits')
        pixels |= tag == b'IDAT'
        if tag == b'IEND':
            finished = True
            break
        offset = end + 4
    if not (dimensions and pixels and finished):
        raise ValueError('Incomplete PNG')


def fetch(entry, output):
    filename, url = entry['file'], entry['url']
    if Path(filename).name != filename or not filename.endswith('.png'):
        raise ValueError('Unsafe image filename')
    if not url.startswith(('https://community.akamai.steamstatic.com/economy/image/',
                           'https://cdn.steamstatic.com/apps/730/icons/')):
        raise ValueError('Unexpected image destination')
    path = output / filename
    if path.exists():
        try:
            validate_png(path.read_bytes())
            return
        except ValueError:
            pass
    for attempt in range(3):
        try:
            request = urllib.request.Request(url, headers={'User-Agent': 'CS2Profiles-Catalog/0.2'})
            with urllib.request.urlopen(request, timeout=35) as response:
                data = response.read(MAX_BYTES + 1)
            validate_png(data)
            temporary = path.with_suffix('.tmp')
            temporary.write_bytes(data)
            temporary.replace(path)
            return
        except Exception:
            if attempt == 2:
                raise
            time.sleep(1 + attempt * 2)


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--output', type=Path, default=ROOT / 'assets/previews')
    parser.add_argument('--workers', type=int, default=8)
    args = parser.parse_args()
    manifest = json.loads((ROOT / 'assets/preview-manifest.json').read_text(encoding='utf-8'))
    args.output.mkdir(parents=True, exist_ok=True)
    failures = []
    entries = manifest['images']
    with ThreadPoolExecutor(max_workers=max(1, min(args.workers, 12))) as pool:
        futures = {pool.submit(fetch, entry, args.output): entry['file'] for entry in entries}
        for count, future in enumerate(as_completed(futures), 1):
            try:
                future.result()
            except Exception as error:
                failures.append(f'{futures[future]}: {error}')
            if count % 100 == 0 or count == len(entries):
                print(f'Checked {count}/{len(entries)} images; failures: {len(failures)}', flush=True)
    if failures:
        for failure in failures[:20]:
            safe = failure.replace('%', '%25').replace('\r', '%0D').replace('\n', '%0A')
            print(f'::error title=Preview download::{safe}')
        raise SystemExit(f'{len(failures)} images missing; refusing to produce an incomplete package')
    print('All catalog images are ready for offline use.')


if __name__ == '__main__':
    main()
