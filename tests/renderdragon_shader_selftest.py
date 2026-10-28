"""Compile every generated RenderDragon program for the requested bgfx profiles."""
import argparse
import importlib.util
import json
import subprocess
import struct
import sys
import tempfile
from concurrent.futures import ThreadPoolExecutor
from pathlib import Path

parser = argparse.ArgumentParser(description=__doc__)
parser.add_argument('--source', required=True, type=Path)
parser.add_argument('--shaderc', required=True, type=Path)
parser.add_argument('--bgfx-include', required=True, type=Path)
parser.add_argument('--profiles', nargs='+', default=['metal', '430', '300_es', 'spirv'])
parser.add_argument('--optimization', choices=['0', '1', '2', '3'], default='3')
args = parser.parse_args()
sys.dont_write_bytecode = True
repo = Path(__file__).resolve().parents[1]
spec = importlib.util.spec_from_file_location('rd_generator', repo / 'tools/gen_renderdragon_shaders.py')
generator = importlib.util.module_from_spec(spec)
spec.loader.exec_module(generator)

with tempfile.TemporaryDirectory(prefix='perlin-rd-') as temp:
    root = Path(temp)
    generator.generate(args.source.resolve(), root)
    manifest = json.loads((root / 'manifest.json').read_text())
    fragments = {p.name for p in root.glob('fs_*.sc')}
    if set(manifest) != fragments:
        raise AssertionError('Every runtime fragment program must identify its upstream source')
    jobs = []
    for path in sorted(root.glob('*.sc')):
        if path.name.startswith('varying_'):
            continue
        name = path.stem
        domain = 'ui' if name.startswith(('fs_entity', 'fs_particle', 'fs_weather', 'fs_cracks', 'fs_nametag')) else name.split('_', 1)[1].split('_', 1)[0]
        for profile in args.profiles:
            jobs.append((path, domain, profile))
    def compile_shader(job):
        path, domain, profile = job
        command = [str(args.shaderc.resolve()), '-f', str(path), '-o', str(root / (path.stem + '-' + profile + '.bin')),
                   '--type', 'vertex' if path.name.startswith('vs_') else 'fragment',
                   '--platform', 'android' if profile == '300_es' else 'osx' if profile == 'metal' else 'windows' if profile == 's_5_0' else 'linux',
                   '--profile', profile, '--varyingdef', str(root / ('varying_' + domain + '.def.sc')),
                   '-i', str(repo / 'src/shaders'), '-i', str(args.bgfx_include.resolve()),
                   '-O', args.optimization, '--Werror']
        result = subprocess.run(command, text=True, stdout=subprocess.PIPE, stderr=subprocess.STDOUT)
        if result.returncode:
            return path.name + ' [' + profile + ']\n' + result.stdout
        return None
    with ThreadPoolExecutor(max_workers=4) as pool:
        failures = [result for result in pool.map(compile_shader, jobs) if result]
    for path, domain, profile in jobs:
        if not path.name.startswith('fs_'):
            continue
        vertex = root / ('vs_' + domain + '-' + profile + '.bin')
        fragment = root / (path.stem + '-' + profile + '.bin')
        if vertex.exists() and fragment.exists():
            vertex_out = struct.unpack_from('<I', vertex.read_bytes(), 8)[0]
            fragment_in = struct.unpack_from('<I', fragment.read_bytes(), 4)[0]
            if vertex_out != fragment_in:
                failures.append(f'{path.stem} [{profile}] vertex/fragment interface mismatch')
    for failure in failures:
        print(failure)
    print(f'RenderDragon shaders: {len(jobs) - len(failures)}/{len(jobs)} compilations passed')
    raise SystemExit(bool(failures))
