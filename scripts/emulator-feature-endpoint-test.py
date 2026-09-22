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
# Selected development emulator only. Run as root/System::Privileged.
# The real bridge validates fake servers; this never impersonates the UI.

import ctypes
import errno
import grp
import hashlib
import json
import os
from pathlib import Path
import signal
import stat
import subprocess
import sys
import tempfile
import time


RUNTIME = Path('/opt/var/lib/consent-feature-runtime')
ENDPOINT = RUNTIME / 'argo.sock'
FIXTURE = Path('/usr/libexec/consent/tests/feature-endpoint-fixture')
FEATURE_SERVICE = 'consent-feature-poc.service'
FEATURE_SOCKET = 'consent-feature-poc.socket'
TEST_SERVICE = 'consent-feature-endpoint-test.service'
TEST_SOCKET = 'consent-feature-endpoint-test.socket'
UNIT_DIRECTORY = Path('/etc/systemd/system')
SUCCESS = 'PASS untrusted endpoint rejected before hello'


def require(condition, message):
    if not condition:
        raise RuntimeError(message)


def command(*arguments, timeout=15):
    result = subprocess.run(arguments, text=True, stdout=subprocess.PIPE,
                            stderr=subprocess.PIPE, timeout=timeout, check=False)
    require(len(result.stdout) + len(result.stderr) < 131072, 'command output exceeded bound')
    require(result.returncode == 0, '{} failed: {}'.format(arguments[0], result.stderr.strip()[:512]))
    return result.stdout.strip()


def property_value(unit, name):
    return command('systemctl', 'show', '-p', name, '--value', unit)


def protected_directory(path):
    path = Path(path)
    for item in (Path('/'), *reversed(path.parents[:-1]), path):
        info = item.lstat()
        require(stat.S_ISDIR(info.st_mode) and info.st_uid == 0 and not info.st_mode & 0o022,
                'unsafe protected directory: ' + str(item))


def metadata(path, socket=False):
    info = path.lstat()
    require(info.st_uid == 0 and info.st_nlink == 1 and
            (stat.S_ISSOCK(info.st_mode) if socket else stat.S_ISREG(info.st_mode)),
            'unexpected protected object: ' + str(path))
    return info


def read_file(path, limit=8192):
    descriptor = os.open(path, os.O_RDONLY | os.O_NOFOLLOW | os.O_CLOEXEC | os.O_NONBLOCK)
    try:
        info = os.fstat(descriptor)
        require(stat.S_ISREG(info.st_mode) and info.st_uid == 0 and info.st_nlink == 1 and
                not info.st_mode & 0o022 and info.st_size <= limit, 'unsafe evidence file')
        value = os.read(descriptor, limit + 1)
        require(len(value) == info.st_size, 'evidence file changed during read')
        return value
    finally:
        os.close(descriptor)


def write_new(path, contents, mode=0o600):
    descriptor = os.open(path, os.O_WRONLY | os.O_CREAT | os.O_EXCL |
                         os.O_CLOEXEC | os.O_NOFOLLOW, mode)
    try:
        os.fchmod(descriptor, mode)
        remaining = memoryview(contents)
        while remaining:
            written = os.write(descriptor, remaining)
            require(written > 0, 'short evidence write')
            remaining = remaining[written:]
        os.fsync(descriptor)
    finally:
        os.close(descriptor)
    descriptor = os.open(path.parent, os.O_RDONLY | os.O_DIRECTORY | os.O_CLOEXEC)
    try:
        os.fsync(descriptor)
    finally:
        os.close(descriptor)


def inode(path):
    info = metadata(path, socket=True)
    return info.st_dev, info.st_ino


def wait_for(check, timeout, failure):
    deadline = time.monotonic() + timeout
    while time.monotonic() < deadline:
        if check():
            return
        time.sleep(0.02)
    raise RuntimeError(failure)


