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
# Owned UI09 namespace/bootstrap fixture. Approval remains the actual native UI.
# Fixed paths only; no PoC RPM installation or product role changes.
# Host orchestration records and restores the separately installed UI TPK.

import argparse
import configparser
import grp
import hashlib
import json
import os
from pathlib import Path
import pwd
import re
import stat
import subprocess
import time
import uuid

ROOT = Path('/opt/var/lib/consent-ui-native-09')
STATE = Path('/opt/var/lib/consent-ui09-state')
AUTHORITY = Path('/opt/var/lib/consent-ui09-authority')
TOOLS = Path('/usr/libexec/consent/poc')
TESTS = ROOT / 'tests'
UNIT_ROOT = Path('/run/systemd/system')
UNITS = ('consentd-ui09.socket', 'consentd-ui09.service',
         'consent-feature-ui09.socket', 'consent-feature-ui09.service')
OLD_UNITS = ('consentd-poc.service', 'consentd-poc.socket',
             'consent-feature-poc.service', 'consent-feature-poc.socket')
SOCKETS = (Path('/opt/var/lib/consent-poc-runtime/consent.sock'),
           Path('/opt/var/lib/consent-feature-runtime/argo.sock'))
LIBRARIES = ('libconsent-poc.so.0.1.0', 'libconsent-feature-poc.so.0.1.0')
HELPERS = ('consentd-ui09', 'consent-storage-prepare-ui09',
           'consent-installation-authority-ui09', 'consent-ui09-admin',
           'emulator-ui-native.py')
TOOL_NAMES = tuple('consent-mock-' + role for role in
                   ('argo', 'installer', 'cm', 'ce', 'holder')) + (
                       'consent-poc-launch',)
PAYLOAD_NAMES = ({'tests/' + name for name in HELPERS} |
                 {'tools/' + name for name in TOOL_NAMES} |
                 {'libraries/' + name for name in LIBRARIES} |
                 {'tpk/original.tpk', 'tpk/ui09.tpk'})
IDENTIFIER = re.compile(r'[A-Za-z0-9_.-]{1,128}\Z')


def require(condition, message):
    if not condition:
        raise RuntimeError(message)


def command(*arguments, timeout=20):
    result = subprocess.run(arguments, capture_output=True, text=True,
                            timeout=timeout, check=False)
    require(len(result.stdout) + len(result.stderr) <= 1048576,
            'command evidence exceeded bound')
    print(json.dumps({'command': arguments, 'exit': result.returncode,
                      'stdout': result.stdout, 'stderr': result.stderr}),
          flush=True)
    require(result.returncode == 0, 'command failed: ' + arguments[0])
    return result.stdout.strip()


def protected(path, directory=False, uid=0):
    info = path.lstat()
    require((stat.S_ISDIR(info.st_mode) if directory else
             stat.S_ISREG(info.st_mode)) and info.st_uid == uid and
            not info.st_mode & 0o022 and
            (directory or info.st_nlink == 1), 'unprotected path: ' + str(path))
    return info


def unit_parent():
    info = UNIT_ROOT.lstat()
    require(stat.S_ISDIR(info.st_mode) and info.st_uid == 0,
            'foreign systemd unit parent')
    if not info.st_mode & 0o022:
        return protected(UNIT_ROOT, directory=True)
    # This canonical platform directory is shared with trusted system writers;
    # its label does not establish exclusivity against other system services.
    group = grp.getgrnam('system_fw')
    require(stat.S_IMODE(info.st_mode) == 0o775 and info.st_gid == group.gr_gid
            and os.getxattr(UNIT_ROOT, 'security.SMACK64',
                            follow_symlinks=False) == b'System::Run',
            'untrusted shared systemd unit parent')
    return info


def read(path, limit=1048576, uid=0):
    identity = protected(path, uid=uid)
    descriptor = os.open(path, os.O_RDONLY | os.O_NOFOLLOW | os.O_CLOEXEC)
    try:
        require(os.fstat(descriptor) == identity, 'file identity changed')
        chunks = bytearray()
        while len(chunks) <= limit:
            part = os.read(descriptor, min(65536, limit + 1 - len(chunks)))
            if not part:
                break
            chunks.extend(part)
        require(len(chunks) <= limit, 'file exceeded bound')
        return bytes(chunks)
    finally:
        os.close(descriptor)


def digest(path):
    return hashlib.sha256(read(path, 32 * 1024 * 1024)).hexdigest()


