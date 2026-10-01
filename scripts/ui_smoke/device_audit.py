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
import hashlib
import json
import os
import pwd
import runpy
from pathlib import Path
import stat
import subprocess
import sys
import xml.etree.ElementTree as ET

ROOT = Path('/opt/var/lib/consent-ui-native-09')
APP = Path('/opt/usr/globalapps/org.tizen.consentui')


def record(path):
    if not os.path.lexists(path):
        return {'absent': True}
    info = path.lstat()
    output = {'uid': info.st_uid, 'gid': info.st_gid,
              'mode': stat.S_IMODE(info.st_mode), 'inode': info.st_ino,
              'dev': info.st_dev, 'nlink': info.st_nlink,
              'mtime_ns': info.st_mtime_ns}
    try:
        output['smack'] = os.getxattr(path, 'security.SMACK64',
                                      follow_symlinks=False).decode()
    except OSError:
        output['smack'] = None
    if stat.S_ISLNK(info.st_mode):
        output['link'] = os.readlink(path)
    elif stat.S_ISREG(info.st_mode):
        descriptor = os.open(path, os.O_RDONLY | os.O_NOFOLLOW | os.O_CLOEXEC)
        digest = hashlib.sha256()
        try:
            if os.fstat(descriptor) != info:
                raise RuntimeError('fingerprint file changed during open')
            while True:
                part = os.read(descriptor, 65536)
                if not part:
                    break
                digest.update(part)
        finally:
            os.close(descriptor)
        output['sha256'] = digest.hexdigest()
    return output


def inventory(root):
    records = {str(root): record(root)}
    if root.is_dir() and not root.is_symlink():
        for directory, children, files in os.walk(root, followlinks=False):
            for name in children + files:
                path = Path(directory) / name
                records[str(path)] = record(path)
    return records


