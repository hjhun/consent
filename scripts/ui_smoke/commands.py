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
"""Bounded host commands and independently checked remote exit statuses."""

import base64
import hashlib
import json
import os
from pathlib import Path
import secrets
import re
import selectors
import shlex
import subprocess
import time
import zlib

ROOT = '/opt/var/lib/consent-ui-native-09'
APP = '/opt/usr/globalapps/org.tizen.consentui'
OLD_TPK = '/usr/share/consent/poc/org.tizen.consentui-0.1.0.tpk'
SERIAL = None
OUTPUT = None
LIMIT = 8 * 1024 * 1024


class Unavailable(RuntimeError):
    """A required tool, device or actual UI observation is unavailable."""


def set_output(path):
    global OUTPUT
    OUTPUT = path


def run(arguments, timeout=30, limit=LIMIT, input_data=None):
    process = None
    selector = selectors.DefaultSelector()
    streams = []
    buffers = [bytearray(), bytearray()]
    deadline = time.monotonic() + timeout
    status = None
    error = None
    teardown = []
    category = 'success'
    try:
        process = subprocess.Popen(arguments, stdout=subprocess.PIPE,
            stderr=subprocess.PIPE,
            stdin=subprocess.PIPE if input_data is not None else None,
            start_new_session=True)
        streams = [process.stdout, process.stderr]
        for index, stream in enumerate(streams):
            os.set_blocking(stream.fileno(), False)
            selector.register(stream, selectors.EVENT_READ, index)
        offset = 0
        if process.stdin is not None:
            os.set_blocking(process.stdin.fileno(), False)
            selector.register(process.stdin, selectors.EVENT_WRITE, 'input')
        while selector.get_map():
            remaining = deadline - time.monotonic()
            if remaining <= 0:
                category = 'timeout'
                raise TimeoutError('bounded host command timeout')
            for key, _ in selector.select(min(remaining, .2)):
                if key.data == 'input':
                    if offset < len(input_data):
                        offset += os.write(key.fd, input_data[offset:offset +
                                                            65536])
                    else:
                        selector.unregister(key.fileobj)
                        key.fileobj.close()
                    continue
                chunk = os.read(key.fd, 65536)
                if not chunk:
                    selector.unregister(key.fileobj)
                    continue
                buffers[key.data].extend(chunk)
                if sum(map(len, buffers)) > limit:
                    category = 'output-limit'
                    raise RuntimeError('command output exceeded bound')
        status = process.wait(timeout=max(.01, deadline - time.monotonic()))
        if status:
            category = 'exit'
            raise RuntimeError('host command failed: ' + str(arguments))
    except BaseException as value:
        error = value
        if category == 'success':
            category = 'command-error'
    finally:
        if process is not None:
            if process.poll() is None:
                import signal
                try:
                    os.killpg(process.pid, signal.SIGKILL)
                except ProcessLookupError:
                    pass
                except BaseException as value:
                    teardown.append(repr(value))
            try:
                status = process.wait(timeout=5)
            except BaseException as value:
                teardown.append(repr(value))
            if process.stdin is not None:
                streams.append(process.stdin)
        try:
            selector.close()
        except BaseException as value:
            teardown.append(repr(value))
        for stream in streams:
            try:
                stream.close()
            except BaseException as value:
                teardown.append(repr(value))
        stdout, stderr = map(bytes, buffers)
        if OUTPUT is not None:
            record = {'argv': arguments, 'exit': status, 'category': category,
                'stdout': stdout[:65536].decode(errors='replace'),
                'stderr': stderr[:65536].decode(errors='replace'),
                'stdout_bytes': len(stdout), 'stderr_bytes': len(stderr),
                'error': repr(error) if error else None,
                'teardown_errors': teardown}
            try:
                name = OUTPUT / ('command-' + secrets.token_hex(8) + '.json')
                name.write_text(json.dumps(record, indent=2) + '\n')
            except BaseException as value:
                if error is None:
                    error = value
    if error is not None:
        raise error
    if teardown:
        raise RuntimeError('command teardown failed: ' + '; '.join(teardown))
    return stdout


MAX_SERVICE = 2000
MAX_SOURCE = 32768
MAX_ENCODED = 45000