def save(path, content, mode=0o600):
    descriptor = os.open(path, os.O_WRONLY | os.O_CREAT | os.O_EXCL |
                         os.O_NOFOLLOW | os.O_CLOEXEC, mode)
    with os.fdopen(descriptor, 'wb') as stream:
        os.fchmod(stream.fileno(), mode)
        stream.write(content)
        stream.flush()
        os.fsync(stream.fileno())


def show(unit, field):
    return command('systemctl', 'show', '-p', field, '--value', unit)


def payload():
    for path in (Path('/opt'), Path('/opt/var'), Path('/opt/var/lib'), ROOT,
                 ROOT / 'tools', ROOT / 'libraries', TESTS):
        protected(path, directory=True)
    unit_parent()
    require(read(ROOT / 'marker') == b'CONSENT-UI-NATIVE-09\n',
            'foreign fixture marker')
    manifest = json.loads(read(ROOT / 'payload.json'))
    require(manifest.get('task') == 'CONSENT-UI-NATIVE-09' and
            type(manifest.get('release')) is int and
            manifest['release'] in (27, 28), 'wrong package snapshot')
    files = manifest.get('files')
    require(isinstance(files, dict) and 1 <= len(files) <= 128,
            'invalid payload manifest')
    for name, expected in files.items():
        path = Path(name)
        require(not path.is_absolute() and '..' not in path.parts and
                name in PAYLOAD_NAMES,
                'unexpected payload path')
        for parent in (ROOT / path).parents:
            if parent == ROOT:
                break
            protected(parent, directory=True)
        require(isinstance(expected, str) and
                re.fullmatch('[0-9a-f]{64}', expected) and
                digest(ROOT / path) == expected, 'payload hash mismatch')
    for name in HELPERS:
        require('tests/' + name in files, 'missing owned helper')
        data = read(TESTS / name, 32 * 1024 * 1024)
        if name != 'emulator-ui-native.py':
            require(data.startswith(b'\x7fELF') and
                    TESTS.joinpath(name).lstat().st_mode & 0o111,
                    'owned native helper invalid')
    for name in LIBRARIES:
        require('libraries/' + name in files, 'missing fixed native library')
        source = ROOT / 'libraries' / name
        require(read(source, 32 * 1024 * 1024).startswith(b'\x7fELF'),
                'native payload is not ELF')
        original = Path('/usr/lib64') / name
        protected(original.parent, directory=True)
        protected(original)
        print(json.dumps({'original_library': str(original),
                          'sha256': digest(original),
                          'staged_sha256': digest(source),
                          'inode': original.lstat().st_ino}), flush=True)
        require(command('rpm', '-qf', str(original)).startswith('consent-poc-'),
                'original library is not owned by PoC package')
        soname = name.replace('.0.1.0', '.0')
        link = original.parent / soname
        require(link.is_symlink() and link.lstat().st_uid == 0 and
                link.resolve(strict=True) == original,
                'unexpected installed SONAME chain')
    for role in ('argo', 'installer', 'cm', 'ce', 'holder'):
        require('tools/consent-mock-' + role in files,
                'missing fixed role executable')
        original = TOOLS / ('consent-mock-' + role)
        staged = ROOT / 'tools' / original.name
        protected(original)
        require(os.getxattr(original, 'security.SMACK64') ==
                os.getxattr(staged, 'security.SMACK64'),
                'staged executable label differs from installed template')
        print(json.dumps({'original_tool': str(original),
                          'sha256': digest(original),
                          'staged_sha256': digest(staged),
                          'staged_inode': staged.lstat().st_ino}), flush=True)
    return manifest


def bindings():
    lines = ['PrivateMounts=yes',
             'BindReadOnlyPaths=' + str(ROOT / 'tools') + ':' + str(TOOLS)]
    for name in LIBRARIES:
        lines.append('BindReadOnlyPaths=' + str(ROOT / 'libraries' / name) +
                     ':/usr/lib64/' + name)
    return '\n'.join(lines) + '\n'


def socket_unit(index):
    service = UNITS[index * 2 + 1]
    return ('[Unit]\nDescription=Owned UI09 development socket\n\n'
            '[Socket]\nListenStream=' + str(SOCKETS[index]) + '\n'
            'SocketUser=root\nSocketGroup=users\nSocketMode=0660\n'
            'DirectoryMode=0755\nAccept=no\nBacklog=64\nRemoveOnStop=yes\n'
            'Service=' + service + '\n')


