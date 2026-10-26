"""Prepare a smaller offline preview set without removing catalog entries."""
import argparse
from concurrent.futures import ThreadPoolExecutor, as_completed
import json
import os
from pathlib import Path

from PIL import Image
from download_previews import validate_png

ROOT = Path(__file__).resolve().parents[1]


def resize_preview(filename, source, output, max_edge):
    if Path(filename).name != filename or not filename.endswith('.png'):
        raise ValueError('Unsafe preview filename')
    source_file, output_file = source / filename, output / filename
    if source_file.resolve() == output_file.resolve():
        raise ValueError('Original previews must not be overwritten')
    raw = source_file.read_bytes()
    validate_png(raw)
    with Image.open(source_file) as original:
        image = original.convert('RGBA')
        image.thumbnail((max_edge, max_edge), Image.Resampling.LANCZOS, reducing_gap=3.0)
        temporary = output_file.with_suffix('.tmp')
        try:
            image.save(temporary, format='PNG', optimize=True, compress_level=9)
            validate_png(temporary.read_bytes())
            temporary.replace(output_file)
        finally:
            temporary.unlink(missing_ok=True)
    return len(raw), output_file.stat().st_size


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--source', type=Path, default=ROOT / 'assets/previews')
    parser.add_argument('--output', type=Path, default=ROOT / 'assets/previews-lite')
    parser.add_argument('--max-edge', type=int, default=384)
    parser.add_argument('--workers', type=int, default=4)
    args = parser.parse_args()
    if args.source.resolve() == args.output.resolve():
        parser.error('Source and output must be separate directories')
    if not 128 <= args.max_edge <= 1024:
        parser.error('Maximum edge must be between 128 and 1024 pixels')
    args.output.mkdir(parents=True, exist_ok=True)
    manifest = json.loads((ROOT / 'assets/preview-manifest.json').read_text(encoding='utf-8'))
    entries = manifest['images']
    original_bytes = output_bytes = 0
    failures = []
    with ThreadPoolExecutor(max_workers=max(1, min(args.workers, 4))) as pool:
        futures = {pool.submit(resize_preview, entry['file'], args.source, args.output, args.max_edge): entry['file']
                   for entry in entries}
        for count, future in enumerate(as_completed(futures), 1):
            try:
                original, smaller = future.result()
                original_bytes += original
                output_bytes += smaller
            except Exception as error:
                failures.append(f'{futures[future]}: {error}')
            if count % 500 == 0 or count == len(entries):
                print(f'Prepared {count}/{len(entries)} previews; failures: {len(failures)}', flush=True)
    if failures:
        for failure in failures[:20]:
            safe = failure.replace('%', '%25').replace('\r', '%0D').replace('\n', '%0A')
            print(f'::error title=Lite preview preparation::{safe}')
        raise SystemExit('Incomplete lite preview set; refusing to package it')
    report = (f'CS2 Profiles: lightweight offline image set\n'
              f'Images: {len(entries)}\nMaximum image edge: {args.max_edge} pixels\n'
              f'Original PNG bytes: {original_bytes}\nPackaged PNG bytes: {output_bytes}\n'
              f'All catalog items and wear-condition images are included.\n'
              f'Transparency and aspect ratios are preserved; enlarged previews have less detail.\n')
    (args.output / 'PACKAGE-INFO.txt').write_text(report, encoding='utf-8')
    print(report)
    if os.environ.get('GITHUB_STEP_SUMMARY'):
        with Path(os.environ['GITHUB_STEP_SUMMARY']).open('a', encoding='utf-8') as summary:
            summary.write('## Lightweight preview package\n\n```text\n' + report + '```\n')


if __name__ == '__main__':
    main()
