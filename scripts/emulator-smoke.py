#!/usr/bin/python3
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
"""Explicit isolated development-emulator smoke, never production integration."""
import argparse
import hashlib
import json
import os
from pathlib import Path
import pwd
import random
import select
import shutil
import sqlite3
import stat
import subprocess
import sys
import time

ROOT = Path('/tmp/consent-smoke')
RUNTIME = Path('/opt/var/lib/consent-smoke-runtime')
STATE = Path('/opt/var/lib/consent-smoke-state')
AUTHORITY = Path('/opt/var/lib/consent-smoke-authority')
TOOLS = Path('/usr/libexec/consent/smoke')
MARKER = 'CONSENT-SMOKE-01 revision 1\n'
ACTORS = []
CHILDREN = []
MANAGED = False


def run(*args, expected=0):
    print('COMMAND', json.dumps(args), flush=True)
    result = subprocess.run(args, text=True, stdout=subprocess.PIPE,
                            stderr=subprocess.STDOUT, timeout=45)
    print(result.stdout, end='', flush=True)
    print('EXIT', result.returncode, flush=True)
    if result.returncode != expected:
        raise RuntimeError(f'expected exit {expected}, got {result.returncode}')
    return result.stdout.strip()


def stop():
    run('systemctl', 'stop', 'consentd-smoke.socket', 'consentd-smoke.service')
    assert run('systemctl', 'show', 'consentd-smoke.service', '-p', 'MainPID',
               '--value') == '0'


def start():
    run('systemctl', 'start', 'consentd-smoke.socket', 'consentd-smoke.service')


def protected_dir(path, owner):
    # Verify each ancestor without following symbolic links.
    for parent in list(path.parents)[::-1] + [path]:
        st = parent.lstat()
        assert stat.S_ISDIR(st.st_mode) and not stat.S_ISLNK(st.st_mode)
        assert st.st_uid == (owner if parent == path else 0)
        assert not st.st_mode & 0o022 or (parent == Path('/tmp') and st.st_mode & stat.S_ISVTX)


def validate_fixture(allow_absent_state=False):
    protected_dir(ROOT, 0)
    protected_dir(RUNTIME, 0)
    if STATE.exists():
        protected_dir(STATE, pwd.getpwnam('security_fw').pw_uid)
    else:
        assert allow_absent_state and not STATE.is_symlink()
    protected_dir(AUTHORITY, 0)
    marker = ROOT / 'owner.marker'
    st = marker.lstat()
    assert stat.S_ISREG(st.st_mode) and st.st_uid == 0 and st.st_nlink == 1
    assert not st.st_mode & 0o077 and marker.read_text() == MARKER


def production_fingerprint():
    # Does not open live consent.db: inode/size/mtime only, no SQLite access.
    result = {}
    for root in ('/opt/var/lib/consentd', '/opt/var/lib/consent-authority',
                 '/opt/var/lib/consent-poc-state'):
        path = Path(root)
        if path.exists():
            result[root] = [(str(p), p.lstat().st_ino, p.lstat().st_size,
                             p.lstat().st_mtime_ns)
                            for p in sorted(path.rglob('*'))]
    return result


class Actor:
    def __init__(self, role, definition, resource=None):
        self.tool = role.startswith('tool-')
        args = [str(TOOLS / f'consent-smoke-{role}'), definition]
        if not self.tool:
            args.append(str(resource))
        self.process = subprocess.Popen(
            args,
            stdin=subprocess.PIPE, stdout=subprocess.PIPE,
            stderr=subprocess.STDOUT, bufsize=0)
        CHILDREN.append(self.process)
        self.buffer = b""
        self.lines = []
        self.until('READY')
        ACTORS.append(self)

    def until(self, marker):
        # Byte reads and select bound even partial-line responses.
        deadline = time.monotonic() + 12
        lines = []
        while time.monotonic() < deadline:
            while b'\n' not in self.buffer:
                remaining = deadline - time.monotonic()
                if remaining <= 0 or not select.select(
                        [self.process.stdout], [], [], remaining)[0]:
                    raise RuntimeError('actor response timeout')
                chunk = os.read(self.process.stdout.fileno(), 4096)
                if not chunk:
                    raise RuntimeError(f'actor exit {self.process.poll()}')
                self.buffer += chunk
            raw, self.buffer = self.buffer.split(b'\n', 1)
            line = raw.decode() + '\n'
            print(f'ACTOR pid={self.process.pid}', line, end='', flush=True)
            lines.append(line)
            if marker in line:
                self.lines.extend(lines)
                return ''.join(lines)
        raise RuntimeError('actor timeout')

    def check(self, mode, operation, decision):
        self.process.stdin.write(f'{mode} {operation} {decision}\n'.encode())
        self.process.stdin.flush()
        terminal = 'wrong-role' if mode in ('register', 'request') else ('PROTECTED_TOOL' if self.tool else 'PROTECTED_READ')
        if decision in ('DISCONNECTED', 'STORAGE_ERROR', 'PERMISSION_DENIED'):
            terminal = 'PASS blocked error='
        if mode == 'reconnect':
            terminal = 'RECONNECTED'
        return self.until(terminal)

    def reconnect(self):
        self.check('authorize', 'dead-handle', 'DISCONNECTED')
        self.check('reconnect', 'explicit-new-authentication', 'RECONNECTED')

    def close(self):
        if self.process.poll() is None:
            self.process.stdin.close()
        status = self.process.wait(timeout=10)
        print('ACTOR_EXIT', self.process.pid, status, flush=True)
        assert status == 0