def remote_service(arguments, token):
    def service(argv):
        command = ' '.join(shlex.quote(str(argument)) for argument in argv)
        wrapper = (command + '\nsmoke_status=$?\nprintf "\\n' + token +
                   ':%s\\n" "$smoke_status"\nexit "$smoke_status"\n')
        return '/bin/sh -c ' + shlex.quote(wrapper)

    original = list(arguments)
    wire = service(original)
    evidence = {'original_argv': original, 'transport': 'literal'}
    if (len(original) == 3 and original[0] in ('python3', '/usr/bin/python3')
            and original[1] == '-c'):
        source = original[2].encode('utf-8')
        if len(source) > MAX_SOURCE:
            raise RuntimeError('remote Python source exceeds finite bound')
        sha = hashlib.sha256(source).hexdigest()
        evidence.update(code_sha256=sha, code_bytes=len(source))
        if len(wire.encode()) > MAX_SERVICE:
            compressed = zlib.compress(source)
            encoded = base64.b64encode(compressed).decode('ascii')
            if (len(encoded) > MAX_ENCODED or
                    zlib.decompress(compressed) != source):
                raise RuntimeError('remote Python transport roundtrip failed')
            decoder = ("def _consent_smoke_decode():\n"
                " import base64,hashlib,zlib\n"
                " d=zlib.decompressobj()\n"
                " p=d.decompress(base64.b64decode(" + repr(encoded) +
                ",validate=True),32769)\n"
                " if len(p)>32768 or not d.eof or d.unconsumed_tail or "
                "d.unused_data or hashlib.sha256(p).hexdigest()!=" + repr(sha) +
                ":raise RuntimeError('remote code integrity/bound')\n"
                " namespace=globals()\n"
                " del namespace['_consent_smoke_decode']\n"
                " exec(compile(p,'<string>','exec'),namespace)\n"
                "_consent_smoke_decode()")
            wire = service([original[0], original[1], decoder])
            evidence.update(transport='zlib-base64', encoded_bytes=len(encoded))
    if len(wire.encode()) > MAX_SERVICE:
        raise RuntimeError('remote service exceeds finite SDB transport bound')
    evidence.update(wire_service=wire, service_bytes=len(wire.encode()))
    return wire, evidence


def remote(arguments, name, timeout=30):
    token = 'CONSENT_UI_SMOKE_' + secrets.token_hex(12)
    wire, evidence = remote_service(arguments, token)
    (OUTPUT / ('remote-wire-' + token + '.json')).write_text(
        json.dumps(evidence, indent=2) + '\n')
    output = run(['sdb', '-s', SERIAL, 'shell',
                  wire], timeout)
    text = output.decode('utf-8', errors='strict').replace('\r\n', '\n')
    (OUTPUT / name).write_bytes(output)
    prefix = token + ':'
    rows = [row[len(prefix):] for row in text.splitlines()
            if row.startswith(prefix)]
    if rows != ['0']:
        raise RuntimeError('remote nonzero/missing status: ' + name)
    return '\n'.join(row for row in text.splitlines()
                     if not row.startswith(prefix)).strip()


def emulator_inventory():
    """Correlate live SDK emulator processes with their own SDB logs."""
    result = {}
    for process in Path('/proc').iterdir():
        if not process.name.isdecimal():
            continue
        try:
            executable = (process / 'exe').resolve(strict=True)
            if executable.name != 'emulator-x86_64':
                continue
            arguments = (process / 'cmdline').read_bytes().split(b'\0')
            index = arguments.index(b'--conf')
            config = Path(os.fsdecode(arguments[index + 1]))
            if not config.is_absolute() or config.name != 'vm_launch.conf':
                continue
            if config.stat().st_size > 65536:
                raise Unavailable('emulator configuration exceeds bound')
            text = config.read_text()
            names = re.findall(r'^vm_name=([^\r\n]+)$', text, re.M)
            if names != [config.parent.name]:
                raise Unavailable('emulator configuration identity mismatch')
            log = config.parent / 'logs' / 'emulator.log'
            with log.open('rb') as stream:
                stream.seek(0, os.SEEK_END)
                stream.seek(max(0, stream.tell() - 8 * 1024 * 1024))
                lines = stream.read().decode('utf-8', errors='replace')
            pattern = (r'\|\s*' + process.name +
                       r'\|.*Added new sdb client\..*serial: '
                       r'(emulator-[0-9]+)(?:\s|$)')
            for serial in set(re.findall(pattern, lines)):
                if serial in result:
                    raise Unavailable('ambiguous emulator process identity')
                result[serial] = {'pid': int(process.name),
                    'executable': str(executable), 'config': str(config),
                    'vm_name': names[0], 'log': str(log),
                    'classification': 'live-sdk-emulator-sdb-log'}
        except (FileNotFoundError, ProcessLookupError):
            continue
        except (PermissionError, ValueError, IndexError):
            continue
    return result


def discover(selected):
    global SERIAL
    rows = run(['sdb', 'devices']).decode().splitlines()
    inventory = emulator_inventory()
    devices = [row.split()[0] for row in rows
               if len(row.split()) >= 2 and row.split()[1] == 'device'
               and row.split()[0] in inventory]
    if selected is None:
        if len(devices) != 1:
            raise Unavailable('select exactly one connected SDK emulator')
        selected = devices[0]
    if selected not in devices:
        raise Unavailable('selected target lacks live SDK emulator evidence')
    SERIAL = selected
    (OUTPUT / 'selected-device.json').write_text(json.dumps(
        {'serial': selected, 'evidence': inventory[selected],
         'sdb_rows': rows}, indent=2) + '\n')
    arch = remote(['uname', '-m'], 'device-architecture.log')
    if arch != 'x86_64':
        raise Unavailable('UI09 fixture requires verified x86_64 emulator')
    return arch
