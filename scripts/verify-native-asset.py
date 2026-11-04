#!/usr/bin/env python3
"""Verify native PE/Mach-O headers and every member of their embedded runtime."""
import argparse
import hashlib
import io
import json
import re
import struct
import tarfile
from pathlib import Path, PurePosixPath

MAGIC = b'LMVRT-PAYLOAD1\0\0'
FOOTER = struct.Struct('<16sQQ64s')


def sha(data):
    return hashlib.sha256(data).hexdigest()


def binary_arch(data, platform_name):
    if platform_name == 'windows':
        if len(data) < 64 or data[:2] != b'MZ':
            raise ValueError('missing PE DOS header')
        offset = struct.unpack_from('<I', data, 60)[0]
        if offset + 24 > len(data) or data[offset:offset + 4] != b'PE\0\0':
            raise ValueError('invalid PE header')
        machine = struct.unpack_from('<H', data, offset + 4)[0]
        known = {0x8664: 'x64', 0xAA64: 'arm64'}
    else:
        if len(data) < 32 or data[:4] != b'\xcf\xfa\xed\xfe':
            raise ValueError('expected a thin little-endian 64-bit Mach-O')
        machine = struct.unpack_from('<I', data, 4)[0]
        known = {0x01000007: 'x64', 0x0100000C: 'arm64'}
    if machine not in known:
        raise ValueError('unsupported native machine architecture')
    return known[machine]


def macho_payload_section(data):
    count, commands_size = struct.unpack_from('<II', data, 16)
    end = 32 + commands_size
    if end > len(data) or count > commands_size // 8:
        raise ValueError('invalid Mach-O load commands')
    cursor = 32
    matches = []
    for _ in range(count):
        if cursor + 8 > end:
            raise ValueError('truncated Mach-O load command')
        command, size = struct.unpack_from('<II', data, cursor)
        if size < 8 or cursor + size > end:
            raise ValueError('invalid Mach-O load command size')
        if command == 0x19:  # LC_SEGMENT_64
            if size < 72:
                raise ValueError('truncated Mach-O segment')
            sections = struct.unpack_from('<I', data, cursor + 64)[0]
            if 72 + sections * 80 > size:
                raise ValueError('truncated Mach-O sections')
            for index in range(sections):
                section = cursor + 72 + index * 80
                name = data[section:section + 16].rstrip(b'\0')
                segment = data[section + 16:section + 32].rstrip(b'\0')
                if (segment, name) == (b'__LMVIDEO', b'__payload'):
                    length, offset = struct.unpack_from('<QI', data, section + 40)
                    if offset + length > len(data) or offset < end:
                        raise ValueError('invalid Mach-O payload section bounds')
                    matches.append(data[offset:offset + length])
        cursor += size
    if cursor != end or len(matches) != 1:
        raise ValueError('expected one embedded Mach-O runtime section')
    return matches[0]


def embedded_members(data, platform_name):
    region = macho_payload_section(data) if platform_name == 'macos' else data
    if len(region) < FOOTER.size:
        raise ValueError('truncated runtime footer')
    magic, offset, size, digest = FOOTER.unpack(region[-FOOTER.size:])
    if magic != MAGIC or size == 0 or offset + size + FOOTER.size != len(region):
        raise ValueError('invalid runtime footer bounds')
    if platform_name == 'macos' and offset != 0:
        raise ValueError('Mach-O section payload offset must be zero')
    payload = region[offset:offset + size]
    if not re.fullmatch(rb'[a-f0-9]{64}', digest) or sha(payload).encode() != digest:
        raise ValueError('runtime payload hash mismatch')
    files = {}
    with tarfile.open(fileobj=io.BytesIO(payload), mode='r:gz') as archive:
        for member in archive:
            path = PurePosixPath(member.name)
            if path.is_absolute() or '..' in path.parts or '\\' in member.name or ':' in member.name:
                raise ValueError('unsafe embedded path: ' + member.name)
            if member.isdir():
                continue
            if not member.isfile() or member.name in files or path.as_posix() != member.name:
                raise ValueError('unsafe or duplicate embedded member: ' + member.name)
            files[member.name] = archive.extractfile(member).read()
    return files, digest.decode()


def verify(asset, platform_name, version, arch=None):
    data = asset.read_bytes()
    actual_arch = binary_arch(data, platform_name)
    arch = arch or ('arm64' if '-arm64' in asset.name else 'x64')
    if actual_arch != arch:
        raise ValueError('launcher architecture mismatch')
    files, payload_sha = embedded_members(data, platform_name)
    manifest = json.loads(files['MANIFEST.json'])
    if (manifest.get('version'), manifest.get('platform'), manifest.get('arch')) != (version, platform_name, arch):
        raise ValueError('runtime manifest version/platform/architecture mismatch')
    locked = {}
    for line in files['SHA256SUMS'].decode().splitlines():
        match = re.fullmatch(r'([a-f0-9]{64})  (.+)', line)
        if not match or match[2] in locked or match[2] not in files or sha(files[match[2]]) != match[1]:
            raise ValueError('runtime member checksum mismatch: ' + line)
        locked[match[2]] = match[1]
    if set(locked) != set(files) - {'SHA256SUMS'}:
        raise ValueError('runtime contains unlocked files')
    actual_files = {name: sha(body) for name, body in files.items() if name not in ('MANIFEST.json', 'SHA256SUMS')}
    if manifest.get('files') != actual_files:
        raise ValueError('runtime manifest file set/hash mismatch')
    host = 'bin/lmvideo.exe' if platform_name == 'windows' else 'bin/lmvideo'
    module = 'lib/lmvideo_cycles_backend.dll' if platform_name == 'windows' else 'lib/liblmvideo_cycles_backend.dylib'
    for name in (host, module):
        if name not in files or binary_arch(files[name], platform_name) != arch:
            raise ValueError('missing/wrong native host or backend: ' + name)
    for name in ('LICENSE', 'THIRD_PARTY_NOTICES.md'):
        if not files.get(name):
            raise ValueError('missing bundled legal text: ' + name)
    if not any(name.endswith('.tza') for name in files):
        raise ValueError('missing OIDN tensor weights')
    if not any('OpenImageDenoise_device_cpu' in name for name in files):
        raise ValueError('missing dynamic OIDN CPU device')
    if not any('.imageio.' in name for name in files):
        raise ValueError('missing dynamic OIIO image plugins')
    native_count = 0
    for name, body in files.items():
        if body[:2] == b'MZ' or body[:4] == b'\xcf\xfa\xed\xfe':
            if binary_arch(body, platform_name) != arch:
                raise ValueError('runtime library architecture mismatch: ' + name)
            native_count += 1
    return {'status': 'PASS', 'platform': platform_name, 'version': version, 'arch': arch,
            'asset_sha256': sha(data), 'payload_sha256': payload_sha, 'files': len(files), 'native_binaries': native_count}


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--asset', type=Path, required=True)
    parser.add_argument('--platform', choices=('macos', 'windows'), required=True)
    parser.add_argument('--arch', choices=('x64', 'arm64'))
    parser.add_argument('--version', required=True)
    parser.add_argument('--report', type=Path, required=True)
    args = parser.parse_args()
    result = verify(args.asset.resolve(strict=True), args.platform, args.version, args.arch)
    args.report.parent.mkdir(parents=True, exist_ok=True)
    args.report.write_text(json.dumps(result, indent=2) + '\n')
    print('PASS verified native', args.platform, 'runtime')


if __name__ == '__main__':
    main()