def approve(definition, mode, operation):
    args = [str(TOOLS / 'consent-smoke-argo'), definition, operation]
    print('COMMAND', json.dumps(args), flush=True)
    process = subprocess.Popen(args, stdout=subprocess.PIPE,
                               stderr=subprocess.STDOUT, text=True)
    CHILDREN.append(process)
    if not select.select([process.stdout], [], [], 12)[0]:
        raise RuntimeError('argo readiness timeout')
    raw = b''
    deadline = time.monotonic() + 12
    while b'\n' not in raw:
        remaining = deadline - time.monotonic()
        if remaining <= 0 or not select.select(
                [process.stdout], [], [], remaining)[0]:
            raise RuntimeError('argo bounded request-ID timeout')
        chunk = os.read(process.stdout.fileno(), 1)
        if not chunk:
            raise RuntimeError('argo exited before request ID')
        raw += chunk
    line = raw.decode()
    print(line, end='', flush=True)
    assert line.startswith('REQUEST_ID '), line
    request = line.strip().split()[1]
    run(str(TOOLS / 'consent-smoke-ui'), '--auto-approve-smoke', request, mode)
    output = process.communicate(timeout=25)[0]
    print(output, end='', flush=True)
    print('ARGO_EXIT', process.returncode, flush=True)
    assert process.returncode == 0


def setup(tools=False):
    global MANAGED
    for suffix in ('service', 'socket'):
        unit = Path('/run/systemd/system') / f'consentd-smoke.{suffix}'
        assert not unit.exists() and not unit.is_symlink()
    assert run('systemctl', 'show', 'consentd-smoke.service',
               '-p', 'LoadState', '--value') == 'not-found'
    assert run('systemctl', 'show', 'consentd-smoke.socket',
               '-p', 'LoadState', '--value') == 'not-found'
    for path in (ROOT, RUNTIME, STATE, AUTHORITY):
        assert not path.exists() and not path.is_symlink(), \
            f'fixture exists: {path}; use explicit --cleanup first'
    for path in (ROOT, RUNTIME, STATE, AUTHORITY):
        protected_dir(path.parent, 0)
    ROOT.mkdir(mode=0o750)
    group = pwd.getpwnam('security_fw').pw_gid
    os.chown(ROOT, 0, group)
    RUNTIME.mkdir(mode=0o750)
    os.chown(RUNTIME, 0, group)
    marker = ROOT / 'owner.marker'
    marker.write_text(MARKER)
    marker.chmod(0o600)
    run(str(TOOLS / 'consent-smoke-storage-prepare'), '--first-install')
    roles = '[policy]\nversion=1\n'
    for role in ('installer', 'cm', 'ce', 'argo', 'ui', 'admin'):
        roles += (f'[identity smoke-{role}]\nuid=0\n'
                  f'executable={TOOLS}/consent-smoke-{role}\nlabel=System\n'
                  f'roles={role};\nsubjects=smoke.subject;\n'
                  'profiles=smoke.profile;\n'
                  f'enforcers={role if role in ("cm", "ce") else "cm;ce"};\n'
                  'packages=smoke.package;\n')
    if tools:
        for role in ('cm', 'ce'):
            roles += (f'[identity smoke-tool-{role}]\nuid=0\n'
                      f'executable={TOOLS}/consent-smoke-tool-{role}\n'
                      f'label=System\nroles={role};\n'
                      'subjects=smoke.subject;\nprofiles=smoke.profile;\n'
                      f'enforcers={role};\npackages=smoke.package;\n')
    policy = ROOT / 'roles.conf'
    policy.write_text(roles)
    policy.chmod(0o640)
    os.chown(policy, 0, group)
    units = Path('/run/systemd/system')
    for suffix in ('service', 'socket'):
        path = units / f'consentd-smoke.{suffix}'
        assert not path.exists() and not path.is_symlink()
    (units / 'consentd-smoke.socket').write_text(
        '[Unit]\nDescription=Isolated developer consent smoke\n'
        '[Socket]\nListenStream=/opt/var/lib/consent-smoke-runtime/consent.sock\n'
        'SocketMode=0600\nSmackLabel=System\n'
        'Service=consentd-smoke.service\nRemoveOnStop=yes\n')
    (units / 'consentd-smoke.service').write_text(
        '[Unit]\nRequires=consentd-smoke.socket\n'
        'After=consentd-smoke.socket\n[Service]\nType=notify\n'
        'User=security_fw\nGroup=security_fw\n'
        'AmbientCapabilities=CAP_SYS_PTRACE\n'
        'CapabilityBoundingSet=CAP_SYS_PTRACE\nNoNewPrivileges=yes\n'
        f'ExecStartPre=+{TOOLS}/consent-smoke-storage-prepare\n'
        f'ExecStartPost=+{TOOLS}/consent-smoke-storage-prepare --complete\n'
        f'ExecStart={TOOLS}/consentd-smoke\nSmackProcessLabel=System\n'
        'UMask=0077\nTimeoutStartSec=15\nTimeoutStopSec=10\n')
    record_units()
    MANAGED = True
    run('systemctl', 'daemon-reload')
    start()


