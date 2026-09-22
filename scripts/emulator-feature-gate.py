#!/usr/bin/python3
#
# Copyright (c) 2026 Samsung Electronics Co., Ltd. All rights reserved.
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
#
# Development emulator only: prepare -> wait -> real UI clears/saves selection
# -> release -> audit -> cleanup. Always run cleanup after a staged experiment.
# The script never impersonates the approval UI or reads the consent database.
# Use prepare --kind acquisition or prepare --kind reuse. Other phases inherit
# the saved kind. Archive the cleaned evidence directory before another run.

import argparse
import configparser
import fcntl
import hashlib
import io
import json
import os
from pathlib import Path
import re
import signal
import stat
import subprocess
import sys
import time


DIRECTORY = Path('/opt/var/lib/consent-feature-gate')
ROLES = Path('/etc/consent-poc/roles.conf')
UNIT = 'consent-feature-poc.service'
SOCKET = 'consent-feature-poc.socket'
DAEMON = 'consentd-poc.service'
DAEMON_SOCKET = 'consentd-poc.socket'
UNITS = (SOCKET, UNIT, DAEMON_SOCKET, DAEMON)
DROP_DIRECTORY = Path('/etc/systemd/system/consent-feature-poc.service.d')
DROP_FILE = DROP_DIRECTORY / 'gate.conf'
TOOLS = Path('/usr/libexec/consent/poc')
GATE_TOOLS = Path('/usr/libexec/consent/tests')
MARKER_KEYS = ('proof_kind', 'proof_id', 'receipt', 'artifact', 'session', 'generation',
               'context_digest', 'operation_id', 'feature_id', 'job_id', 'pid',
               'deadline_monotonic_ms')
IDENTIFIER = re.compile(r'[A-Za-z0-9_.-]{1,128}\Z')


def require(condition, message):
    if not condition:
        raise RuntimeError(message)


def command(*arguments, timeout=15):
    result = subprocess.run(arguments, check=False, text=True,
                            stdout=subprocess.PIPE, stderr=subprocess.PIPE,
                            timeout=timeout)
    require(len(result.stdout) + len(result.stderr) <= 262144,
            'command output exceeded test evidence bound')
    require(result.returncode == 0,
            '{} failed with status {}: {}'.format(arguments[0], result.returncode,
                                                 result.stderr.strip()[:512]))
    return result.stdout.strip()


def property_value(unit, name):
    return command('systemctl', 'show', '-p', name, '--value', unit)


def directory(path, exact_mode=None):
    path = Path(path)
    for item in (Path('/'), *reversed(path.parents[:-1]), path):
        info = item.lstat()
        require(stat.S_ISDIR(info.st_mode) and info.st_uid == 0 and
                not info.st_mode & 0o022, 'unsafe protected directory: ' + str(item))
    if exact_mode is not None:
        info = path.lstat()
        require(stat.S_IMODE(info.st_mode) == exact_mode and info.st_gid == 0,
                'unexpected gate directory owner or mode')


def read_file(path, limit=65536, mode=None):
    descriptor = os.open(path, os.O_RDONLY | os.O_CLOEXEC | os.O_NOFOLLOW | os.O_NONBLOCK)
    try:
        info = os.fstat(descriptor)
        require(stat.S_ISREG(info.st_mode) and info.st_uid == 0 and info.st_nlink == 1 and
                not info.st_mode & 0o022 and info.st_size <= limit,
                'unsafe protected file: ' + str(path))
        if mode is not None:
            require(stat.S_IMODE(info.st_mode) == mode, 'unexpected protected file mode')
        data = os.read(descriptor, limit + 1)
        require(len(data) <= limit and len(data) == info.st_size, 'file changed while reading')
        return data, info, os.getxattr(descriptor, 'security.SMACK64')
    finally:
        os.close(descriptor)


