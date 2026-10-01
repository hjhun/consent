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
import errno
import hashlib
import json
import os
import stat
import subprocess
import sys
from pathlib import PurePosixPath

def require(value, message):
    if not value:
        raise RuntimeError(message)

def stable_file(current, original):
    keys = ('st_dev', 'st_ino', 'st_uid', 'st_gid', 'st_mode', 'st_nlink',
            'st_size', 'st_mtime_ns', 'st_ctime_ns')
    return all(getattr(current, key) == getattr(original, key) for key in keys)


def file_label(fd):
    try:
        return os.getxattr(fd, 'security.SMACK64')
    except OSError as error:
        if error.errno in (errno.ENODATA, errno.EOPNOTSUPP):
            return None
        raise


def remove_inventory(fd, expected_files, owner_uid=0):
    observed = {}
    observed_dirs = set()
    identities = {}
    labels = {}
    children = {}
    def inspect(directory, prefix=''):
        children[prefix] = set(os.listdir(directory))
        for name in children[prefix]:
            value = os.stat(name,dir_fd=directory,follow_symlinks=False)
            path = prefix+name
            identities[path] = value
            require(value.st_uid==owner_uid and not value.st_mode & 0o022,
                    'foreign owned entry: '+path)
            if stat.S_ISDIR(value.st_mode):
                observed_dirs.add(path)
                child = os.open(name,os.O_RDONLY|os.O_DIRECTORY|os.O_NOFOLLOW,
                                dir_fd=directory)
                try:
                    require(os.fstat(child)==value,'directory raced')
                    inspect(child,path+'/')
                finally:
                    os.close(child)
            else:
                require(stat.S_ISREG(value.st_mode) and value.st_nlink==1 and
                        value.st_size<=32*1024*1024,'foreign file: '+path)
                file = os.open(name,os.O_RDONLY|os.O_NOFOLLOW,dir_fd=directory)
                try:
                    require(stable_file(os.fstat(file), value), 'file raced')
                    labels[path] = file_label(file)
                    digest=hashlib.sha256()
                    while True:
                        part=os.read(file,65536)
                        if not part:
                            break
                        digest.update(part)
                    require(stable_file(os.fstat(file), value) and
                            file_label(file) == labels[path],
                            'file changed during read')
                    observed[path]=digest.hexdigest()
                finally:
                    os.close(file)
    inspect(fd)
    require(observed==expected_files, 'payload set/hash changed')
    expected_dirs = {str(parent) for path in expected_files
                     for parent in PurePosixPath(path).parents
                     if str(parent) != '.'}
    require(observed_dirs==expected_dirs, 'payload directory set changed')
    def same_identity(current, original):
        keys = ('st_dev', 'st_ino', 'st_uid', 'st_gid', 'st_mode')
        return all(getattr(current, key) == getattr(original, key)
                   for key in keys)

    def remove(directory, prefix=''):
        require(set(os.listdir(directory)) == children[prefix],
                'unexpected entry before removal: ' + prefix)
        for name in sorted(children[prefix]):
            path = prefix + name
            original = identities[path]
            current = os.stat(name, dir_fd=directory, follow_symlinks=False)
            require(same_identity(current, original), 'replaced entry: ' + path)
            flags = os.O_RDONLY | os.O_NOFOLLOW | os.O_CLOEXEC
            if stat.S_ISDIR(original.st_mode):
                child = os.open(name, flags | os.O_DIRECTORY, dir_fd=directory)
                try:
                    require(same_identity(os.fstat(child), original),
                            'directory raced open: ' + path)
                    remove(child, path + '/')
                finally:
                    os.close(child)
                require(same_identity(os.stat(name, dir_fd=directory,
                            follow_symlinks=False), original),
                        'directory replaced before removal: ' + path)
                os.rmdir(name, dir_fd=directory)
            else:
                child = os.open(name, flags, dir_fd=directory)
                try:
                    require(stable_file(os.fstat(child), original) and
                            file_label(child) == labels[path],
                            'file changed before removal: ' + path)
                    digest = hashlib.sha256()
                    size = 0
                    while True:
                        part = os.read(child, 65536)
                        if not part:
                            break
                        size += len(part)
                        require(size <= 32 * 1024 * 1024, 'file grew: ' + path)
                        digest.update(part)
                    require(stable_file(os.fstat(child), original) and
                            file_label(child) == labels[path] and
                            digest.hexdigest() == expected_files[path],
                            'file content changed: ' + path)
                    require(stable_file(os.stat(name, dir_fd=directory,
                                follow_symlinks=False), original),
                            'file replaced before unlink: ' + path)
                    os.unlink(name, dir_fd=directory)
                finally:
                    os.close(child)
        require(not os.listdir(directory), 'new entry during removal: ' +
            prefix)
    remove(fd)

