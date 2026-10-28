"""Read PE metadata and selected static CS2 schema records; never load or execute DLLs.

Static schema layout research used a2x/cs2-analyzer (MIT), commit
4367fa2b0d2ee4a62e504fbab518a428151b9f9f. This client's size/count/fields
locations differ from that revision. See THIRD_PARTY_NOTICES.md.
"""
import argparse
import hashlib
import json
from pathlib import Path
import re
import struct


DEFAULT_CLASSES = (
    'C_EconEntity', 'C_EconItemView', 'C_AttributeContainer',
    'CAttributeList', 'CEconItemAttribute', 'CPlayer_WeaponServices',
    'CCSPlayerController_InventoryServices',
)


class PE64:
    def __init__(self, data):
        self.data = data
        if data[:2] != b'MZ':
            raise ValueError('Not a DOS/PE image')
        pe = self.unpack('<I', 0x3c)[0]
        if self.read(pe, 4) != b'PE\0\0':
            raise ValueError('Invalid PE signature')
        machine, count = self.unpack('<HH', pe + 4)
        optional_size = self.unpack('<H', pe + 20)[0]
        optional = pe + 24
        if machine != 0x8664 or self.unpack('<H', optional)[0] != 0x20b:
            raise ValueError('Expected an x64 PE32+ image')
        if not 1 <= count <= 96 or optional_size < 112:
            raise ValueError('Invalid PE header size or section count')
        self.read(optional, optional_size)
        self.base = self.unpack('<Q', optional + 24)[0]
        self.image_size = self.unpack('<I', optional + 56)[0]
        self.sections = []
        for i in range(count):
            at = optional + optional_size + i * 40
            self.read(at, 40)
            name = self.read(at, 8).rstrip(b'\0').decode('ascii', errors='replace')
            virtual_size, rva, raw_size, raw = self.unpack('<IIII', at + 8)
            if rva + max(virtual_size, raw_size) > self.image_size:
                raise ValueError('Section extends beyond image')
            self.read(raw, raw_size)
            section = dict(name=name, rva=rva, virtual_size=virtual_size,
                           raw_size=raw_size, raw_offset=raw,
                           characteristics=self.unpack('<I', at + 36)[0])
            for previous in self.sections:
                if raw_size and previous['raw_size'] and max(raw, previous['raw_offset']) < min(raw + raw_size, previous['raw_offset'] + previous['raw_size']):
                    raise ValueError('Overlapping raw sections')
                if max(rva, previous['rva']) < min(rva + max(virtual_size, raw_size), previous['rva'] + max(previous['virtual_size'], previous['raw_size'])):
                    raise ValueError('Overlapping virtual sections')
            self.sections.append(section)

    def read(self, offset, size):
        if offset < 0 or size < 0 or offset + size > len(self.data):
            raise ValueError('Read outside file bounds')
        return self.data[offset:offset + size]

    def unpack(self, fmt, offset):
        return struct.unpack(fmt, self.read(offset, struct.calcsize(fmt)))

    def offset_to_va(self, offset):
        for section in self.sections:
            if section['raw_offset'] <= offset < section['raw_offset'] + section['raw_size']:
                return self.base + section['rva'] + offset - section['raw_offset']
        raise ValueError('File position is outside mapped sections')

    def va_to_offset(self, va, size=1):
        if size < 1:
            raise ValueError('Invalid mapped read size')
        for section in self.sections:
            delta = va - self.base - section['rva']
            if 0 <= delta and delta + size <= section['raw_size']:
                return section['raw_offset'] + delta
        raise ValueError('Address is not backed by file bytes')

    def string(self, va):
        offset = self.va_to_offset(va)
        end = self.data.find(b'\0', offset, offset + 256)
        if end < 0:
            raise ValueError('Unterminated schema name')
        self.va_to_offset(va, end - offset + 1)
        try:
            return self.data[offset:end].decode('ascii')
        except UnicodeError as error:
            raise ValueError('Invalid schema name encoding') from error

    def schema(self, class_name):
        """Require one complete, internally consistent static record for this name.

        Current layout: name +0x08, size +0x20, count +0x24, fields +0x30.
        Field records are 0x20 bytes. Types, inheritance and runtime addresses
        are intentionally not inferred from these static records.
        """
        if not re.fullmatch(r'[A-Za-z_][A-Za-z_0-9]{0,127}', class_name):
            raise ValueError('Invalid class name')
        candidates = {}
        needle = re.escape(class_name.encode('ascii') + b'\0')
        for match in re.finditer(needle, self.data):
            try:
                name_va = self.offset_to_va(match.start())
                self.va_to_offset(name_va, len(class_name) + 1)
            except ValueError:
                continue
            for ref in re.finditer(re.escape(struct.pack('<Q', name_va)), self.data):
                start = ref.start() - 8
                if start < 0 or start % 8:
                    continue
                try:
                    record_va = self.offset_to_va(start)
                    self.va_to_offset(record_va, 0x38)
                    size, count = self.unpack('<IH', start + 0x20)
                    if not 0 < size <= 1024 * 1024 or not 0 < count <= 2048:
                        continue
                    fields_va = self.unpack('<Q', start + 0x30)[0]
                    fields_at = self.va_to_offset(fields_va, count * 32)
                    fields = {}
                    for i in range(count):
                        at = fields_at + i * 32
                        name = self.string(self.unpack('<Q', at)[0])
                        offset = self.unpack('<i', at + 16)[0]
                        if not re.fullmatch(r'[A-Za-z_][A-Za-z_0-9]*', name) or name in fields:
                            raise ValueError('Invalid or duplicate field name')
                        if not 0 <= offset < size:
                            raise ValueError('Field offset outside class')
                        fields[name] = offset
                    candidates[start] = dict(record_rva=record_va - self.base,
                                             size=size, fields=fields)
                except ValueError:
                    continue
        if len(candidates) != 1:
            raise ValueError(f'{class_name}: expected one validated record, found {len(candidates)}')
        return next(iter(candidates.values()))