def service_unit(feature=False):
    if feature:
        return ('[Unit]\nDescription=Owned UI09 choice coordinator\n'
                'Requires=consent-feature-ui09.socket consentd-ui09.socket\n'
                'After=consent-feature-ui09.socket consentd-ui09.socket\n\n'
                '[Service]\nType=simple\nExecStart=' + str(TOOLS) +
                '/consent-mock-argo feature-serve-choice\n'
                'User=root\nGroup=root\nSockets=consent-feature-ui09.socket\n'
                + service_tail())
    return ('[Unit]\nDescription=Owned UI09 development daemon\n'
            'Requires=consentd-ui09.socket\nAfter=consentd-ui09.socket\n\n'
            '[Service]\nType=notify\nNotifyAccess=main\nExecStart=' +
            str(TESTS / 'consentd-ui09') + '\nExecStartPre=+' +
            str(TESTS / 'consent-storage-prepare-ui09') +
            '\nExecStartPost=+' + str(TESTS / 'consent-storage-prepare-ui09') +
            ' --complete\nUser=security_fw\nGroup=security_fw\n'
            'Sockets=consentd-ui09.socket\n' + service_tail())


def service_tail():
    return ('AmbientCapabilities=CAP_SYS_PTRACE\n'
            'CapabilityBoundingSet=CAP_SYS_PTRACE\nSmackProcessLabel=System\n'
            'UMask=0077\nRestart=no\nTimeoutStartSec=15\nTimeoutStopSec=10\n'
            'KillMode=control-group\nStandardOutput=journal\n'
            'StandardError=journal\nNoNewPrivileges=yes\n' + bindings())


def managed_units():
    return {UNITS[0]: socket_unit(0), UNITS[1]: service_unit(),
            UNITS[2]: socket_unit(1), UNITS[3]: service_unit(True)}


def verify_units():
    for unit, text in managed_units().items():
        path = UNIT_ROOT / unit
        require(read(path) == text.encode() and
                stat.S_IMODE(path.lstat().st_mode) == 0o644,
                'managed unit content or mode changed')
        require(show(unit, 'FragmentPath') == str(path),
                'effective unit is not the managed fragment')
        require(not show(unit, 'DropInPaths'), 'foreign unit drop-in')


def actor(acquired, tool, *arguments):
    require(tool in ('consent-mock-installer',), 'unsupported setup actor')
    unit = 'consent-ui09-installer-' + uuid.uuid4().hex
    require(show(unit + '.service', 'LoadState') == 'not-found',
            'foreign setup actor unit')
    path = Path('/run/systemd/transient') / (unit + '.service')
    args = ['systemd-run', '--quiet', '--wait', '--pipe', '--collect',
            '--unit=' + unit, '-p', 'User=root', '-p', 'Group=root',
            '-p', 'SmackProcessLabel=System', '-p', 'PrivateMounts=yes',
            '-p', 'RuntimeMaxSec=15', '-p', 'TimeoutStartSec=15']
    for line in bindings().splitlines():
        if line.startswith('BindReadOnlyPaths='):
            args.extend(('-p', line))
    args.extend((str(TOOLS / tool), *arguments))
    acquired['transient'].append((unit + '.service', path))
    save_actor(unit + '.service', path, [str(TOOLS / tool), *arguments])
    child = subprocess.Popen(args, stdout=subprocess.PIPE,
                             stderr=subprocess.PIPE, text=True)
    acquired['children'].append(child)
    try:
        stdout, stderr = child.communicate(timeout=25)
        require(len(stdout) + len(stderr) <= 1048576,
                'installer evidence exceeded bound')
        print(json.dumps({'command': args, 'exit': child.returncode,
                          'stdout': stdout, 'stderr': stderr}), flush=True)
        require(child.returncode == 0, 'installer actor failed')
        drain_transient(unit + '.service', path)
        return stdout.strip()
    finally:
        if child.poll() is None:
            child.terminate()
            try:
                child.wait(timeout=3)
            except subprocess.TimeoutExpired:
                child.kill()
                child.wait(timeout=3)
        if child.stdout:
            child.stdout.close()
        if child.stderr:
            child.stderr.close()


