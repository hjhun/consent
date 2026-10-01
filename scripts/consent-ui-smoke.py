#!/usr/bin/env python3
# Copyright (c) 2026 Samsung Electronics Co., Ltd. All Rights Reserved
#
# Licensed under the Apache License, Version 2.0 (the "License");
# you may not use this file except in compliance with the License.
# You may obtain a copy of the License at
#
#     http://www.apache.org/licenses/LICENSE-2.0
#
# Unless required by applicable law or agreed to in writing, software
# distributed under the License is distributed on an "AS IS" BASIS,
# WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
# See the License for the specific language governing permissions and
# limitations under the License.
"""One-command actual native UI smoke with owned restoration."""

import argparse
import hashlib
import json
from pathlib import Path
import random
import os
import shutil
import socket
import sys

from ui_smoke import artifacts, commands, transaction, ui


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--build-dir', type=Path, required=True)
    parser.add_argument('--output', type=Path, required=True)
    parser.add_argument('--seed', type=int, required=True)
    parser.add_argument('--serial')
    parser.add_argument('--aurum-cli', default='aurum-ui')
    parser.add_argument('--aurum-cache', type=Path, default=Path(
        os.environ.get('TIZEN_AURUM_CACHE',
                       str(Path.home() / '.cache/tizen-aurum-ui-automation'))))
    parser.add_argument('--port', type=int)
    parser.add_argument('--scenario', choices=(
        'all', 'functional', 'restart', 'delete', 'generation'), default='all')
    args = parser.parse_args()
    if not __debug__:
        parser.error('optimized Python is not supported')
    output = args.output.absolute()
    if output.exists() or output.is_symlink():
        parser.error('output must be a new non-overwriting directory')
    output.mkdir(mode=0o700, parents=True)
    commands.set_output(output)
    result = {'seed': args.seed, 'scenario': args.scenario, 'subruns': []}
    status = 1
    try:
        for tool in ('sdb', 'rpm', 'rpm2cpio', 'cpio', 'readelf'):
            if not shutil.which(tool):
                raise commands.Unavailable('missing host tool: ' + tool)
        cli = shutil.which(args.aurum_cli)
        if not cli:
            raise commands.Unavailable('missing external Aurum CLI')
        cache = args.aurum_cache.resolve(strict=True)
        cache_files = [cache / 'venv/bin/python',
                       cache / 'generated/aurum_pb2.py',
                       cache / 'generated/aurum_pb2_grpc.py']
        if not all(p.is_file() for p in cache_files):
            raise commands.Unavailable('existing Aurum client cache missing')
        result['aurum_cache'] = {str(p):
            hashlib.sha256(p.read_bytes()).hexdigest() for p in cache_files}
        try:
            import PIL.Image
        except ImportError as error:
            raise commands.Unavailable('host Pillow is required') from error
        architecture = commands.discover(args.serial)
        result.update(serial=commands.SERIAL, architecture=architecture,
                      aurum_cli=str(Path(cli).resolve()))
        inputs = output / 'inputs'
        inputs.mkdir(mode=0o700)
        build = artifacts.Build(args.build_dir.resolve(strict=True), inputs,
                                architecture)
        build.backup()
        if args.port is None:
            with socket.socket() as probe:
                probe.bind(('127.0.0.1', 0))
                port = probe.getsockname()[1]
        else:
            if not 1024 <= args.port <= 65535:
                raise ValueError('bounded unprivileged host port required')
            port = args.port
        result['port'] = port
        modes = ['functional', 'restart', 'delete', 'generation']
        if args.scenario != 'all':
            modes = [args.scenario]
        # Seeded ordering applies only to independent fresh lifecycle subruns.
        if args.scenario == 'all':
            lifecycle = modes[1:]
            random.Random(args.seed).shuffle(lifecycle)
            modes = [modes[0], *lifecycle]
        result['order'] = modes
        scripts = Path(__file__).resolve().parent
        result['runner_sources'] = {str(p.relative_to(scripts)):
            hashlib.sha256(p.read_bytes()).hexdigest()
            for p in [scripts / 'consent-ui-smoke.py',
                      *sorted((scripts / 'ui_smoke').glob('*.py'))]}
        for index, mode in enumerate(modes, 1):
            subrun = output / f'{index:02}-{mode}'
            subrun.mkdir(mode=0o700)
            transaction.configure(build, subrun)
            transaction.execute(lambda owner: ui.scenario(owner, cli, port,
                                                         mode, cache))
            result['subruns'].append({'scenario': mode, 'exit': 0})
        status = 0
    except commands.Unavailable as error:
        result['error'] = str(error)
        status = 3
    except BaseException as error:
        result['error'] = repr(error)
        status = 1
    finally:
        result['exit'] = status
        (output / 'result.json').write_text(json.dumps(result, indent=2) + '\n')
    print('CONSENT_UI_SMOKE_EXIT' + str(status), flush=True)
    return status


if __name__ == '__main__':
    raise SystemExit(main())