class Scenario:
    def __init__(self):
        self.artifacts = None
        self.original_states = {}
        self.original_socket = None
        self.owned_socket = None
        self.direct = None
        self.units = {}
        self.stopped = False
        self.quiesced = False
        self.alternate = None

    def stop_original(self):
        command('systemctl', 'stop', FEATURE_SOCKET, FEATURE_SERVICE)
        require(property_value(FEATURE_SERVICE, 'MainPID') == '0', 'ordinary feature daemon did not stop')
        for unit in (FEATURE_SOCKET, FEATURE_SERVICE):
            require(property_value(unit, 'ActiveState') == 'inactive', 'ordinary feature unit remains active')

    def bridge_denied(self, name):
        library = ctypes.CDLL('libconsent-feature-poc.so.0')
        library.consent_feature_catalog.argtypes = [ctypes.POINTER(ctypes.c_void_p)]
        library.consent_feature_catalog.restype = ctypes.c_int
        library.consent_feature_free.argtypes = [ctypes.c_void_p]
        library.consent_feature_free.restype = None
        output = ctypes.c_void_p()
        status = library.consent_feature_catalog(ctypes.byref(output))
        had_output = output.value is not None
        if had_output:
            library.consent_feature_free(output)
        write_new(self.artifacts / (name + '-bridge.json'),
                  (json.dumps({'status': status, 'output_null': not had_output}) + '\n').encode())
        require(status == -errno.EACCES and not had_output,
                name + ' was not rejected with EACCES and NULL output')

    def cleanup(self):
        for number in (signal.SIGHUP, signal.SIGINT, signal.SIGTERM):
            signal.signal(number, signal.SIG_IGN)
        if self.direct is not None and self.direct.poll() is None:
            self.direct.terminate()
            try:
                self.direct.wait(timeout=2)
            except subprocess.TimeoutExpired:
                self.direct.kill()
                self.direct.wait(timeout=2)
        if self.units:
            loaded = [unit for unit in (TEST_SOCKET, TEST_SERVICE)
                      if property_value(unit, 'LoadState') != 'not-found']
            if loaded:
                command('systemctl', 'stop', *loaded)
            if TEST_SERVICE in loaded:
                require(property_value(TEST_SERVICE, 'MainPID') == '0', 'fake activated service did not stop')
            for unit in loaded:
                require(property_value(unit, 'ActiveState') in ('inactive', 'failed'),
                        'fake activation unit remains active')
        if self.owned_socket is not None and ENDPOINT.exists():
            require(inode(ENDPOINT) == self.owned_socket, 'endpoint was replaced; refusing unlink')
            ENDPOINT.unlink()
        elif ENDPOINT.exists() and self.quiesced:
            raise RuntimeError('unexpected endpoint appeared; original backup retained')
        for path, digest in self.units.items():
            require(hashlib.sha256(read_file(path)).hexdigest() == digest,
                    'temporary unit changed; refusing removal')
            path.unlink()
        if self.units:
            command('systemctl', 'daemon-reload')
        if self.original_socket is not None:
            require(not ENDPOINT.exists() and not ENDPOINT.is_symlink(), 'cannot restore original socket without clobber')
            backup = self.artifacts / 'original.sock'
            require(inode(backup) == self.original_socket, 'original socket backup changed')
            os.rename(backup, ENDPOINT)
        if self.stopped:
            for unit in (FEATURE_SOCKET, FEATURE_SERVICE):
                if self.original_states[unit] == 'active':
                    command('systemctl', 'start', unit)
            for unit, expected in self.original_states.items():
                require(property_value(unit, 'ActiveState') == expected, 'original unit state was not restored')
        if self.artifacts:
            write_new(self.artifacts / 'restored.json', b'{"original_feature_units_restored":true}\n')

    def direct_case(self):
        output_path = self.artifacts / 'direct.log'
        descriptor = os.open(output_path, os.O_WRONLY | os.O_CREAT | os.O_EXCL | os.O_CLOEXEC, 0o600)
        try:
            self.direct = subprocess.Popen([str(FIXTURE), '--direct'], stdout=descriptor,
                                           stderr=subprocess.STDOUT, close_fds=True)
        finally:
            os.close(descriptor)
        wait_for(lambda: ENDPOINT.exists() or self.direct.poll() is not None, 3,
                 'direct fixture did not bind within bound')
        require(self.direct.poll() is None and ENDPOINT.exists(), 'direct fixture failed before bridge connection')
        self.owned_socket = inode(ENDPOINT)
        os.chown(ENDPOINT, 0, grp.getgrnam('users').gr_gid, follow_symlinks=False)
        require(stat.S_IMODE(ENDPOINT.lstat().st_mode) == 0o660, 'direct endpoint mode mismatch')
        label = Path('/proc/{}/attr/current'.format(self.direct.pid)).read_text().rstrip('\x00\n')
        require(label == 'System::Privileged', 'direct fixture must otherwise match the trusted server label')
        write_new(self.artifacts / 'direct-peer.json',
                  (json.dumps({'pid': self.direct.pid, 'uid': 0, 'label': label}) + '\n').encode())
        self.bridge_denied('direct')
        require(self.direct.wait(timeout=7) == 0, 'direct fixture received bytes or timed out')
        require(read_file(output_path).decode().strip() == SUCCESS, 'direct denial-before-bytes proof missing')
        require(not ENDPOINT.exists(), 'direct fixture did not remove its own socket')
        self.owned_socket = None
        print('PASS direct non-PID1 server rejected before any request byte')

    def activated_case(self):
        self.alternate = self.artifacts / 'alternate.sock'
        output_path = self.artifacts / 'activated.log'
        write_new(output_path, b'')
        socket_text = ('[Socket]\nListenStream=' + str(self.alternate) +
                       '\nSocketUser=root\nSocketGroup=users\nSocketMode=0660\n'
                       'Accept=no\nRemoveOnStop=yes\nService=' + TEST_SERVICE + '\n')
        service_text = ('[Service]\nType=oneshot\nRemainAfterExit=yes\nExecStart=' +
                        str(FIXTURE) + ' --activated\nUser=root\nGroup=root\n'
                        'SmackProcessLabel=System::Privileged\nTimeoutStartSec=20s\n'
                        'StandardOutput=file:' + str(output_path) +
                        '\nStandardError=journal\nNoNewPrivileges=yes\n')
        for name, text in ((TEST_SOCKET, socket_text), (TEST_SERVICE, service_text)):
            path = UNIT_DIRECTORY / name
            data = text.encode()
            self.units[path] = hashlib.sha256(data).hexdigest()
            write_new(path, data, 0o644)
        command('systemctl', 'daemon-reload')
        command('systemctl', 'start', TEST_SOCKET)
        require(property_value(TEST_SOCKET, 'ActiveState') == 'active', 'alternate PID1 socket not active')
        self.owned_socket = inode(self.alternate)
        require(not ENDPOINT.exists() and not ENDPOINT.is_symlink(), 'refusing to clobber endpoint')
        os.rename(self.alternate, ENDPOINT)
        self.bridge_denied('renamed-pid1')
        wait_for(lambda: property_value(TEST_SERVICE, 'SubState') == 'exited', 8,
                 'activated fixture did not finish within bound')
        values = {name: property_value(TEST_SERVICE, name)
                  for name in ('ExecMainCode', 'ExecMainStatus', 'MainPID', 'Result')}
        require(values == {'ExecMainCode': '1', 'ExecMainStatus': '0', 'MainPID': '0', 'Result': 'success'},
                'activated fixture did not execute and exit successfully')
        require(read_file(output_path).decode().strip() == SUCCESS, 'PID1 rename denial-before-bytes proof missing')
        write_new(self.artifacts / 'activated-status.json', (json.dumps(values) + '\n').encode())
        print('PASS renamed PID1 socket rejected by original bound path before any request byte')

    def run(self):
        require(os.geteuid() == 0, 'run as root/System::Privileged')
        require(Path('/proc/self/attr/current').read_text().rstrip('\x00\n') == 'System::Privileged',
                'explicit System::Privileged provisioning context required')
        for path in ('/opt/var/lib', RUNTIME, UNIT_DIRECTORY, FIXTURE.parent):
            protected_directory(path)
        runtime = RUNTIME.lstat()
        require(stat.S_IMODE(runtime.st_mode) == 0o755 and runtime.st_gid == 0 and
                os.getxattr(RUNTIME, 'security.SMACK64').rstrip(b'\x00') == b'_',
                'ordinary feature runtime metadata is unexpected')
        executable = metadata(FIXTURE)
        require(executable.st_mode & 0o111 and not executable.st_mode & 0o022, 'unsafe fixture executable')
        for unit in (TEST_SOCKET, TEST_SERVICE):
            require(not (UNIT_DIRECTORY / unit).exists() and not (UNIT_DIRECTORY / unit).is_symlink(),
                    'temporary endpoint unit path already exists')
            require(property_value(unit, 'LoadState') == 'not-found', 'temporary endpoint unit already exists')
        for unit in (FEATURE_SOCKET, FEATURE_SERVICE):
            require(not property_value(unit, 'DropInPaths'), 'ordinary feature unit has an override')
            self.original_states[unit] = property_value(unit, 'ActiveState')
            require(self.original_states[unit] in ('active', 'inactive'), 'ordinary feature unit is not settled')
        self.artifacts = Path(tempfile.mkdtemp(prefix='consent-feature-endpoint-', dir='/opt/var/lib'))
        os.chmod(self.artifacts, 0o700)
        print('ARTIFACTS ' + str(self.artifacts), flush=True)
        write_new(self.artifacts / 'original-states.json', (json.dumps(self.original_states) + '\n').encode())
        try:
            self.stopped = True
            self.stop_original()
            self.quiesced = True
            if ENDPOINT.exists() or ENDPOINT.is_symlink():
                self.original_socket = inode(ENDPOINT)
                os.rename(ENDPOINT, self.artifacts / 'original.sock')
            self.direct_case()
            self.activated_case()
        finally:
            self.cleanup()


def interrupted(number, frame):
    del frame
    raise RuntimeError('test interrupted by signal ' + str(number))


if __name__ == '__main__':
    for number in (signal.SIGHUP, signal.SIGINT, signal.SIGTERM):
        signal.signal(number, interrupted)
    os.umask(0o077)
    try:
        Scenario().run()
    except Exception as error:
        print('FAIL feature endpoint test: ' + str(error), file=sys.stderr)
        sys.exit(1)