def save_actor(unit, path, arguments):
    ledger = ROOT / 'actors.json'
    if ledger.exists():
        records = json.loads(read(ledger))
    else:
        records = {}
    require(len(records) < 16 and unit not in records, 'actor ledger exceeded')
    records[unit] = {'fragment': str(path), 'arguments': arguments}
    data = json.dumps(records).encode()
    if not ledger.exists():
        save(ledger, data)
        return
    identity = protected(ledger)
    descriptor = os.open(ledger, os.O_WRONLY | os.O_NOFOLLOW | os.O_CLOEXEC)
    try:
        require(os.fstat(descriptor) == identity, 'actor ledger changed')
        os.ftruncate(descriptor, 0)
        require(os.write(descriptor, data) == len(data), 'short actor ledger')
        os.fsync(descriptor)
    finally:
        os.close(descriptor)


def drain_transient(unit, path):
    if show(unit, 'LoadState') == 'not-found':
        return
    protected(path)
    require(show(unit, 'FragmentPath') == str(path) and
            show(unit, 'Transient') == 'yes' and
            not show(unit, 'DropInPaths') and show(unit, 'User') == 'root',
            'foreign transient actor fragment')
    record = json.loads(read(ROOT / 'actors.json'))[unit]
    expected = record['arguments']
    executable = show(unit, 'ExecStart')
    prefix = '{ path=' + expected[0] + ' ; argv[]=' + ' '.join(expected) + ' ;'
    require(executable.startswith(prefix),
            'transient actor executable or arguments changed')
    require(show(unit, 'PrivateMounts') == 'yes',
            'transient actor lost mount isolation')
    fragment_digest = digest(path)
    print(json.dumps({'transient': unit, 'fragment_sha256': fragment_digest}),
          flush=True)
    require(digest(path) == fragment_digest, 'transient fragment changed')
    command('systemctl', 'stop', unit)
    wait_empty(unit)


def cgroup_empty(group):
    if not isinstance(group, str) or not group.startswith('/') or (
            '..' in Path(group).parts or len(group) > 1024):
        raise RuntimeError('invalid owned cgroup path')
    rows = Path('/proc/self/mountinfo').read_text().splitlines()
    systemd = []
    unified = []
    for row in rows:
        left, separator, right = row.partition(' - ')
        if not separator:
            raise RuntimeError('invalid kernel mount record')
        fields, filesystem = left.split(), right.split()
        if filesystem[0] == 'cgroup' and 'name=systemd' in filesystem[2]:
            systemd.append(Path(fields[4]))
        elif filesystem[0] == 'cgroup2':
            unified.append(Path(fields[4]))
    mounts = systemd or unified
    if len(mounts) != 1:
        raise RuntimeError('unknown systemd cgroup hierarchy')
    path = mounts[0] / group.lstrip('/')
    try:
        path.lstat()
    except FileNotFoundError:
        return True
    count = 0
    def failed_walk(error):
        raise error
    for directory, children, _ in os.walk(path, followlinks=False,
                                           onerror=failed_walk):
        count += 1
        if count > 4096:
            raise RuntimeError('cgroup descendants exceeded bound')
        for child in children:
            if (Path(directory) / child).is_symlink():
                raise RuntimeError('foreign cgroup link')
        name = 'tasks' if systemd else 'cgroup.procs'
        file = Path(directory) / name
        with file.open() as stream:
            content = stream.read(1048577)
        if len(content) > 1048576:
            raise RuntimeError('cgroup task list exceeded bound')
        if content.strip():
            return False
    return True


def wait_empty(unit):
    deadline = time.monotonic() + 12
    while time.monotonic() < deadline:
        pid = show(unit, 'MainPID')
        cgroup = show(unit, 'ControlGroup')
        if pid in ('', '0') and (not cgroup or cgroup_empty(cgroup)):
            return
        time.sleep(0.1)
    raise RuntimeError('owned cgroup did not drain')