def authority(*args):
    output = run('systemd-run', '--quiet', '--wait', '--pipe',
                 '-p', 'SmackProcessLabel=System::Privileged',
                 str(TOOLS / 'consent-smoke-installation-authority'), *args)
    return output.splitlines()[-1]


def catalog():
    package = ROOT / 'catalog-package'
    (package / 'res/skills/smoke').mkdir(parents=True)
    resource = package / 'res/skills/smoke/SKILL.md'
    resource.write_text('# Protected developer smoke skill\n')
    (package / 'skill.json').write_text(json.dumps(dict(
        version=1, key='consent-smoke', name='Smoke protected skill',
        desc='Protected smoke resource', resource='res/skills/smoke')))
    manifest = dict(version=1, operation='smoke-install', owner='smoke.package',
                    mode='replace', root=str(package), metadata=[dict(
        key='http://tizen.org/metadata/capability/skill', value='skill.json')])
    path = package / 'manifest.json'
    path.write_text(json.dumps(manifest))
    tool = '/usr/libexec/capmgr/capmgr-package-tool'
    database = ROOT / 'catalog.db'
    staged = json.loads(run(tool, '--offline', str(database), 'stage', str(path)))
    assert staged['state'] == 'pending' and staged['revision'] == 0
    for _ in range(2):
        final = json.loads(run(tool, '--offline', str(database), 'finalize',
                               'smoke-install', 'success'))
        assert final['state'] == 'success' and final['revision'] == 1
    with sqlite3.connect(f'file:{database}?mode=ro', uri=True) as db:
        rows = db.execute('SELECT id,owner FROM capability').fetchall()
        assert rows == [('skill:consent-smoke', 'smoke.package')], rows
    print('catalog_actual/PASS skill:consent-smoke owner=smoke.package')
    print('BINDING separate developer mapping skill:consent-smoke -> '
          'smoke.cm.read level=1; parser has no consent metadata')
    return resource


def record_units():
    digests = {}
    for suffix in ('socket', 'service'):
        path = Path('/run/systemd/system') / f'consentd-smoke.{suffix}'
        digests[suffix] = hashlib.sha256(path.read_bytes()).hexdigest()
    target = ROOT / 'units.json'
    target.write_text(json.dumps(digests))
    target.chmod(0o600)


def validate_units():
    metadata = ROOT / 'units.json'
    st = metadata.lstat()
    assert stat.S_ISREG(st.st_mode) and st.st_uid == 0 and st.st_nlink == 1
    assert not st.st_mode & 0o077
    digests = json.loads(metadata.read_text())
    for suffix in ('socket', 'service'):
        path = Path('/run/systemd/system') / f'consentd-smoke.{suffix}'
        st = path.lstat()
        assert stat.S_ISREG(st.st_mode) and st.st_uid == 0
        assert st.st_nlink == 1 and not st.st_mode & 0o022
        assert hashlib.sha256(path.read_bytes()).hexdigest() == digests[suffix]
        assert run('systemctl', 'show', f'consentd-smoke.{suffix}',
                   '-p', 'FragmentPath', '--value') == str(path)


def validate_database(path):
    # Hold the protected parent and bind the exact no-follow regular inode.
    with_fd = os.open(STATE, os.O_RDONLY | os.O_DIRECTORY | os.O_NOFOLLOW)
    try:
        fd = os.open(path.name, os.O_RDONLY | os.O_NOFOLLOW, dir_fd=with_fd)
        try:
            st = os.fstat(fd)
            named = os.stat(path.name, dir_fd=with_fd, follow_symlinks=False)
            assert stat.S_ISREG(st.st_mode) and st.st_nlink == 1
            assert st.st_uid == pwd.getpwnam('security_fw').pw_uid
            assert not st.st_mode & 0o077
            assert (st.st_dev, st.st_ino) == (named.st_dev, named.st_ino)
        finally:
            os.close(fd)
    finally:
        os.close(with_fd)