def main(ROOT='/opt/var/lib/consent-ui-native-09', arguments=None):
    arguments = sys.argv[1:] if arguments is None else arguments
    if len(arguments) != 2:
        raise SystemExit(2)
    manifest = ROOT + '/cleanup.json'
    metadata = os.lstat(manifest)
    if not stat.S_ISREG(metadata.st_mode) or metadata.st_uid != 0 or (
            metadata.st_nlink != 1 or metadata.st_mode & 0o022):
        raise RuntimeError('foreign cleanup manifest')
    handle = os.open(manifest, os.O_RDONLY | os.O_NOFOLLOW | os.O_CLOEXEC)
    try:
        label = file_label(handle)
        if not stable_file(os.fstat(handle), metadata):
            raise RuntimeError('cleanup manifest raced')
        chunks = []
        size = 0
        while True:
            chunk = os.read(handle, min(65536, 1048577 - size))
            if not chunk:
                break
            chunks.append(chunk)
            size += len(chunk)
            if size > 1048576:
                raise RuntimeError('cleanup manifest exceeds bound')
        require_metadata = os.fstat(handle)
        if not stable_file(require_metadata, metadata) or (
                file_label(handle) != label):
            raise RuntimeError('cleanup manifest changed during read')
        content = b''.join(chunks)
    finally:
        os.close(handle)
    if (len(content) > 1048576 or
            hashlib.sha256(content).hexdigest() != arguments[0]):
        raise RuntimeError('cleanup manifest hash mismatch')
    EXPECTED = json.loads(content)
    EXPECTED['files']['cleanup.json'] = arguments[0]
    EXPECTED['files']['cleanup.py'] = arguments[1]

    for name in ('consentd-ui09.socket', 'consentd-ui09.service',
                 'consent-feature-ui09.socket', 'consent-feature-ui09.service'):
        value = subprocess.check_output(['systemctl','show','-p','LoadState',
                                         '--value',name],text=True).strip()
        require(value == 'not-found', 'unit acquired or foreign: '+name)
    for path in ('/opt/var/lib/consent-ui09-state',
                 '/opt/var/lib/consent-ui09-authority',
                 '/opt/var/lib/consent-poc-runtime/consent.sock',
                 '/opt/var/lib/consent-feature-runtime/argo.sock'):
        require(not os.path.lexists(path),
            'unexpected protected state/endpoint')
    info = os.lstat(ROOT)
    require(stat.S_ISDIR(info.st_mode), 'root not directory')
    for key in ('dev','inode','uid','gid'):
        require(getattr(info,'st_'+('ino' if key=='inode' else key)) ==
                EXPECTED['root'][key], 'root identity changed: '+key)
    require(stat.S_IMODE(info.st_mode)==EXPECTED['root']['mode'],
        'root mode changed')
    require(os.getxattr(ROOT,'security.SMACK64',
        follow_symlinks=False).decode()==
            EXPECTED['root']['smack'], 'root label changed')
    fd = os.open(ROOT,os.O_RDONLY|os.O_DIRECTORY|os.O_NOFOLLOW|os.O_CLOEXEC)
    try:
        require(os.fstat(fd)==info, 'root raced open')
        remove_inventory(fd, EXPECTED['files'])
    finally:
        os.close(fd)
    current=os.lstat(ROOT)
    require((current.st_dev,current.st_ino)==(info.st_dev,info.st_ino),
        'root replaced')
    os.rmdir(ROOT)
    print('OWNED_PAYLOAD_CLEANUP0')


if __name__ == '__main__':
    main()