def setup(acquired):
    payload()
    # All external unit/path conflicts are checked before the first mutation.
    for unit in (*UNITS, 'consent-ui09-installer.service'):
        require(show(unit, 'LoadState') == 'not-found' and
                not (UNIT_ROOT / unit).exists(), 'unit already exists')
    for unit in OLD_UNITS:
        require(show(unit, 'ActiveState') in ('inactive', 'failed') and
                show(unit, 'MainPID') in ('', '0'), 'original PoC is running')
    for path in (*SOCKETS, STATE, AUTHORITY, ROOT / 'roles.conf',
                 ROOT / 'generation', ROOT / 'managed.json',
                 ROOT / 'actors.json',
                 ROOT / 'acquired.json'):
        require(not os.path.lexists(path), 'fresh fixture path already exists')
    require(pwd.getpwnam('security_fw').pw_uid > 0, 'invalid daemon account')
    mounts = command('findmnt', '-rn', '-o', 'TARGET').splitlines()
    require(all(str(path) not in mounts for path in
                (TOOLS, *(Path('/usr/lib64') / name for name in LIBRARIES))),
            'existing global tool/library mount')
    original = configparser.ConfigParser(interpolation=None, strict=True)
    original.read_string(read(Path('/etc/consent-poc/roles.conf')).decode())
    for role in ('installer', 'argo', 'cm', 'ce', 'holder'):
        section = original['identity mock-' + role]
        require(section['uid'] == '0' and section['label'] == 'System' and
                section['executable'] == str(TOOLS / ('consent-mock-' + role)),
                'unexpected original actor identity')
        if role in ('cm', 'ce'):
            require(section['roles'] == 'checker;', 'wrong enforcer role')
            section['enforcers'] = 'mock-' + role + ';'
    ui = original['identity consent-ui']
    require(ui['executable'] == '/usr/bin/dotnet-hydra-loader' and
            ui['label'] == 'User::Pkg::org.tizen.consentui' and
            ui['roles'] == 'ui;' and ui['subjects'] == 'owner;' and
            ui['profiles'] == 'default;' and int(ui['uid']) > 0,
            'unexpected provisioned UI identity')
    require('identity ui09-admin' not in original, 'foreign admin identity')
    original['identity ui09-admin'] = {
        'uid': '0', 'executable': str(TESTS / 'consent-ui09-admin'),
        'label': 'System', 'roles': 'admin;',
        'subjects': 'owner;', 'profiles': 'default;'}
    import io
    buffer = io.StringIO()
    original.write(buffer)
    save(ROOT / 'roles.conf', buffer.getvalue().encode(), 0o640)
    os.chown(ROOT / 'roles.conf', 0, pwd.getpwnam('security_fw').pw_gid)
    command('chsmack', '-a', 'System', str(ROOT / 'roles.conf'))
    # FirstInstall creates authority only; STATE must remain absent here.
    command(str(TESTS / 'consent-storage-prepare-ui09'), '--first-install')
    require(not STATE.exists(), 'first-install unexpectedly created state')
    authority = str(TESTS / 'consent-installation-authority-ui09')
    package = 'org.tizen.consentui'
    generation = command(authority, '--image-root', '/', 'begin', package,
                         'ui09-begin', 'absent')
    require(IDENTIFIER.fullmatch(generation), 'invalid installation generation')
    command(authority, '--image-root', '/', 'attach', package, package,
            'ui09-attach', generation)
    command(authority, '--image-root', '/', 'commit', package, 'ui09-commit',
            generation)
    require(not STATE.exists(), 'generation provisioning created state')
    save(ROOT / 'generation', (generation + '\n').encode())
    for index, path in enumerate(SOCKETS):
        parent = path.parent
        if not parent.exists():
            parent.mkdir(mode=0o755)
            acquired['directories'][str(parent)] = parent.lstat()
            command('chsmack', '-a', '_', str(parent))
        info = protected(parent, directory=True)
        require(info.st_uid == 0 and stat.S_IMODE(info.st_mode) == 0o755,
                'runtime directory protection mismatch')
        # Existing runtime directories are observed, never relabeled.
        require(os.getxattr(parent, 'security.SMACK64') == b'_',
                'existing runtime label differs from verified template')
    for unit, text in managed_units().items():
        save(UNIT_ROOT / unit, text.encode(), 0o644)
        acquired['units'][unit] = (UNIT_ROOT / unit).lstat()
        command('chsmack', '-a', 'System', str(UNIT_ROOT / unit))
    save(ROOT / 'managed.json', json.dumps({
        'units': {unit: hashlib.sha256(text.encode()).hexdigest()
                  for unit, text in managed_units().items()},
        'created_runtime': {name: {
            'dev': info.st_dev, 'inode': info.st_ino, 'uid': info.st_uid,
            'gid': info.st_gid, 'mode': stat.S_IMODE(info.st_mode),
            'smack': '_'}
            for name, info in acquired['directories'].items()}}).encode())
    manifest = json.loads(read(ROOT / 'payload.json'))
    if 'invocation' in manifest:
        nonce = manifest['invocation']
        require(isinstance(nonce, str) and len(nonce) == 64 and
                all(character in '0123456789abcdef' for character in nonce),
                'invalid invocation nonce')
        identity = manifest['root_identity']
        info = protected(ROOT, directory=True)
        require(info.st_dev == identity['dev'] and
                info.st_ino == identity['inode'], 'invocation root changed')
        save(ROOT / 'acquired.json', json.dumps({
            'invocation': nonce, 'root_identity': identity,
            'created_runtime': json.loads(read(ROOT / 'managed.json'))[
                'created_runtime'],
            'roles_sha256': hashlib.sha256(
                read(ROOT / 'roles.conf')).hexdigest(),
            'units': {unit: hashlib.sha256(text.encode()).hexdigest()
                      for unit, text in managed_units().items()}}).encode())
    command('systemctl', 'daemon-reload')
    verify_units()
    command('systemctl', 'start', UNITS[0], UNITS[1])
    actor(acquired, 'consent-mock-installer', 'feature-register-choice', generation)
    command('systemctl', 'start', UNITS[2], UNITS[3])
    for unit in (UNITS[1], UNITS[3]):
        require(show(unit, 'ActiveState') == 'active' and
                int(show(unit, 'MainPID')) > 0, 'owned service did not start')
    print('READY UI09 explicit period-choice fixture', flush=True)