def atomic_file(path, data, mode=0o600, group=0, label=b'System'):
    path = Path(path)
    directory(path.parent)
    temporary = path.parent / ('.' + path.name + '.gate-tmp')
    descriptor = os.open(temporary, os.O_WRONLY | os.O_CREAT | os.O_EXCL |
                         os.O_CLOEXEC | os.O_NOFOLLOW, 0o600)
    try:
        os.fchown(descriptor, 0, group)
        os.fchmod(descriptor, mode)
        os.setxattr(descriptor, 'security.SMACK64', label)
        view = memoryview(data)
        while view:
            written = os.write(descriptor, view)
            require(written > 0, 'short protected file write')
            view = view[written:]
        os.fsync(descriptor)
        os.replace(temporary, path)
        parent = os.open(path.parent, os.O_RDONLY | os.O_DIRECTORY | os.O_CLOEXEC)
        try:
            os.fsync(parent)
        finally:
            os.close(parent)
    finally:
        os.close(descriptor)
        if temporary.exists():
            temporary.unlink()


def write_json(name, value):
    atomic_file(DIRECTORY / name,
                (json.dumps(value, sort_keys=True, separators=(',', ':')) + '\n').encode())


def read_json(name, limit=4096):
    data, _, _ = read_file(DIRECTORY / name, limit, 0o600)
    value = json.loads(data)
    require(isinstance(value, dict), 'invalid marker object')
    return value


def stop_units():
    command('systemctl', 'stop', SOCKET, UNIT, DAEMON_SOCKET, DAEMON)
    for unit in (UNIT, DAEMON):
        require(property_value(unit, 'MainPID') == '0', 'service did not stop: ' + unit)
    for unit in UNITS:
        require(property_value(unit, 'ActiveState') in ('inactive', 'failed'),
                'unit did not become inactive: ' + unit)


def load_run():
    directory(DIRECTORY, 0o700)
    value = read_json('run.json', 16384)
    require(value.get('schema') == 1 and value.get('boot_id') ==
            Path('/proc/sys/kernel/random/boot_id').read_text().strip(),
            'gate run belongs to another boot or schema')
    return value


def lock_directory():
    directory(DIRECTORY, 0o700)
    descriptor = os.open(DIRECTORY / 'phase.lock', os.O_RDWR | os.O_CREAT |
                         os.O_CLOEXEC | os.O_NOFOLLOW, 0o600)
    info = os.fstat(descriptor)
    require(stat.S_ISREG(info.st_mode) and info.st_uid == 0 and info.st_nlink == 1 and
            stat.S_IMODE(info.st_mode) == 0o600, 'unsafe phase lock')
    fcntl.flock(descriptor, fcntl.LOCK_EX | fcntl.LOCK_NB)
    # Deliberately retained for the command lifetime, including failure cleanup.
    return descriptor


def cleanup():
    for signum in (signal.SIGHUP, signal.SIGINT, signal.SIGTERM):
        signal.signal(signum, signal.SIG_IGN)
    if not DIRECTORY.exists():
        print('CLEAN no staged gate run')
        return
    run = load_run()
    if run.get('cleaned'):
        restored, _, _ = read_file(ROLES)
        require(hashlib.sha256(restored).hexdigest() == run['roles_sha256'] and
                not DROP_FILE.exists(), 'previously restored configuration changed')
        print('CLEAN original configuration already restored; evidence retained')
        return
    stop_units()
    original, _, _ = read_file(DIRECTORY / 'roles.original', mode=0o600)
    require(hashlib.sha256(original).hexdigest() == run['roles_sha256'],
            'saved role configuration changed')
    read_file(ROLES)
    atomic_file(ROLES, original, run['roles_mode'], run['roles_group'],
                run['roles_label'].encode())
    if DROP_FILE.exists() or DROP_FILE.is_symlink():
        contents, _, _ = read_file(DROP_FILE)
        require(hashlib.sha256(contents).hexdigest() == run['dropin_sha256'],
                'gate unit override changed; refusing to remove it')
        DROP_FILE.unlink()
    if DROP_DIRECTORY.exists():
        require(not list(DROP_DIRECTORY.iterdir()), 'unexpected gate unit override entry')
        DROP_DIRECTORY.rmdir()
    command('systemctl', 'daemon-reload')
    require(not property_value(UNIT, 'DropInPaths'), 'gate override remains loaded')
    # Restore only the units recorded active before this explicit experiment.
    for unit in (DAEMON_SOCKET, DAEMON, SOCKET, UNIT):
        if run['units'][unit] == 'active':
            command('systemctl', 'start', unit)
    restored, _, _ = read_file(ROLES)
    require(hashlib.sha256(restored).hexdigest() == run['roles_sha256'], 'role restoration mismatch')
    run['cleaned'] = True
    write_json('run.json', run)
    print('CLEAN restored original PoC roles and unit configuration; gate evidence retained at ' + str(DIRECTORY))