def chrome():
    package = 'org.tizen.taskbar'
    owner = pwd.getpwnam('tizenglobalapp').pw_uid
    process_owner = pwd.getpwnam('owner').pw_uid
    base = Path('/usr/apps') / package
    required = {'Version': '2.0.1', 'mainappid': package, 'Removable': '0',
                'Preload': '1', 'Readonly': '1', 'system': '1',
                'root_path': str(base), 'Type': 'tpk'}
    probe = subprocess.run(['pkginfo', '--pkg', package], check=True,
        stdout=subprocess.PIPE, stderr=subprocess.PIPE, timeout=5)
    if len(probe.stdout) > 16384 or len(probe.stderr) > 16384:
        raise RuntimeError('chrome package metadata exceeded bound')
    fields = {}
    for line in probe.stdout.decode().splitlines():
        key, separator, value = line.partition(':')
        if key.strip() in required:
            if not separator or key.strip() in fields:
                raise RuntimeError('duplicate chrome package field')
            fields[key.strip()] = value.strip()
    if any(fields.get(k) != v for k, v in required.items()):
        raise RuntimeError('unsupported trusted chrome package')
    identities = {}
    root = os.open('/', os.O_RDONLY | os.O_DIRECTORY | os.O_NOFOLLOW)
    try:
        for part in ('usr', 'apps', package):
            child = os.open(part, os.O_RDONLY | os.O_DIRECTORY |
                            os.O_NOFOLLOW, dir_fd=root)
            info = os.fstat(child)
            allowed = (info.st_uid in (0, owner) and
                       not stat.S_IMODE(info.st_mode) & 0o022)
            if part == 'apps':
                allowed = (info.st_uid == 0 and info.st_gid == 0 and
                           stat.S_IMODE(info.st_mode) in (0o755, 0o775))
            if not allowed:
                os.close(child)
                raise RuntimeError('unprotected trusted chrome parent')
            os.close(root)
            root = child
        identities['root'] = {'dev': info.st_dev, 'inode': info.st_ino,
                              'uid': info.st_uid, 'mode': info.st_mode,
                              'smack': os.getxattr(
                                  root, 'security.SMACK64').decode()}

        def read_leaf(directory, name, maximum):
            fd = os.open(name, os.O_RDONLY | os.O_NOFOLLOW, dir_fd=directory)
            try:
                before = os.fstat(fd)
                if (not stat.S_ISREG(before.st_mode) or before.st_uid != owner
                        or before.st_nlink != 1 or before.st_mode & 0o022
                        or before.st_size > maximum):
                    raise RuntimeError('unprotected trusted chrome leaf')
                data = b''
                while True:
                    part = os.read(fd, min(65536, maximum + 1 - len(data)))
                    if not part:
                        break
                    data += part
                    if len(data) > maximum:
                        raise RuntimeError('chrome leaf exceeded bound')
                after = os.fstat(fd)
                keys = ('st_dev', 'st_ino', 'st_uid', 'st_gid', 'st_mode',
                        'st_nlink', 'st_size', 'st_mtime_ns', 'st_ctime_ns')
                named = os.stat(name, dir_fd=directory, follow_symlinks=False)
                if any(getattr(v, k) != getattr(before, k)
                       for v in (after, named) for k in keys):
                    raise RuntimeError('chrome leaf changed during read')
                return data, {'dev': before.st_dev, 'inode': before.st_ino,
                    'uid': before.st_uid, 'gid': before.st_gid,
                    'mode': stat.S_IMODE(before.st_mode),
                    'smack': os.getxattr(fd, 'security.SMACK64').decode(),
                    'sha256': hashlib.sha256(data).hexdigest()}
            finally:
                os.close(fd)

        manifest, identities['manifest'] = read_leaf(
            root, 'tizen-manifest.xml', 16384)
        expected = ('079aaa34d06742535810198bf53407d9952b1c65671b6b11835b8'
                    '43f0abfbf02')
        if identities['manifest']['sha256'] != expected:
            raise RuntimeError('unsupported chrome manifest')
        tree = ET.fromstring(manifest)
        app = tree.find('{http://tizen.org/ns/packages}ui-application')
        if (tree.get('package') != package or tree.get('version') != '2.0.1'
                or app is None or app.get('appid') != package
                or app.get('exec') != 'TaskBar.dll'):
            raise RuntimeError('trusted chrome application mismatch')
        directory = os.open('bin', os.O_RDONLY | os.O_DIRECTORY |
                            os.O_NOFOLLOW, dir_fd=root)
        try:
            info = os.fstat(directory)
            if info.st_uid != owner or info.st_mode & 0o022:
                raise RuntimeError('unprotected chrome executable parent')
            _, identities['dll'] = read_leaf(directory, 'TaskBar.dll', 4194304)
        finally:
            os.close(directory)
    finally:
        os.close(root)
    dll = str(base / 'bin/TaskBar.dll')
    processes = []
    for entry in Path('/proc').iterdir():
        if not entry.name.isdecimal():
            continue
        try:
            executable = os.readlink(entry / 'exe')
            if executable != '/usr/bin/dotnet-hydra-loader':
                continue
            with (entry / 'cmdline').open('rb') as stream:
                command = stream.read(8193)
            if command.split(b'\0', 1)[0] != dll.encode():
                continue
            if len(command) > 8192 or entry.stat().st_uid != process_owner:
                raise RuntimeError('trusted chrome process mismatch')
            with (entry / 'maps').open() as stream:
                maps_text = stream.read(1048577)
            if len(maps_text) > 1048576:
                raise RuntimeError('chrome maps exceeded bound')
            identity = identities['dll']
            mapped = False
            for line in maps_text.splitlines():
                fields = line.split(maxsplit=5)
                if len(fields) != 6 or fields[5] != dll:
                    continue
                major, minor = (int(v, 16) for v in fields[3].split(':'))
                if (int(fields[4]) == identity['inode'] and
                        major == os.major(identity['dev']) and
                        minor == os.minor(identity['dev'])):
                    mapped = True
            if not mapped:
                raise RuntimeError('chrome DLL mapping identity mismatch')
            processes.append(int(entry.name))
        except (FileNotFoundError, ProcessLookupError):
            continue
    if len(processes) != 1:
        raise RuntimeError('trusted chrome process unavailable/ambiguous')
    return {'package': package, 'fields': required,
            'identities': identities, 'pid': processes[0],
            'process_owner': process_owner}


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


def fingerprint():
    names = ('consent', 'consentd', 'consent-devel', 'consent-tests',
        'consent-poc')
    listing = subprocess.check_output(['rpm', '-ql', *names], text=True)
    paths = sorted(set(listing.splitlines()))
    output = {'package_files': {path: record(Path(path)) for path in paths},
              'protected_trees': {}, 'runtime_parents': {}, 'application': {},
              'units': {}}
    for root in ('/etc/consent', '/etc/consent-poc', '/opt/var/lib/consentd',
                 '/opt/var/lib/consent-authority',
                 '/opt/var/lib/consent-poc-state',
                 '/opt/var/lib/consent-poc-authority'):
        output['protected_trees'].update(inventory(Path(root)))
    for root in ('/opt/var/lib/consent-poc-runtime',
                 '/opt/var/lib/consent-feature-runtime'):
        output['runtime_parents'][root] = record(Path(root))
    output['application'] = inventory(APP)
    for unit in ('consentd.service', 'consentd-poc.service',
                 'consent-feature-poc.service'):
        output['units'][unit] = subprocess.check_output(['systemctl', 'show',
            unit, '-p', 'MainPID', '-p', 'ActiveState', '-p', 'Result',
            '-p', 'FragmentPath'], text=True)
    return output


