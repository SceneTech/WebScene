#!/usr/bin/env python3
"""Validate the existing pinned Linux V8 SDK before linking a Runtime consumer."""
import argparse
import hashlib
import json
from pathlib import Path
import re


def sha256(path):
    digest = hashlib.sha256()
    with path.open('rb') as stream:
        for chunk in iter(lambda: stream.read(1024 * 1024), b''):
            digest.update(chunk)
    return digest.hexdigest()


def validate(root, output):
    version_header = root / 'include/v8-version.h'
    fields = dict(re.findall(r'^#define\s+(V8_\w+)\s+(\d+)\s*$', version_header.read_text(), re.M))
    version = '.'.join(fields[key] for key in ('V8_MAJOR_VERSION', 'V8_MINOR_VERSION', 'V8_BUILD_NUMBER'))
    if version != '15.3.10':
        raise ValueError('Linux Runtime requires pinned V8 15.3.10, got ' + version)
    args_path = output / 'args.gn'
    settings = dict(re.findall(r'^\s*(\w+)\s*=\s*([^\n#]+)', args_path.read_text(), re.M))
    settings = {key: value.strip() for key, value in settings.items()}
    required = {
        'target_cpu': '"x64"', 'use_custom_libcxx': 'false',
        'v8_monolithic': 'true', 'v8_monolithic_for_shared_library': 'true',
        'v8_enable_pointer_compression': 'true',
        'v8_enable_pointer_compression_shared_cage': 'true',
        'v8_enable_partition_alloc': 'true', 'v8_enable_sandbox': 'false',
        'v8_enable_static_roots': 'false', 'use_thin_lto': 'false',
        'use_allocator_shim': 'false', 'use_partition_alloc_as_malloc': 'false',
    }
    for key, value in required.items():
        if settings.get(key) != value:
            raise ValueError(f'Incompatible V8 ABI: {key} must be {value}, got {settings.get(key)!r}')
    for key in ('v8_enable_direct_handle', 'v8_enable_direct_local'):
        if settings.get(key, 'false') != 'false':
            raise ValueError('Direct handles are not part of this pinned V8 ABI')
    sources = {
        'versionHeader': version_header,
        'publicHeader': root / 'include/v8.h',
        'inspectorHeader': root / 'include/v8-inspector.h',
        'args': args_path,
        'monolith': output / 'obj/libv8_monolith.a',
        'icu': output / 'icudtl.dat',
        'partitionAllocFlags': output / 'gen/third_party/partition_alloc/src/partition_alloc/buildflags.h',
        'v8License': root / 'LICENSE',
        'icuLicense': root / 'third_party/icu/LICENSE',
    }
    if not (root / 'third_party/partition_alloc/src/partition_alloc').is_dir():
        raise ValueError('Pinned PartitionAlloc source headers are missing')
    if 'consoleAPICalled' not in sources['inspectorHeader'].read_text():
        raise ValueError('Pinned WebScene inspector header extension is missing')
    for name, path in sources.items():
        if not path.is_file() or not path.stat().st_size:
            raise ValueError(f'Missing/empty {name}: {path}')
    return {'schemaVersion': 1, 'v8Version': version, 'cxxAbi': 'libstdc++',
            'target': 'linux-x64', 'settings': settings,
            'inputs': {name: {'sha256': sha256(path), 'bytes': path.stat().st_size}
                       for name, path in sources.items()}}


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--root', required=True, type=Path)
    parser.add_argument('--output', required=True, type=Path)
    parser.add_argument('--manifest', required=True, type=Path)
    args = parser.parse_args()
    report = validate(args.root.resolve(strict=True), args.output.resolve(strict=True))
    args.manifest.parent.mkdir(parents=True, exist_ok=True)
    args.manifest.write_text(json.dumps(report, indent=2) + '\n')
    print('Verified Linux V8', report['v8Version'], report['inputs']['monolith']['sha256'])


if __name__ == '__main__':
    main()