def stop():
    verify_units()
    command('systemctl', 'stop', UNITS[2], UNITS[3], UNITS[0], UNITS[1])
    for unit in (UNITS[1], UNITS[3]):
        require(show(unit, 'MainPID') == '0' and
                show(unit, 'ActiveState') == 'inactive',
                'owned daemon survived')
        wait_empty(unit)
    require(all(not os.path.lexists(path) for path in SOCKETS),
            'owned socket survived stop')


def cleanup():
    payload()
    if (ROOT / 'actors.json').exists():
        records = json.loads(read(ROOT / 'actors.json'))
        require(isinstance(records, dict) and len(records) <= 16,
                'invalid actor ledger')
        for unit, record in records.items():
            require(re.fullmatch(r'consent-ui09-installer-[0-9a-f]{32}\.service',
                                 unit), 'foreign transient ledger unit')
            path = Path('/run/systemd/transient') / unit
            require(record['fragment'] == str(path), 'foreign actor fragment')
            drain_transient(unit, path)
    # Partial setup is allowed; existing managed fragments must still be exact.
    present = [unit for unit in UNITS if os.path.lexists(UNIT_ROOT / unit)]
    if present:
        for unit in present:
            require(read(UNIT_ROOT / unit) == managed_units()[unit].encode(),
                    'foreign partial unit')
        command('systemctl', 'daemon-reload')
        for unit in present:
            require(show(unit, 'FragmentPath') == str(UNIT_ROOT / unit) and
                    not show(unit, 'DropInPaths'), 'foreign effective unit')
        # Stop every present owned socket before its service, including a failed
        # bootstrap. A missing STATE is valid here and is never recreated.
        command('systemctl', 'stop', *present)
        for unit in present:
            if unit.endswith('.service'):
                wait_empty(unit)
        for unit in present:
            if show(unit, 'ActiveState') == 'failed':
                command('systemctl', 'reset-failed', unit)
            (UNIT_ROOT / unit).unlink()
        command('systemctl', 'daemon-reload')
    for unit in UNITS:
        require(show(unit, 'LoadState') == 'not-found', 'unit remains loaded')
    # Keep fixture data for audit/removal. Cleanup does not reinstall UI.
    print('CLEANUP services absent; owned state retained for audit', flush=True)


def remove_owned(directory, uid):
    if not os.path.lexists(directory):
        return
    identity = protected(directory, directory=True, uid=uid)
    descriptor = os.open(directory, os.O_RDONLY | os.O_DIRECTORY |
                         os.O_NOFOLLOW | os.O_CLOEXEC)
    try:
        require(os.fstat(descriptor) == identity, 'directory identity changed')
        remove_contents(descriptor)
        current = directory.lstat()
        require((current.st_dev, current.st_ino) ==
                (identity.st_dev, identity.st_ino), 'directory was replaced')
        directory.rmdir()
    finally:
        os.close(descriptor)