def gui_pids():
    result = []
    for child in Path('/proc').iterdir():
        if not child.name.isdecimal():
            continue
        try:
            executable = os.readlink(child / 'exe')
            if executable != '/usr/bin/dotnet-hydra-loader':
                continue
            mappings = (child / 'maps').read_text()
            if str(APP) in mappings:
                result.append(int(child.name))
        except (FileNotFoundError, ProcessLookupError):
            continue
    return result


def maps():
    evidence = {}
    expected = json.loads((ROOT / 'payload.json').read_text())['files']
    ui = gui_pids()
    if len(ui) != 1:
        raise RuntimeError('expected exactly one observed native UI process')
    entries = [('ui', ui[0], 'bundle')]
    for role in ('daemon', 'coordinator'):
        unit = ('consentd-ui09.service' if role == 'daemon' else
                'consent-feature-ui09.service')
        pid = int(subprocess.check_output(['systemctl', 'show', '-p',
            'MainPID', '--value', unit], text=True).strip())
        if pid <= 0:
            raise RuntimeError('owned service missing')
        entries.append((role, pid, 'namespace'))
        if role == 'coordinator':
            # Child ownership comes from the actual kernel parent relationship.
            for path in Path('/proc').iterdir():
                if not path.name.isdecimal():
                    continue
                try:
                    status = (path / 'status').read_text().splitlines()
                    parent = next(line.split()[1] for line in status
                                  if line.startswith('PPid:'))
                    executable = os.readlink(path / 'exe')
                    if int(parent) == pid and executable in (
                        '/usr/libexec/consent/poc/consent-mock-ce',
                        '/usr/libexec/consent/poc/consent-mock-cm',
                        '/usr/libexec/consent/poc/consent-mock-holder'):
                        entries.append((Path(executable).name, int(path.name),
                                        'namespace'))
                except (FileNotFoundError, ProcessLookupError):
                    continue
    for role, pid, kind in entries:
        process = Path('/proc') / str(pid)
        mappings = (process / 'maps').read_text()
        status = (process / 'status').read_text().splitlines()
        identities = {line.split(':')[0]: line.split(':', 1)[1].strip()
                      for line in status if line.startswith(('Uid:', 'Gid:'))}
        row = {'pid': pid, 'maps': mappings, 'kernel_ids': identities,
               'exe': os.readlink(process / 'exe'),
               'mnt_namespace': os.readlink(process / 'ns/mnt'),
               'smack': (process / 'attr/current').read_text().strip(),
               'libraries': {}}
        if kind == 'namespace':
            if row['mnt_namespace'] == os.readlink('/proc/1/ns/mnt'):
                raise RuntimeError('actor shares global mount namespace')
        if role == 'daemon':
            binary = ROOT / 'tests/consentd-ui09'
            actual_executable = record(process / 'root' /
                                       str(binary).lstrip('/'))
            staged_executable = record(binary)
            if (actual_executable.get('sha256') !=
                    expected['tests/consentd-ui09'] or
                    actual_executable['dev'] != staged_executable['dev'] or
                    actual_executable['inode'] != staged_executable['inode']):
                raise RuntimeError(
                    'daemon executable differs from staged helper')
            row['daemon_executable'] = actual_executable
        names = ('libconsent-poc.so.0', 'libconsent-feature-poc.so.0')
        if kind == 'namespace':
            names = names[:1] if role != 'daemon' else ()
        for name in names:
            real = name if kind == 'bundle' else name + '.1.0'
            absolute = APP / 'bin' / real if kind == 'bundle' else (
                Path('/usr/lib64') / real)
            target = process / 'root' / str(absolute).lstrip('/')
            actual = record(target)
            key = 'libraries/' + name + '.1.0'
            if actual.get('sha256') != expected[key]:
                raise RuntimeError(
                    'mapped native ELF does not match signed TPK')
            lines = [line for line in mappings.splitlines()
                     if line.endswith(str(absolute))]
            expected_device = (os.major(actual['dev']), os.minor(actual['dev']))
            if not lines or not all(
                    int(line.split()[4]) == actual['inode'] and
                    tuple(int(value,
                        16) for value in line.split()[3].split(':'))
                    == expected_device for line in lines):
                raise RuntimeError('mapping inode/path mismatch')
            if kind == 'namespace':
                staged = record(ROOT / key)
                if (actual['inode'], actual['dev']) != (
                        staged['inode'], staged['dev']):
                    raise RuntimeError('namespace did not map staged inode')
            row['libraries'][name] = actual
        evidence[role] = row
    if not all(role in evidence for role in ('consent-mock-cm',
                                              'consent-mock-ce')):
        raise RuntimeError('both actual CM and CE worker maps are required')
    return evidence