def mutate_database(path, corrupt=False):
    validate_fixture()
    parent = os.open(STATE, os.O_RDONLY | os.O_DIRECTORY | os.O_NOFOLLOW)
    fd = -1
    try:
        fd = os.open(path.name, (os.O_RDWR if corrupt else os.O_RDONLY) |
                     os.O_NOFOLLOW, dir_fd=parent)
        held = os.fstat(fd)
        named = os.stat(path.name, dir_fd=parent, follow_symlinks=False)
        assert stat.S_ISREG(held.st_mode) and held.st_nlink == 1
        assert held.st_uid == pwd.getpwnam('security_fw').pw_uid
        assert not held.st_mode & 0o077
        assert (held.st_dev, held.st_ino) == (named.st_dev, named.st_ino)
        if corrupt:
            os.ftruncate(fd, 0)
            data = b'deliberately corrupt isolated smoke database\n'
            assert os.write(fd, data) == len(data)
            os.fsync(fd)
        else:
            os.unlink(path.name, dir_fd=parent)
        os.fsync(parent)
    finally:
        if fd >= 0:
            os.close(fd)
        os.close(parent)


def inspect_recovery(definitions=5):
    with sqlite3.connect(f'file:{STATE}/consent.db?mode=ro', uri=True) as db:
        assert db.execute('PRAGMA integrity_check').fetchone() == ('ok',)
        assert db.execute('PRAGMA user_version').fetchone() == (2,)
        assert db.execute('SELECT count(*) FROM definitions WHERE active=1').fetchone() == (definitions,)
        assert db.execute('SELECT count(*) FROM grants').fetchone() == (0,)
        assert db.execute("SELECT value FROM meta WHERE key='cleanup_unknown'").fetchone()[0] == '1'
    print(f'PASS recovered integrity=ok schema=2 definitions={definitions} '
          'grants=0 cleanup_unknown=1')


def tools_catalog():
    package = TOOLS / 'tool-package'
    descriptor = json.loads((package / 'cli.json').read_text())
    assert descriptor['key'] == 'smoke-tool'
    assert descriptor['executable'] == 'bin/consent-smoke-tool'
    provider = package / descriptor['executable']
    info = provider.lstat()
    assert stat.S_ISREG(info.st_mode) and info.st_uid == 0
    assert info.st_nlink == 1 and not info.st_mode & 0o022
    path = ROOT / 'tool-manifest.json'
    path.write_text(json.dumps(dict(
        version=1, operation='tool-install', owner='smoke.package',
        mode='replace', root=str(package), metadata=[dict(
            key='http://tizen.org/metadata/capability/cli', value='cli.json')])))
    parser = '/usr/libexec/capmgr/capmgr-package-tool'
    database = ROOT / 'catalog-tool.db'
    staged = json.loads(run(parser, '--offline', str(database),
                            'stage', str(path)))
    assert staged['state'] == 'pending' and staged['revision'] == 0
    for _ in range(2):
        final = json.loads(run(parser, '--offline', str(database),
                              'finalize', 'tool-install', 'success'))
        assert final['state'] == 'success' and final['revision'] == 1
    with sqlite3.connect(f'file:{database}?mode=ro', uri=True) as db:
        rows = db.execute('SELECT id,owner,executable FROM capability').fetchall()
        assert rows == [('cli:smoke-tool', 'smoke.package', str(provider))], rows
    print('catalog_actual/PASS cli:smoke-tool owner=smoke.package '
          f'executable={provider}')
    print('BINDING separate developer mapping; parser has no consent metadata')