def prepare(kind):
    directory('/opt/var/lib')
    directory(ROLES.parent)
    directory('/etc/systemd/system')
    require(not DIRECTORY.exists() and not DIRECTORY.is_symlink(),
            'gate evidence already exists; inspect and archive it before another run')
    require(not DROP_DIRECTORY.exists() and not DROP_DIRECTORY.is_symlink(),
            'feature service already has an override directory')
    for unit in UNITS:
        require(not property_value(unit, 'DropInPaths'), 'unexpected unit override: ' + unit)
    states = {unit: property_value(unit, 'ActiveState') for unit in UNITS}
    require(all(value in ('active', 'inactive', 'failed') for value in states.values()),
            'unit is transitioning; repeat after it settles')
    original, info, label = read_file(ROLES)
    parser = configparser.ConfigParser(interpolation=None)
    parser.read_string(original.decode())
    for role in ('argo', 'cm', 'ce', 'holder'):
        section = 'identity mock-' + role
        require(parser[section]['executable'] == str(TOOLS / ('consent-mock-' + role)),
                'unexpected original mock executable')
        require(parser[section]['uid'] == '0' and parser[section]['label'] == 'System',
                'unexpected original mock identity')
        executable = GATE_TOOLS / ('consent-feature-gate-' + role)
        directory(executable.parent)
        _, executable_info, _ = read_file(executable, 16 * 1024 * 1024)
        require(executable_info.st_mode & 0o111, 'gate executable is not executable')
        parser[section]['executable'] = str(executable)
    buffer = io.StringIO()
    parser.write(buffer)
    override = ('[Service]\nExecStart=\nExecStart=' +
                str(GATE_TOOLS / 'consent-feature-gate-argo') + ' feature-serve\n').encode()
    DIRECTORY.mkdir(mode=0o700)
    os.setxattr(DIRECTORY, 'security.SMACK64', b'System')
    lock_directory()
    atomic_file(DIRECTORY / 'roles.original', original)
    run = {'schema': 1, 'kind': kind,
           'boot_id': Path('/proc/sys/kernel/random/boot_id').read_text().strip(),
           'units': states, 'roles_sha256': hashlib.sha256(original).hexdigest(),
           'roles_mode': stat.S_IMODE(info.st_mode), 'roles_group': info.st_gid,
           'roles_label': label.decode(), 'dropin_sha256': hashlib.sha256(override).hexdigest(),
           'started_realtime': int(time.time()), 'cleaned': False}
    write_json('run.json', run)
    try:
        stop_units()
        atomic_file(DIRECTORY / 'kind', (kind + '\n').encode())
        os.mkfifo(DIRECTORY / 'release.fifo', 0o600)
        os.setxattr(DIRECTORY / 'release.fifo', 'security.SMACK64', b'System')
        DROP_DIRECTORY.mkdir(mode=0o755)
        atomic_file(DROP_FILE, override, 0o644)
        atomic_file(ROLES, buffer.getvalue().encode(), stat.S_IMODE(info.st_mode), info.st_gid, label)
        command('systemctl', 'daemon-reload')
        command('systemctl', 'start', DAEMON_SOCKET, DAEMON, SOCKET, UNIT)
        pid = int(property_value(UNIT, 'MainPID'))
        require(pid > 0 and os.readlink('/proc/{}/exe'.format(pid)) ==
                str(GATE_TOOLS / 'consent-feature-gate-argo'), 'test coordinator did not start')
        require(property_value(UNIT, 'StandardOutput') == 'journal', 'action journal evidence unavailable')
        run['coordinator_pid'] = pid
        write_json('run.json', run)
        print('READY dedicated {} gate coordinator PID {}; use the real UI to select and run a task'.format(kind, pid))
    except BaseException:
        try:
            cleanup()
        except BaseException as error:
            print('CLEANUP FAILED; protected backup retained: ' + str(error), file=sys.stderr)
        raise