def ownership():
    module = runpy.run_path(str(ROOT / 'tests/emulator-ui-native.py'))
    root = record(ROOT)
    result = {'root': root, 'acquired': False, 'units': {}}
    if not os.path.lexists(ROOT / 'roles.conf'):
        return result
    manifest = json.loads(module['read'](ROOT / 'payload.json'))
    proof_path = ROOT / 'acquired.json'
    if not os.path.lexists(proof_path):
        return result
    proof = json.loads(module['read'](proof_path))
    if proof.get('invocation') != manifest.get('invocation') or not (
            isinstance(proof.get('invocation'), str)):
        raise RuntimeError('foreign invocation acquisition proof')
    if proof.get('root_identity') != manifest.get('root_identity'):
        raise RuntimeError('foreign invocation root proof')
    for key, value in proof['root_identity'].items():
        if root[key] != value:
            raise RuntimeError('acquired root changed')
    result['invocation'] = proof['invocation']
    module['protected'](ROOT / 'roles.conf')
    require_mode = stat.S_IMODE((ROOT / 'roles.conf').lstat().st_mode)
    if require_mode != 0o640:
        raise RuntimeError('foreign acquired role configuration')
    if record(ROOT / 'roles.conf')['sha256'] != proof['roles_sha256']:
        raise RuntimeError('acquired configuration changed')
    managed = ROOT / 'managed.json'
    if managed.exists():
        ledger = json.loads(module['read'](managed))
        texts = module['managed_units']()
        expected = {unit: hashlib.sha256(text.encode()).hexdigest()
                    for unit, text in texts.items()}
        if ledger.get('units') != expected or proof.get('units') != expected:
            raise RuntimeError('foreign managed acquisition ledger')
        for unit, text in texts.items():
            path = Path('/run/systemd/system') / unit
            if os.path.lexists(path):
                data = module['read'](path)
                if data != text.encode():
                    raise RuntimeError('acquired unit content changed')
                result['units'][unit] = record(path)
    else:
        raise RuntimeError('completed acquisition ledger missing')
    created = proof.get('created_runtime')
    if not isinstance(created,
        dict) or created != ledger.get('created_runtime'):
        raise RuntimeError('runtime acquisition ledger mismatch')
    allowed = {'/opt/var/lib/consent-poc-runtime',
               '/opt/var/lib/consent-feature-runtime'}
    for name, identity in created.items():
        if name not in allowed or set(identity) != {
                'dev', 'inode', 'uid', 'gid', 'mode', 'smack'}:
            raise RuntimeError('foreign acquired runtime directory')
        current = record(Path(name))
        if any(current.get(key) != value for key, value in identity.items()):
            raise RuntimeError('mkdir-time runtime identity changed')
    result['created_runtime'] = created
    result['acquired'] = True
    return result


def generated():
    ownership_proof = ownership()
    if not ownership_proof['acquired']:
        raise RuntimeError('no invocation acquisition for generated inventory')
    files = {}
    for name in ('roles.conf', 'generation', 'managed.json', 'actors.json',
                 'acquired.json'):
        path = ROOT / name
        if os.path.lexists(path):
            metadata = record(path)
            if metadata['uid'] != 0 or metadata['mode'] & 0o022:
                raise RuntimeError('foreign generated configuration')
            files[name] = metadata['sha256']
    return files


if __name__ == '__main__':
    if len(sys.argv) != 2 or sys.argv[1] not in ('fingerprint', 'maps',
        'gui-pids', 'ownership', 'generated', 'chrome'):
        raise SystemExit(2)
    result = (fingerprint() if sys.argv[1] == 'fingerprint' else
              maps() if sys.argv[1] == 'maps' else
              ownership() if sys.argv[1] == 'ownership' else
              generated() if sys.argv[1] == 'generated' else
              chrome() if sys.argv[1] == 'chrome' else gui_pids())
    print(json.dumps(result, sort_keys=True))