def tools_scenarios(args, generation):
    tools_catalog()
    definitions = [(f'smoke.cm.tool.{record}', 'cm', 1, record)
                   for record in ('summary', 'fail', 'timeout', 'malformed',
                                  'stderr', 'nonzero', 'conflict', 'nul')]
    definitions += [(f'smoke.ce.tool.level{i}', 'ce', i, f'level{i}')
                    for i in range(4)]
    metadata = {definition: dict(definition=definition, enforcer=role,
                                 level=level, record=record)
                for definition, role, level, record in definitions}
    path = ROOT / 'tool-metadata.json'
    path.write_text(json.dumps(metadata))
    path.chmod(0o600)
    context = ROOT / 'tool-context'
    context.mkdir(mode=0o700)
    for i in range(4):
        data = context / f'level{i}.txt'
        data.write_text(f'synthetic context summary level{i}\n')
        data.chmod(0o600)
    for definition, role, level, _ in definitions:
        run(str(TOOLS / 'consent-smoke-installer'), generation, definition,
            role, str(level), 'register-' + definition)
    # Invalid record/level cannot be chosen as a caller assertion.
    run(str(TOOLS / 'consent-smoke-tool-ce'), 'smoke.ce.tool.level4', expected=2)
    run(str(TOOLS / 'consent-smoke-installer'), generation, 'smoke.invalid.4',
        'ce', '4', 'invalid-tool-level', '--probe-invalid')
    run(str(TOOLS / 'consent-smoke-installer'), generation, 'smoke.invalid.3',
        'ce', '3', 'invalid-tool-mode', '--probe-invalid')
    # Exercise real argument and trusted-metadata boundaries before admission.
    run(str(TOOLS / 'consent-smoke-tool-cm'), 'smoke.cm.tool.unknown', expected=2)
    run(str(TOOLS / 'consent-smoke-tool-ce'), 'smoke.ce.tool.level0',
        '--level=0', expected=2)
    metadata['smoke.ce.tool.level3']['level'] = 0
    path.write_text(json.dumps(metadata))
    rejected = run(str(TOOLS / 'consent-smoke-tool-ce'),
                   'smoke.ce.tool.level3', expected=1)
    assert 'ADMISSION' not in rejected
    metadata['smoke.ce.tool.level3']['level'] = 3
    path.write_text(json.dumps(metadata))
    provider = TOOLS / 'tool-package/bin/consent-smoke-tool'
    for name, arguments in (('unknown-tool', {'record': 'summary'}),
                            ('cli:smoke-tool', {'record': 'unknown'}),
                            ('cli:smoke-tool', {'record': 'summary', 'level': 0}),
                            ('cli:smoke-tool', {'record': 'summary', 'path': '/x'})):
        request = dict(jsonrpc='2.0', id='invalid-input', method='tools/call',
                       params=dict(name=name, arguments=arguments))
        response = json.loads(run(str(provider), '--json', json.dumps(request)))
        assert response['id'] == request['id']
        assert response['error']['code'] == -32602 and 'result' not in response
    print('PASS actual input/level boundaries rejected before gate admission')
    cm_definition, ce_definition = 'smoke.cm.tool.summary', 'smoke.ce.tool.level0'
    cm = Actor('tool-cm', cm_definition)
    ce = Actor('tool-ce', ce_definition)
    for actor in (cm, ce):
        assert 'count=0' in actor.check('authorize', 'before', 'CONSENT_REQUIRED')
        assert 'count=0' in actor.check('query', 'advisory', 'CONSENT_REQUIRED')
        actor.check('register', 'wrong-role', 'DENIED')
        actor.check('request', 'wrong-request', 'DENIED')
    for role, definition in (('cm', ce_definition), ('ce', cm_definition)):
        actor = Actor('tool-' + role, definition)
        actor.check('authorize', 'cross-tool', 'PERMISSION_DENIED')
        actor.close()
    for actor, definition in ((cm, cm_definition), (ce, ce_definition)):
        approve(definition, 'PERSISTENT', 'tool-approve-' + definition)
        assert 'state=succeeded count=1' in actor.check(
            'authorize', 'tool-approved', 'ALLOWED')
        for field in ('scope', 'purpose', 'recipient', 'operation'):
            assert 'blocked/advisory count=1' in actor.check(
                'probe_' + field, 'tuple-' + field, 'CONSENT_REQUIRED')
    stop()
    start()
    for actor in (cm, ce):
        actor.reconnect()
        actor.check('authorize', 'normal-tool-restart', 'ALLOWED')
    run(str(TOOLS / 'consent-smoke-admin'), cm_definition)
    cm.check('authorize', 'tool-revoked', 'CONSENT_REQUIRED')
    approve(cm_definition, 'ONCE', 'tool-once')
    consumed = cm.check('authorize', 'tool-once-consume', 'ALLOWED')
    retry = cm.check('authorize', 'tool-once-consume', 'ALLOWED')
    assert 'deduplicated state=succeeded' in retry
    assert consumed.split('count=')[-1].strip() == retry.split('count=')[-1].strip()
    cm.check('authorize', 'tool-once-next', 'CONSENT_REQUIRED')
    replacement = Actor('tool-cm', cm_definition)
    assert 'unknown prior execution blocked count=0' in replacement.check(
        'authorize', 'tool-once-consume', 'ALLOWED')
    replacement.close()
    for record, state in (('fail', 'native_error'), ('timeout', 'unknown'),
                          ('malformed', 'unknown'), ('stderr', 'succeeded'),
                          ('nonzero', 'succeeded'), ('conflict', 'unknown'),
                          ('nul', 'unknown')):
        definition = 'smoke.cm.tool.' + record
        actor = Actor('tool-cm', definition)
        assert 'count=0' in actor.check('authorize', record, 'CONSENT_REQUIRED')
        approve(definition, 'ONCE', 'approve-' + record)
        assert f'state={state} count=1' in actor.check('authorize', record, 'ALLOWED')
        assert f'deduplicated state={state} count=1' in actor.check(
            'authorize', record, 'ALLOWED')
        actor.check('authorize', record + '-new', 'CONSENT_REQUIRED')
        actor.close()
    for level in (1, 2, 3):
        definition = f'smoke.ce.tool.level{level}'
        actor = Actor('tool-ce', definition)
        assert 'count=0' in actor.check('authorize', 'before', 'CONSENT_REQUIRED')
        approve(definition, 'ONCE', f'tool-level{level}')
        assert 'state=succeeded count=1' in actor.check(
            'authorize', f'context-level{level}-once', 'ALLOWED')
        assert 'deduplicated state=succeeded count=1' in actor.check(
            'authorize', f'context-level{level}-once', 'ALLOWED')
        actor.check('authorize', f'context-level{level}-next', 'CONSENT_REQUIRED')
        actor.close()
    phases = ['running-delete', 'stopped-delete', 'corrupt']
    random.Random(args.seed).shuffle(phases)
    print('TOOLS_SEED', args.seed, 'ORDER', phases, flush=True)
    for phase in phases:
        for definition in (cm_definition, ce_definition):
            run(str(TOOLS / 'consent-smoke-admin'), definition)
            approve(definition, 'PERSISTENT', phase + '-persist-' + definition)
        old = cm.check('authorize', phase + '-before', 'ALLOWED')
        ce.check('authorize', phase + '-before-ce', 'ALLOWED')
        if phase != 'running-delete':
            stop()
        database = STATE / 'consent.db'
        mutate_database(database, corrupt=phase == 'corrupt')
        if phase != 'running-delete':
            start()
            cm.reconnect()
            ce.reconnect()
        lost = cm.check('authorize', phase + '-lost', 'CONSENT_REQUIRED')
        ce.check('authorize', phase + '-lost-ce', 'CONSENT_REQUIRED')
        assert old.split('epoch=')[1].split()[0] != lost.split('epoch=')[1].split()[0]
        stop()
        inspect_recovery(len(definitions))
        start()
        cm.reconnect()
        ce.reconnect()
        for actor, definition in ((cm, cm_definition), (ce, ce_definition)):
            approve(definition, 'PERSISTENT', phase + '-fresh-' + definition)
            assert 'state=succeeded' in actor.check(
                'authorize', phase + '-fresh', 'ALLOWED')
        print('PASS tool recovery', phase, 'same_actor_pids',
              cm.process.pid, ce.process.pid, flush=True)
    stop()
    mutate_database(STATE / 'definitions.registry')
    mutate_database(STATE / 'consent.db')
    result = subprocess.run(['systemctl', 'start', 'consentd-smoke.service'],
                            timeout=25)
    print('TOOL_REGISTRY_LOSS_START_EXIT', result.returncode, flush=True)
    assert result.returncode != 0
    for actor in (cm, ce):
        actor.check('authorize', 'tool-registry-loss', 'DISCONNECTED')
    print('PASS tool total registry loss safely blocked; no automatic reset')
    for actor in ACTORS:
        actor.close()