def analyze(path, classes=()):
    if path.stat().st_size > 256 * 1024 * 1024:
        raise ValueError('Image exceeds analysis size limit')
    raw = path.read_bytes()
    pe = PE64(raw)
    return dict(file=path.name, bytes=len(raw), sha256=hashlib.sha256(raw).hexdigest(),
                machine='AMD64', preferred_image_base=pe.base, image_size=pe.image_size,
                sections=pe.sections, classes={name: pe.schema(name) for name in classes})


def verify_report(modules, report):
    if not isinstance(report, dict) or report.get('report_format') != 1 or report.get('steam_app_id') != 730:
        raise ValueError('Unsupported client report')
    expected = {}
    entries = report.get('files')
    if not isinstance(entries, list):
        raise ValueError('Client report has no file list')
    for entry in entries:
        if not isinstance(entry, dict) or not isinstance(entry.get('file'), str):
            raise ValueError('Invalid client report entry')
        name = entry['file'].replace('\\', '/').split('/')[-1]
        if name in expected:
            raise ValueError('Duplicate module in client report')
        expected[name] = entry
    for module in modules:
        entry = expected.get(module['file'], {})
        if not entry.get('present') or module['sha256'] != entry.get('sha256') or module['bytes'] != entry.get('bytes'):
            raise ValueError('Client report mismatch: ' + module['file'])
    return report['steam_build_id']


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--client', type=Path, required=True)
    parser.add_argument('--engine', type=Path)
    parser.add_argument('--schema-system', type=Path)
    parser.add_argument('--client-report', type=Path)
    parser.add_argument('--class-name', action='append', dest='classes')
    parser.add_argument('--output', type=Path, required=True)
    args = parser.parse_args()
    inputs = [path for path in (args.client, args.engine, args.schema_system, args.client_report) if path]
    try:
        if any(path.resolve() == args.output.resolve() or (args.output.exists() and path.samefile(args.output)) for path in inputs):
            raise ValueError('Output must not overwrite an input file')
        modules = [analyze(args.client, args.classes or DEFAULT_CLASSES)]
        modules.extend(analyze(path) for path in (args.engine, args.schema_system) if path)
        build = None
        if args.client_report:
            build = verify_report(modules, json.loads(args.client_report.read_text(encoding='utf-8-sig')))
        result = dict(format=1, analysis='static_only', steam_build_id_from_report=build,
                      runtime_verified=False, modules=modules,
                      limitations=['Offsets describe fields inside classes, not process addresses.',
                                   'Field types and inheritance are not inferred.',
                                   'No code was executed; material refresh and matchmaking compatibility are unverified.'])
        args.output.write_text(json.dumps(result, indent=2) + '\n', encoding='utf-8')
        print(f'Analyzed {len(modules)} modules; wrote {args.output}')
    except (OSError, ValueError, KeyError) as error:
        parser.exit(1, f'Analysis failed: {error}\n')


if __name__ == '__main__':
    main()