def ready_marker(run):
    marker = read_json('ready.json', 2048)
    require(marker.get('schema') == 1 and marker.get('state') == 'armed', 'invalid ready marker')
    require(marker.get('pid') == run['coordinator_pid'], 'marker PID differs from staged coordinator')
    for key in ('proof_id', 'operation_id', 'feature_id', 'job_id'):
        require(isinstance(marker.get(key), str) and IDENTIFIER.fullmatch(marker[key]),
                'invalid authorization evidence field')
    if run['kind'] == 'acquisition':
        require(marker.get('proof_kind') == 'acquisition-receipt' and
                marker.get('receipt') == marker['proof_id'] and
                not any(key in marker for key in ('artifact', 'session', 'generation', 'context_digest')),
                'acquisition marker does not contain an actual receipt proof')
    else:
        require(run['kind'] == 'reuse' and marker.get('proof_kind') == 'artifact-permit' and
                marker.get('artifact') == marker['proof_id'] and
                'receipt' not in marker and 'original_receipt' not in marker,
                'reuse permit must not be mislabelled as an acquisition receipt')
        require(isinstance(marker.get('session'), str) and IDENTIFIER.fullmatch(marker['session']) and
                isinstance(marker.get('generation'), str) and
                re.fullmatch(r'[1-9][0-9]{0,18}', marker['generation']) and
                int(marker['generation']) <= 9223372036854775807 and
                isinstance(marker.get('context_digest'), str) and
                re.fullmatch(r'[0-9a-f]{64}', marker['context_digest']) and
                marker['feature_id'] == 'calendar.reuse',
                'reuse proof is missing its exact session, generation or context digest')
    require(marker['operation_id'].startswith(marker['job_id'] + '.'),
            'authorization operation is not derived from the recorded job')
    require(os.readlink('/proc/{}/exe'.format(marker['pid'])) ==
            str(GATE_TOOLS / 'consent-feature-gate-argo'), 'coordinator PID was replaced')
    return marker


def wait_ready(timeout):
    run = load_run()
    require(not run['cleaned'], 'gate run already cleaned')
    deadline = time.monotonic() + timeout
    while time.monotonic() < deadline:
        if (DIRECTORY / 'ready.json').exists():
            try:
                marker = ready_marker(run)
            except (json.JSONDecodeError, RuntimeError):
                time.sleep(0.02)
                continue
            require(time.monotonic() * 1000 < marker['deadline_monotonic_ms'], 'gate already expired')
            write_json('observed.json', marker)
            print(json.dumps(marker, sort_keys=True))
            print('NEXT clear/save selection through the real UI before the 30-second gate deadline')
            return
        time.sleep(0.05)
    raise RuntimeError('actual AUTHORIZE marker did not arrive before bounded wait')


def cancelled(run):
    ready = ready_marker(run)
    observed = read_json('observed.json', 2048)
    require(observed == ready, 'ready marker was not captured by wait phase')
    terminal = read_json('terminal.json', 2048)
    require(terminal.get('schema') == 1 and terminal.get('state') == 'cancelled' and
            all(terminal.get(key) == ready.get(key) for key in MARKER_KEYS),
            'actor did not cancel this exact authorized job before release')
    return ready


