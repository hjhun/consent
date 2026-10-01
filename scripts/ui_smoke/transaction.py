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
import inspect
import hashlib
import json
from pathlib import Path
import secrets
import time

from . import commands as base
from .device_audit import cgroup_empty

HERE = Path(__file__).resolve().parent
OUTPUT = None
BUILD = None
ROOT = base.ROOT
APP = 'org.tizen.consentui'


def configure(build, output):
    global BUILD, OUTPUT
    BUILD, OUTPUT = build, output
    base.set_output(output)


def require(condition, message):
    if not condition:
        raise RuntimeError(message)


def sources():
    return BUILD.payload()


class Transaction:
    def __init__(self):
        self.output = OUTPUT
        self.nonce = secrets.token_hex(32)
        self.created_root = False
        self.install_attempted = False
        self.setup_succeeded = False
        self.setup_dispatched = False
        self.root_identity = None
        self.transients = {}
        self.created_runtime = {}
        self.counter = 0
        self.before = None
        self.after = None
        self.owned_files = {}

    def remote(self, args, kind, timeout=30):
        self.counter += 1
        return base.remote(args, f'host-{self.counter:03}-{kind}.log', timeout)

    def upload(self, source, relative):
        require(source.is_file() and not source.is_symlink(), 'unsafe source')
        require(self.created_root and relative
            and '..' not in Path(relative).parts
                and not Path(relative).is_absolute(), 'unsafe owned upload')
        self.verify_root()
        target = ROOT + '/' + relative
        result = base.run(['sdb', '-s', base.SERIAL, 'push', str(source),
            target], 45)
        require(source.is_file() and not source.is_symlink(), 'unsafe source')
        actual = self.remote(['sha256sum', target], 'uploaded-hash').split()[0]
        require(actual == hashlib.sha256(source.read_bytes()).hexdigest(),
                'uploaded bytes mismatch')
        self.owned_files[relative] = actual
        return result

    def verify_root(self):
        code = ('import os,json;s=os.lstat(' + repr(ROOT) + ');'
                'print(json.dumps([s.st_dev,s.st_ino]))')
        current = json.loads(self.remote(['python3', '-c', code],
                                         'verify-root'))
        require(current == [self.root_identity['dev'],
                            self.root_identity['inode']],
                'original acquired root replaced')

    def preflight(self):
        for unit in ('consentd-poc.service', 'consentd-poc.socket',
                     'consent-feature-poc.service',
                         'consent-feature-poc.socket'):
            state = self.remote(['systemctl', 'show', '-p', 'ActiveState',
                                 '--value', unit], 'original-active')
            pid = self.remote(['systemctl', 'show', '-p', 'MainPID',
                               '--value', unit], 'original-pid')
            require(state == 'inactive' and pid in ('', '0'),
                    'original PoC/feature unit is not inactive')
        for unit in ('consentd-ui09.service', 'consentd-ui09.socket',
                     'consent-feature-ui09.service',
                         'consent-feature-ui09.socket'):
            state = self.remote(['systemctl', 'show', '-p', 'LoadState',
                                 '--value', unit], 'fresh-unit')
            require(state == 'not-found', 'preexisting owned unit')
        paths = [ROOT, '/opt/var/lib/consent-ui09-state',
                 '/opt/var/lib/consent-ui09-authority',
                 '/opt/var/lib/consent-poc-runtime/consent.sock',
                 '/opt/var/lib/consent-feature-runtime/argo.sock']
        program = ('import os,sys;p=' + repr(paths) +
                   ';sys.exit(1 if any(os.path.lexists(x) for x in p) else 0)')
        self.remote(['python3', '-c', program], 'fresh-paths')

    def stage(self):
        files = sources()
        self.preflight()
        self.remote(['python3', '-c',
                     'import os,sys;sys.exit(1 if os.path.lexists(' +
                     repr(ROOT) + ') else 0)'], 'fresh-root')
        self.remote(['mkdir', '-m', '0750', ROOT], 'mkdir-root')
        self.created_root = True
        code = ('import os,json,stat;s=os.lstat(' + repr(ROOT) + ');'
                'print(json.dumps({"dev":s.st_dev,"inode":s.st_ino,'
                '"uid":s.st_uid,"gid":s.st_gid,'
                '"mode":stat.S_IMODE(s.st_mode),'
                '"smack":os.getxattr(' + repr(ROOT) +
                ',"security.SMACK64",follow_symlinks=False).decode()}))')
        self.root_identity = json.loads(self.remote(
            ['python3', '-c', code], 'root-acquisition'))
        self.acquired_root = dict(self.root_identity)
        self.remote(['chown', 'root:security_fw', ROOT], 'root-owner')
        protected = json.loads(self.remote(['python3', '-c', code],
                                          'root-protection'))
        require(all(protected[key] == self.acquired_root[key]
                    for key in ('dev', 'inode', 'uid', 'mode', 'smack')),
                'acquired root replaced during protection')
        self.root_identity = protected
        for name in ('tests', 'tools', 'libraries', 'tpk'):
            self.remote(['mkdir', '-m', '0755', ROOT + '/' + name],
                'mkdir-child')
        marker = OUTPUT / 'stage-marker'
        marker.write_text('CONSENT-UI-NATIVE-09\n')
        self.upload(marker, 'marker')
        self.remote(['chmod', '0600', ROOT + '/marker'], 'marker-mode')
        for relative, path in files.items():
            self.upload(path, relative)
            mode = '0644' if relative.startswith('tpk/') else '0755'
            self.remote(['chmod', mode, ROOT + '/' + relative], 'payload-mode')
            self.remote(['chsmack', '-a', '_', ROOT + '/' + relative], 'label')
        manifest = OUTPUT / 'target-payload.json'
        manifest.write_text(json.dumps({'task': 'CONSENT-UI-NATIVE-09',
            'release': BUILD.release, 'invocation': self.nonce,
            'root_identity': self.root_identity,
                'files': {name: hashlib.sha256(path.read_bytes())
                                   .hexdigest() for name,
                                       path in files.items()}},
            indent=2) + '\n')
        self.upload(manifest, 'payload.json')
        self.remote(['chmod', '0600', ROOT + '/payload.json'], 'manifest-mode')
        self.upload(HERE / 'device_audit.py', 'audit.py')
        self.remote(['chmod', '0600', ROOT + '/audit.py'], 'audit-mode')
        require(self.audit('gui-pids') == [], 'original UI already running')
        self.before = self.audit('fingerprint')
        current = self.audit('ownership')['root']
        require(all(current[key] == value
                    for key, value in self.root_identity.items()),
                'acquired root identity changed')
        (OUTPUT / 'global-before.json').write_text(
            json.dumps(self.before, indent=2) + '\n')

    def audit(self, phase, timeout=45):
        output = self.remote(['python3', ROOT + '/audit.py', phase],
                             'audit-' + phase, timeout)
        return json.loads(output)

    def maps(self):
        result = self.audit('maps')
        name = OUTPUT / ('native-maps-r7-' + secrets.token_hex(4) + '.json')
        name.write_text(json.dumps(result, indent=2) + '\n')
        return result

    def journal_transient(self, kind, arguments, properties):
        unit = 'consent-ui09-' + kind + '-' + secrets.token_hex(8) + '.service'
        require(self.remote(['systemctl', 'show', '-p', 'LoadState',
                             '--value', unit], 'transient-fresh')
                                 == 'not-found',
                'foreign transient exists')
        self.transients[unit] = {'arguments': arguments,
            'properties': properties}
        ledger = OUTPUT / 'host-transients.json'
        ledger.write_text(json.dumps(self.transients, indent=2) + '\n')
        ledger.chmod(0o600)
        return unit

    def dispatch(self, kind, arguments, properties, timeout):
        unit = self.journal_transient(kind, arguments, properties)
        command = ['systemd-run', '--quiet', '--wait', '--pipe', '--collect',
                   '--unit=' + unit.removesuffix('.service')]
        for name, value in properties.items():
            command.extend(('-p', name + '=' + value))
        command.extend(arguments)
        primary = None
        result = None
        try:
            result = self.remote(command, kind, timeout)
        except BaseException as error:
            primary = error
        try:
            self.drain_transient(unit)
        except BaseException as error:
            (OUTPUT / ('dispatch-error-' + kind + '.json')).write_text(
                json.dumps({'primary': repr(primary),
                            'teardown': repr(error)}, indent=2) + '\n')
            raise RuntimeError('transient teardown failed; primary=' +
                               repr(primary)) from error
        if primary is not None:
            raise primary
        return result

    def drain_transient(self, unit):
        expected = self.transients[unit]
        state = self.remote(['systemctl', 'show', '-p', 'LoadState',
                             '--value', unit], 'transient-load')
        if state == 'not-found':
            return
        fragment = self.remote(['systemctl', 'show', '-p', 'FragmentPath',
                                '--value', unit], 'transient-fragment')
        require(fragment == '/run/systemd/transient/' + unit,
                'foreign transient fragment')
        require(self.remote(['systemctl', 'show', '-p', 'Transient',
                             '--value', unit], 'transient-type') == 'yes',
                'unit is not transient')
        require(not self.remote(['systemctl', 'show', '-p', 'DropInPaths',
                                 '--value', unit], 'transient-dropin'),
                'foreign transient drop-in')
        executable = self.remote(['systemctl', 'show', '-p', 'ExecStart',
                                  '--value', unit], 'transient-exec')
        argv = expected['arguments']
        prefix = '{ path=' + argv[0] + ' ; argv[]=' + ' '.join(argv) + ' ;'
        require(executable.startswith(prefix), 'transient exact argv changed')
        for field, value in expected['properties'].items():
            if field in ('RuntimeMaxSec', 'TimeoutStartSec'):
                field = ('RuntimeMaxUSec' if field == 'RuntimeMaxSec' else
                         'TimeoutStartUSec')
                seconds = int(value)
                require(seconds in (10, 45, 150), 'unknown transient budget')
                value = {10: ('10s',), 45: ('45s',),
                         150: ('150s', '2min 30s')}[seconds]
            actual = self.remote(['systemctl', 'show', '-p', field,
                                  '--value', unit], 'transient-property')
            matches = actual in value if isinstance(value,
                tuple) else actual == value
            require(matches, 'transient property changed: ' + field)
        self.remote(['systemctl', 'stop', unit], 'transient-stop')
        deadline = time.monotonic() + 12
        while time.monotonic() < deadline:
            pid = self.remote(['systemctl', 'show', '-p', 'MainPID',
                               '--value', unit], 'transient-pid')
            group = self.remote(['systemctl', 'show', '-p', 'ControlGroup',
                                 '--value', unit], 'transient-cgroup')
            if pid in ('', '0') and not group:
                return
            if group:
                program = ('import os,json;from pathlib import Path\n' +
                           inspect.getsource(cgroup_empty) +
                           '\nprint(json.dumps(cgroup_empty(' + repr(group) +
                           ')))')
                empty = json.loads(self.remote(['python3', '-c', program],
                                                'transient-tasks'))
                if pid in ('', '0') and empty is True:
                    return
            time.sleep(0.1)
        raise RuntimeError('owned transient cgroup did not drain')

    def package(self, filename, kind):
        require(filename in ('ui09.tpk', 'original.tpk'),
            'invalid package path')
        arguments = ['/usr/bin/pkgcmd', '-i', '-t', 'tpk', '-p',
                     ROOT + '/tpk/' + filename, '--global']
        properties = {'User': 'root', 'Group': 'root',
            'SmackProcessLabel': 'System::Privileged',
            'RuntimeMaxSec': '45', 'TimeoutStartSec': '45'}
        return self.dispatch(kind, arguments, properties, 55)

    def script(self, phase):
        require(phase in ('setup', 'cleanup', 'purge', 'status', 'stop'),
                'invalid fixture phase')
        arguments = ['/usr/bin/python3', ROOT + '/tests/emulator-ui-native.py',
                     phase]
        properties = {'User': 'root', 'Group': 'root',
            'SmackProcessLabel': 'System::Privileged',
            'RuntimeMaxSec': '150', 'TimeoutStartSec': '150'}
        return self.dispatch('fixture-' + phase, arguments, properties, 155)

    def revoke_calendar(self):
        arguments = [ROOT + '/tests/consent-ui09-admin']
        bindings = [ROOT + '/tools:/usr/libexec/consent/poc']
        for name in ('libconsent-poc.so.0.1.0',
                     'libconsent-feature-poc.so.0.1.0'):
            bindings.append(ROOT + '/libraries/' + name + ':/usr/lib64/' + name)
        properties = {'User': 'root', 'Group': 'root',
            'SmackProcessLabel': 'System', 'PrivateMounts': 'yes',
            'BindReadOnlyPaths': ' '.join(bindings),
            'RuntimeMaxSec': '10', 'TimeoutStartSec': '10'}
        response = self.dispatch('revoke-calendar', arguments, properties, 15)
        require('UI09_REVOKE status=0' in response, 'fixed revoke failed')
        return response

    def install(self):
        self.install_attempted = True
        self.package('ui09.tpk', 'install-tpk')
        self.setup_dispatched = True
        result = self.script('setup')
        require('READY UI09' in result and 'UI09_EXIT0' in result,
                'owned setup did not confirm success')
        proof = self.audit('ownership')
        self.validate_acquisition(proof)
        require(proof['acquired'], 'setup lacks completed acquisition proof')
        self.setup_succeeded = True
        self.capture_generated()
        self.created_runtime = proof['created_runtime']

    def launch(self):
        arguments = ['/usr/libexec/consent/poc/consent-poc-launch', APP,
                     '--settings', 'ko-KR']
        properties = {'User': 'owner', 'Group': 'users',
            'SmackProcessLabel': 'System::Privileged',
            'RuntimeMaxSec': '10', 'TimeoutStartSec': '10'}
        return self.dispatch('launch', arguments, properties, 15)

    def capture_generated(self):
        records = self.audit('generated')
        require(isinstance(records, dict) and set(records).issubset({
            'roles.conf', 'generation', 'managed.json', 'actors.json',
            'acquired.json'}), 'unexpected generated payload inventory')
        require({'roles.conf', 'generation', 'managed.json', 'acquired.json'}
                <= set(records), 'incomplete generated acquisition inventory')
        for name, digest in records.items():
            require(isinstance(digest, str) and len(digest) == 64,
                    'invalid generated file digest')
            self.owned_files[name] = digest

    def validate_acquisition(self, proof):
        require(proof.get('invocation') == self.nonce,
                'uncertain setup invocation mismatch')
        for key, value in self.root_identity.items():
            require(proof['root'][key] == value,
                    'fresh invocation root identity changed')

    def finish(self):
        errors = []
        # Every cleanup step continues independently; original exception remains
        # separately recorded and any restoration failure makes the run nonzero.
        for unit in list(self.transients):
            try:
                self.drain_transient(unit)
            except BaseException as error:
                errors.append(str(error))
        drained = not errors
        acquired = self.setup_succeeded
        if drained and self.setup_dispatched and not acquired:
            try:
                proof = self.audit('ownership')
                self.validate_acquisition(proof)
                acquired = proof['acquired']
                self.created_runtime = proof.get('created_runtime', {})
                if acquired:
                    self.capture_generated()
                (OUTPUT / 'uncertain-setup-acquisition.json').write_text(
                    json.dumps(proof, indent=2) + '\n')
            except BaseException as error:
                errors.append(str(error))
                drained = False
        ui_stopped = not self.install_attempted
        if drained and self.install_attempted:
            try:
                if self.audit('gui-pids'):
                    self.remote(['pkgcmd', '-k', '-n', APP, '--global'],
                        'close-ui')
                deadline = time.monotonic() + 10
                while self.audit('gui-pids') and time.monotonic() < deadline:
                    time.sleep(0.1)
                require(self.audit('gui-pids') == [],
                    'native UI survived close')
                ui_stopped = True
            except BaseException as error:
                errors.append(str(error))
        services_stopped = not acquired
        if drained and acquired:
            try:
                self.script('cleanup')
                services_stopped = True
            except BaseException as error:
                errors.append(str(error))
        restored = not self.install_attempted
        if (drained and ui_stopped and services_stopped and
                self.install_attempted):
            try:
                self.package('original.tpk', 'restore-original-tpk')
                expected = BUILD.original['dll_sha256']
                actual = self.remote(['sha256sum', base.APP +
                                      '/bin/ConsentUI.dll'], 'restored-ui-hash')
                require(actual.split()[0] == expected,
                    'original UI not restored')
                restored = True
            except BaseException as error:
                errors.append(str(error))
        drained = True
        for unit in list(self.transients):
            try:
                self.drain_transient(unit)
            except BaseException as error:
                errors.append(str(error))
                drained = False
        if (drained and ui_stopped and services_stopped and restored and
                acquired):
            try:
                self.script('purge')
            except BaseException as error:
                errors.append(str(error))
        if self.before is not None:
            try:
                self.after = self.audit('fingerprint')
                (OUTPUT / 'global-after.json').write_text(
                    json.dumps(self.after, indent=2) + '\n')
                for category in ('package_files', 'protected_trees', 'units'):
                    require(self.before[category] == self.after[category],
                            'original global fingerprint changed: ' + category)
                runtime_deltas = {}
                for path, metadata in self.before['runtime_parents'].items():
                    current = self.after['runtime_parents'][path]
                    if metadata.get('absent'):
                        # New parents are classified explicitly, never asserted
                        # equal to an absent original or timestamp-reset.
                        if not current.get('absent'):
                            created = self.created_runtime.get(path)
                            fields = ('dev', 'inode', 'uid', 'gid', 'mode',
                                      'smack')
                            require(acquired and created is not None and
                                    all(current[key] == created[key]
                                        for key in fields),
                                    'created runtime directory replaced')
                        runtime_deltas[path] = {'created': current}
                    else:
                        fields = ('dev', 'inode', 'uid', 'gid', 'mode', 'smack')
                        require(all(metadata[key]
                            == current[key] for key in fields),
                                'original runtime parent metadata changed')
                        runtime_deltas[path] = {'before': metadata,
                                                'after': current}
                (OUTPUT / 'runtime-parent-deltas.json').write_text(
                    json.dumps(runtime_deltas, indent=2) + '\n')
                old = self.before['application']
                new = self.after['application']
                require(set(old) == set(new),
                    'restored package file set differs')
                for path, metadata in old.items():
                    # Package inode/time changes are expected. Bytes and
                    # protected ownership/mode/label/link metadata must match.
                    fields = ('uid', 'gid', 'mode', 'smack', 'sha256', 'link')
                    require(all(metadata.get(key) == new[path].get(key)
                                for key in fields),
                            'restored application metadata/bytes differ: ' +
                            path)
            except BaseException as error:
                errors.append(str(error))
        if (self.created_root and drained and ui_stopped and services_stopped
                and restored and not errors):
            try:
                manifest = OUTPUT / 'cleanup.json'
                manifest.write_text(json.dumps({'root': self.root_identity,
                    'files': self.owned_files}, indent=2) + '\n')
                self.upload(manifest, 'cleanup.json')
                self.remote(['chmod', '0600', ROOT + '/cleanup.json'],
                            'cleanup-manifest-mode')
                helper = HERE / 'device_cleanup.py'
                self.upload(helper, 'cleanup.py')
                self.remote(['chmod', '0600', ROOT + '/cleanup.py'],
                            'cleanup-helper-mode')
                properties = {'User': 'root', 'Group': 'root',
                    'SmackProcessLabel': 'System::Privileged',
                    'RuntimeMaxSec': '45', 'TimeoutStartSec': '45'}
                result = self.dispatch('payload-cleanup',
                    ['/usr/bin/python3', ROOT + '/cleanup.py',
                     self.owned_files['cleanup.json'],
                     self.owned_files['cleanup.py']], properties, 50)
                require('OWNED_PAYLOAD_CLEANUP0' in result,
                        'payload cleanup missing success')
            except BaseException as error:
                errors.append(str(error))
        (OUTPUT / 'finally.json').write_text(json.dumps({'errors': errors,
            'created_root': self.created_root,
            'install_attempted': self.install_attempted,
            'setup_succeeded': self.setup_succeeded}, indent=2) + '\n')
        require(not errors, 'owned teardown/restoration failed: ' +
            '; '.join(errors))


def execute(actions):
    transaction = Transaction()
    original = None
    try:
        transaction.stage()
        transaction.install()
        actions(transaction)
    except BaseException as error:
        original = error
        (OUTPUT / 'host-original-error.txt').write_text(repr(error) + '\n')
    finally:
        try:
            transaction.finish()
        except BaseException as error:
            (OUTPUT / 'host-teardown-error.txt').write_text(repr(error) + '\n')
            original = RuntimeError('restoration failed: ' + repr(error) +
                                    '; primary: ' + repr(original))
    if original is not None:
        raise original