def main(args):
    assert os.getuid() == 0
    assert Path('/proc/self/attr/current').read_text().strip() == 'System'
    if args.cleanup:
        validate_fixture(allow_absent_state=True)
        validate_units()
        stop()
        for suffix in ('socket', 'service'):
            path = Path('/run/systemd/system') / f'consentd-smoke.{suffix}'
            assert path.is_file() and not path.is_symlink()
            assert 'consent-smoke' in path.read_text()
            path.unlink()
        for path in (STATE, AUTHORITY, RUNTIME, ROOT):
            if path.exists():
                shutil.rmtree(path)
        run('systemctl', 'daemon-reload')
        print('PASS explicit owned fixture cleanup')
        return
    before = production_fingerprint()
    if args.tools:
        for binary in ('consent-smoke-tool-cm', 'consent-smoke-tool-ce',
                       'tool-package/bin/consent-smoke-tool',
                       'tool-package/cli.json'):
            if not (TOOLS / binary).is_file():
                raise RuntimeError('--tools requires installed smoke tool mode: '
                                   + binary)
    setup(args.tools)
    validate_fixture()
    resource = None if args.tools else catalog()
    preflight = subprocess.run([str(TOOLS / 'consent-smoke-capmgr-preflight')],
                               timeout=10)
    print('PREFLIGHT_EXIT', preflight.returncode, flush=True)
    assert preflight.returncode in (0, 3), 'CM library missing or ABI failure'
    generation = authority('begin', 'smoke.package', 'begin-smoke', 'absent')
    authority('attach', 'smoke.package', 'smoke.app', 'attach-smoke', generation)
    authority('commit', 'smoke.package', 'commit-smoke', generation)
    if args.tools:
        tools_scenarios(args, generation)
        stop()
        assert before == production_fingerprint(), 'production/PoC state changed'
        print('PASS production and PoC state metadata unchanged')
        print('developer_tools/PASS; product_internal_integration/BLOCKED '
              '(CM public gate and CE adapter absent)')
        if args.require_product:
            raise RuntimeError('product public API integration required but blocked')
        return
    fixture_data = ROOT / 'context-data.txt'
    fixture_data.write_text('isolated context fixture\n')
    definitions = [('smoke.cm.read', 'cm', '1')]
    definitions += [(f'smoke.ce.level{i}', 'ce', str(i)) for i in range(4)]
    for definition, role, level in definitions:
        run(str(TOOLS / 'consent-smoke-installer'), generation, definition,
            role, level, 'register-' + definition)
    run(str(TOOLS / 'consent-smoke-installer'), generation, 'smoke.unknown',
        'ce', '4', 'unknown-level', expected=2)
    for level in ('3', '4'):
        run(str(TOOLS / 'consent-smoke-installer'), generation,
            'smoke.invalid.' + level, 'ce', level, 'invalid-' + level,
            '--probe-invalid')
    cm = Actor('cm', 'smoke.cm.read', resource)
    ce = Actor('ce', 'smoke.ce.level0', fixture_data)
    for actor in (cm, ce):
        text = actor.check('authorize', 'before-approval', 'CONSENT_REQUIRED')
        assert 'count=0' in text
        actor.check('register', 'wrong-role', 'DENIED')
        actor.check('request', 'wrong-request', 'DENIED')
    cross_cm = Actor('cm', 'smoke.ce.level0', fixture_data)
    cross_ce = Actor('ce', 'smoke.cm.read', resource)
    for actor in (cross_cm, cross_ce):
        actor.check('authorize', 'cross-enforcer', 'PERMISSION_DENIED')
        actor.close()
    approve('smoke.cm.read', 'PERSISTENT', 'approve-cm')
    allowed = cm.check('authorize', 'cm-approved', 'ALLOWED')
    assert 'count=1' in allowed
    approve('smoke.ce.level0', 'PERSISTENT', 'approve-level0')
    ce.check('authorize', 'level0-approved', 'ALLOWED')
    stop()
    start()
    cm.reconnect()
    ce.reconnect()
    cm.check('authorize', 'normal-restart', 'ALLOWED')
    ce.check('authorize', 'normal-restart-ce', 'ALLOWED')
    run(str(TOOLS / 'consent-smoke-admin'), 'smoke.cm.read')
    cm.check('authorize', 'revoked', 'CONSENT_REQUIRED')
    approve('smoke.cm.read', 'ONCE', 'approve-once')
    cm.check('query', 'advisory', 'ALLOWED')
    consumed = cm.check('authorize', 'once-consume', 'ALLOWED')
    retried = cm.check('authorize', 'once-consume', 'ALLOWED')
    assert 'deduplicated' in retried
    assert consumed.split('count=')[1].split()[0] == retried.split('count=')[1].split()[0]
    cm.check('authorize', 'once-next-operation', 'CONSENT_REQUIRED')
    replacement = Actor('cm', 'smoke.cm.read', resource)
    unknown = replacement.check('authorize', 'once-consume', 'ALLOWED')
    assert 'unknown prior execution blocked count=0' in unknown
    replacement.close()
    for level in (1, 2, 3):
        actor = Actor('ce', f'smoke.ce.level{level}', fixture_data)
        actor.check('authorize', f'level{level}-before', 'CONSENT_REQUIRED')
        approve(f'smoke.ce.level{level}', 'ONCE', f'approve-level{level}')
        actor.check('authorize', f'level{level}-once', 'ALLOWED')
        retried = actor.check('authorize', f'level{level}-once', 'ALLOWED')
        assert 'deduplicated' in retried
        actor.check('authorize', f'level{level}-next', 'CONSENT_REQUIRED')
        actor.close()
    phases = ['running-delete', 'stopped-delete', 'corrupt']
    random.Random(args.seed).shuffle(phases)
    print('SEED', args.seed, 'ORDER', phases, flush=True)
    for phase in phases:
        run(str(TOOLS / 'consent-smoke-admin'), 'smoke.cm.read')
        approve('smoke.cm.read', 'PERSISTENT', f'{phase}-persist')
        run(str(TOOLS / 'consent-smoke-admin'), 'smoke.ce.level0')
        approve('smoke.ce.level0', 'PERSISTENT', f'{phase}-persist-ce')
        old = cm.check('authorize', f'{phase}-before', 'ALLOWED')
        ce.check('authorize', f'{phase}-before-ce', 'ALLOWED')
        if phase != 'running-delete':
            stop()
        validate_fixture()
        database = STATE / 'consent.db'
        validate_database(database)
        mutate_database(database, corrupt=phase == 'corrupt')
        if phase != 'running-delete':
            start()
            cm.reconnect()
            ce.reconnect()
        # Same actor process/ledger across recovery; stopped daemons need new handles.
        text = cm.check('authorize', f'{phase}-after', 'CONSENT_REQUIRED')
        ce.check('authorize', f'{phase}-after-ce', 'CONSENT_REQUIRED')
        stop()
        inspect_recovery()
        start()
        cm.reconnect()
        ce.reconnect()
        old_epoch = old.split('epoch=')[1].split()[0]
        new_epoch = text.split('epoch=')[1].split()[0]
        assert old_epoch != new_epoch
        approve('smoke.cm.read', 'PERSISTENT', f'{phase}-fresh')
        cm.check('authorize', f'{phase}-fresh-authorize', 'ALLOWED')
        approve('smoke.ce.level0', 'PERSISTENT', f'{phase}-fresh-ce')
        ce.check('authorize', f'{phase}-fresh-authorize-ce', 'ALLOWED')
        print('PASS recovery', phase, 'same_actor_pid', cm.process.pid,
              'epoch', old_epoch, '->', new_epoch, flush=True)
        stop()
        with sqlite3.connect(f'file:{STATE}/consent.db?mode=ro', uri=True) as db:
            assert db.execute('PRAGMA integrity_check').fetchone() == ('ok',)
        start()
        cm.reconnect()
        ce.reconnect()
    stop()
    validate_fixture()
    validate_database(STATE / 'definitions.registry')
    mutate_database(STATE / 'definitions.registry')
    database = STATE / 'consent.db'
    validate_database(database)
    mutate_database(database)
    result = subprocess.run(['systemctl', 'start', 'consentd-smoke.service'],
                            timeout=25)
    print('REGISTRY_LOSS_START_EXIT', result.returncode, flush=True)
    assert result.returncode != 0
    cm.check('authorize', 'total-registry-loss', 'DISCONNECTED')
    print('PASS total registry loss safely blocked; no automatic reset')
    for actor in ACTORS:
        actor.close()
    stop()
    assert before == production_fingerprint(), 'production/PoC state changed'
    print('PASS production and PoC state metadata unchanged')
    print('developer_smoke/PASS; product_internal_integration/BLOCKED '
          '(CM public gate and CE adapter absent)')
    if args.require_product:
        raise RuntimeError('product public API integration required but blocked')