def release():
    run = load_run()
    marker = cancelled(run)
    descriptor = os.open(DIRECTORY / 'release.fifo', os.O_RDWR | os.O_NONBLOCK |
                         os.O_CLOEXEC | os.O_NOFOLLOW)
    try:
        info = os.fstat(descriptor)
        require(stat.S_ISFIFO(info.st_mode) and info.st_uid == 0 and info.st_gid == 0 and
                info.st_nlink == 1 and stat.S_IMODE(info.st_mode) == 0o600, 'unsafe release FIFO')
        require(os.write(descriptor, b'R') == 1, 'release FIFO write failed')
    finally:
        os.close(descriptor)
    marker['released_after_cancel'] = True
    marker['released_monotonic_ms'] = int(time.monotonic() * 1000)
    write_json('release.json', marker)
    print('RELEASE sent after actor cancellation; no UI identity was bypassed')


def audit():
    run = load_run()
    marker = cancelled(run)
    released = read_json('release.json', 2048)
    require(released.get('schema') == 1 and released.get('released_after_cancel') is True and
            all(released.get(key) == marker.get(key) for key in MARKER_KEYS), 'release evidence mismatch')
    time.sleep(0.3)
    journal = command('journalctl', '--no-pager', '-u', UNIT, '--since',
                      '@' + str(run['started_realtime']), '-o', 'json')
    actions = []
    for line in journal.splitlines():
        event = json.loads(line)
        message = event.get('MESSAGE', '')
        try:
            payload = json.loads(message)
        except (json.JSONDecodeError, TypeError):
            continue
        if payload.get('event') == 'action' and payload.get('operation') == marker['operation_id']:
            actions.append(payload)
    require(not actions, 'protected mock action ran after selected job cancellation')
    evidence = dict(marker, cancelled_before_release=True, observed_action_events=0,
                    real_ui_disable_evidence='capture separately with the UI driver')
    write_json('audit.json', evidence)
    atomic_file(DIRECTORY / 'journal.jsonl', (journal + '\n').encode())
    print('PASS actual {} -> actor cancellation -> release; matching action events=0'.format(marker['proof_kind']))


def interrupted(signum, frame):
    del frame
    raise RuntimeError('test phase interrupted by signal ' + str(signum))


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('phase', choices=('prepare', 'wait', 'release', 'audit', 'cleanup'))
    parser.add_argument('--timeout', type=int, default=120)
    parser.add_argument('--kind', choices=('acquisition', 'reuse'),
                        help='prepare stage (default acquisition); later phases verify the saved kind when supplied')
    args = parser.parse_args()
    require(os.geteuid() == 0, 'run as root in System::Privileged provisioning context')
    require(1 <= args.timeout <= 180, 'wait timeout must be 1..180 seconds')
    os.umask(0o077)
    for signum in (signal.SIGHUP, signal.SIGINT, signal.SIGTERM):
        signal.signal(signum, interrupted)
    if args.phase == 'prepare':
        prepare(args.kind or 'acquisition')
    else:
        if DIRECTORY.exists():
            lock_directory()
        try:
            if args.kind is not None:
                require(load_run().get('kind') == args.kind, 'requested gate kind differs from staged run')
            if args.phase == 'cleanup':
                cleanup()
            elif args.phase == 'wait':
                wait_ready(args.timeout)
            elif args.phase == 'release':
                release()
            else:
                audit()
                cleanup()
        except BaseException:
            if args.phase != 'cleanup' and (DIRECTORY / 'run.json').exists():
                try:
                    cleanup()
                except BaseException as error:
                    print('CLEANUP FAILED; protected backup retained: ' + str(error), file=sys.stderr)
            raise


if __name__ == '__main__':
    try:
        main()
    except Exception as error:
        print('FAIL feature gate: ' + str(error), file=sys.stderr)
        sys.exit(1)