def remove_contents(descriptor):
    allowed = (0, pwd.getpwnam('security_fw').pw_uid)
    names = os.listdir(descriptor)
    require(len(names) <= 4096, 'owned cleanup exceeded entry bound')
    for name in names:
        require(name not in ('.', '..') and '/' not in name,
                'invalid cleanup leaf')
        identity = os.stat(name, dir_fd=descriptor, follow_symlinks=False)
        require(identity.st_uid in allowed and
                not identity.st_mode & 0o022, 'foreign cleanup entry')
        if stat.S_ISDIR(identity.st_mode):
            child = os.open(name, os.O_RDONLY | os.O_DIRECTORY |
                            os.O_NOFOLLOW | os.O_CLOEXEC, dir_fd=descriptor)
            try:
                require(os.fstat(child) == identity, 'cleanup directory changed')
                remove_contents(child)
                os.rmdir(name, dir_fd=descriptor)
            finally:
                os.close(child)
        else:
            require(stat.S_ISREG(identity.st_mode) and identity.st_nlink == 1,
                    'foreign cleanup file type or hardlink')
            child = os.open(name, os.O_RDONLY | os.O_NOFOLLOW | os.O_CLOEXEC,
                            dir_fd=descriptor)
            try:
                require(os.fstat(child) == identity, 'cleanup file changed')
                current = os.stat(name, dir_fd=descriptor,
                                  follow_symlinks=False)
                require((current.st_dev, current.st_ino) ==
                        (identity.st_dev, identity.st_ino),
                        'cleanup file path changed')
                os.unlink(name, dir_fd=descriptor)
            finally:
                os.close(child)


def purge():
    cleanup()
    require(all(not os.path.lexists(path) for path in SOCKETS),
            'endpoint exists during owned purge')
    remove_owned(STATE, pwd.getpwnam('security_fw').pw_uid)
    remove_owned(AUTHORITY, 0)
    # The payload tree is retained until host evidence has been archived.
    print('PURGE owned state/authority absent; payload retained', flush=True)


def rollback(acquired):
    errors = []
    for unit, path in acquired['transient']:
        try:
            drain_transient(unit, path)
        except BaseException as error:
            errors.append(str(error))
    if acquired['units']:
        try:
            for unit, identity in acquired['units'].items():
                current = protected(UNIT_ROOT / unit)
                require((current.st_dev, current.st_ino) ==
                        (identity.st_dev, identity.st_ino) and
                        read(UNIT_ROOT / unit) ==
                        managed_units()[unit].encode(),
                        'acquired unit identity changed')
            command('systemctl', 'daemon-reload')
            for unit in acquired['units']:
                require(show(unit, 'FragmentPath') == str(UNIT_ROOT / unit),
                        'acquired effective fragment changed')
                command('systemctl', 'stop', unit)
                if unit.endswith('.service'):
                    wait_empty(unit)
            for unit in acquired['units']:
                (UNIT_ROOT / unit).unlink()
            command('systemctl', 'daemon-reload')
        except BaseException as error:
            errors.append(str(error))
    for name, identity in acquired['directories'].items():
        try:
            path = Path(name)
            current = protected(path, directory=True)
            require((current.st_dev, current.st_ino) ==
                    (identity.st_dev, identity.st_ino),
                    'acquired runtime directory changed')
            path.rmdir()
        except BaseException as error:
            errors.append(str(error))
    require(not errors, 'rollback failures: ' + '; '.join(errors))


def main():
    parser = argparse.ArgumentParser()
    parser.add_argument('phase', choices=('setup', 'stop', 'cleanup', 'purge', 'status'))
    args = parser.parse_args()
    require(os.geteuid() == 0, 'root-owned development fixture required')
    if args.phase == 'setup':
        acquired = {'units': {}, 'directories': {}, 'transient': [],
                    'children': []}
        try:
            setup(acquired)
        except BaseException:
            try:
                rollback(acquired)
            except BaseException as cleanup_error:
                print('TEARDOWN_FAILED ' + str(cleanup_error), flush=True)
            raise
    elif args.phase == 'stop':
        payload()
        stop()
    elif args.phase == 'cleanup':
        cleanup()
    elif args.phase == 'purge':
        purge()
    else:
        payload()
        verify_units()
        for unit in UNITS:
            print(unit, show(unit, 'ActiveState'), show(unit, 'MainPID'))


if __name__ == '__main__':
    try:
        main()
    except BaseException as error:
        print('UI09_EXIT1 ' + str(error), flush=True)
        raise SystemExit(1)
    print('UI09_EXIT0', flush=True)