if __name__ == '__main__':
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--seed', type=int, default=20260930)
    parser.add_argument('--tools', action='store_true')
    parser.add_argument('--cleanup', action='store_true')
    parser.add_argument('--require-product', action='store_true')
    options = parser.parse_args()
    os.environ['LD_LIBRARY_PATH'] = str(TOOLS)
    if not __debug__:
        raise RuntimeError('Python -O is forbidden: safety assertions required')
    # Global deadline also bounds pipe reads and all child actors.
    import signal
    signal.signal(signal.SIGALRM, lambda *_: (_ for _ in ()).throw(
        TimeoutError('smoke global deadline')))
    signal.alarm(300)
    exit_status = 0
    try:
        main(options)
    except BaseException as error:
        exit_status = 1
        print('SMOKE_ERROR', repr(error), flush=True)
    finally:
        signal.alarm(0)
        for process in CHILDREN:
            try:
                if process.poll() is None:
                    process.terminate()
                    try:
                        process.wait(timeout=5)
                    except subprocess.TimeoutExpired:
                        process.kill()
                        process.wait(timeout=5)
                print('CHILD_EXIT', process.pid, process.returncode, flush=True)
            except BaseException as error:
                print('CHILD_TEARDOWN_ERROR', process.pid, repr(error), flush=True)
                exit_status = 1
            finally:
                for stream in (process.stdin, process.stdout, process.stderr):
                    if stream and not stream.closed:
                        stream.close()
        if MANAGED:
            try:
                validate_fixture(allow_absent_state=True)
                validate_units()
                stop()
            except BaseException as error:
                print('TEARDOWN_ERROR', repr(error), flush=True)
                exit_status = 1
        try:
            run('journalctl', '-u', 'consentd-smoke.service',
                '--no-pager', '-n', '80')
        except BaseException as error:
            print('LOG_COLLECTION_ERROR', repr(error), flush=True)
        print('SMOKE_EXIT', exit_status, flush=True)
    sys.exit(exit_status)
