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
"""Exercise invalid systemd activation before daemon bootstrap."""

import os
from pathlib import Path
import select
import signal
import socket
import stat
import sys
import time


ROOT = Path("/tmp/consent-activation-test-12")
EXPECTED = ROOT / "consent.sock"
WRONG = ROOT / "wrong.sock"


def require(condition, reason):
    if not condition:
        raise RuntimeError(reason)


def directory():
    created = False
    try:
        ROOT.mkdir(mode=0o700)
        created = True
    except FileExistsError:
        pass
    info = ROOT.lstat()
    require(stat.S_ISDIR(info.st_mode), "activation root is not a directory")
    require(info.st_uid == os.getuid(), "activation root owner changed")
    require(stat.S_IMODE(info.st_mode) == 0o700, "activation root mode")
    require(not os.path.lexists(EXPECTED) and
            not os.path.lexists(WRONG),
            "activation endpoint already exists")
    return created


def bound(path, kind):
    descriptor = socket.socket(socket.AF_UNIX, kind)
    descriptor.bind(str(path))
    if kind == socket.SOCK_STREAM:
        descriptor.listen(1)
    return descriptor


def run_child(binary, descriptors):
    read_fd, write_fd = os.pipe2(os.O_CLOEXEC)
    pid = os.fork()
    if pid == 0:
        try:
            os.close(read_fd)
            os.dup2(write_fd, 1)
            os.dup2(write_fd, 2)
            os.close(write_fd)
            for offset, descriptor in enumerate(descriptors):
                target = 3 + offset
                os.dup2(descriptor.fileno(), target)
                os.set_inheritable(target, True)
            environment = os.environ.copy()
            environment.pop("LISTEN_PID", None)
            environment.pop("LISTEN_FDS", None)
            if descriptors:
                environment["LISTEN_PID"] = str(os.getpid())
                environment["LISTEN_FDS"] = str(len(descriptors))
            os.execve(binary, [binary], environment)
        except BaseException:
            os._exit(126)
    os.close(write_fd)
    output = bytearray()
    deadline = time.monotonic() + 3
    finished = None
    eof = False
    while time.monotonic() < deadline:
        if eof and finished is not None:
            break
        timeout = max(1, min(100, int((deadline - time.monotonic()) * 1000)))
        ready, _, _ = select.select([] if eof else [read_fd], [], [],
                                    timeout / 1000)
        if ready:
            chunk = os.read(read_fd, 4096)
            output.extend(chunk)
            require(len(output) <= 16384, "activation log unbounded")
            if not chunk:
                eof = True
        if finished is None:
            done, status = os.waitpid(pid, os.WNOHANG)
            if done:
                finished = status
    os.close(read_fd)
    if finished is None:
        done, status = os.waitpid(pid, os.WNOHANG)
        if done:
            finished = status
    if finished is None:
        os.kill(pid, signal.SIGKILL)
        os.waitpid(pid, 0)
        raise RuntimeError("activation child exceeded deadline")
    require(os.waitstatus_to_exitcode(finished) == 1,
            "activation child did not reject")
    return output.decode("utf-8", errors="replace")


def case(binary, name, sockets, expected):
    identities = [os.fstat(value.fileno()) for value in sockets]
    output = run_child(binary, sockets)
    require(expected in output, name + " missing rejection log: " + output)
    for descriptor, before in zip(sockets, identities):
        after = os.fstat(descriptor.fileno())
        require((before.st_dev, before.st_ino) ==
                (after.st_dev, after.st_ino), name + " parent FD changed")
    print("PASS activation " + name + " rejected before bootstrap")


def main(binary):
    created = directory()
    try:
        case(binary, "no-fd", [], "event=activation-invalid count=0")
        datagram = bound(EXPECTED, socket.SOCK_DGRAM)
        try:
            case(binary, "wrong-type", [datagram],
                 "event=activation-invalid endpoint=")
        finally:
            datagram.close()
            EXPECTED.unlink()
        stream = bound(WRONG, socket.SOCK_STREAM)
        try:
            case(binary, "wrong-path", [stream],
                 "event=activation-invalid endpoint=")
        finally:
            stream.close()
            WRONG.unlink()
        first = bound(EXPECTED, socket.SOCK_STREAM)
        second = bound(WRONG, socket.SOCK_STREAM)
        try:
            case(binary, "two-fds", [first, second],
                 "event=activation-invalid count=2")
        finally:
            first.close()
            second.close()
            EXPECTED.unlink()
            WRONG.unlink()
    finally:
        if created:
            ROOT.rmdir()
    print("PASS activation negative admission: four cases")


if __name__ == "__main__":
    if len(sys.argv) != 2:
        raise SystemExit("Usage: activation_negative_test.py DAEMON")
    try:
        main(sys.argv[1])
    except (OSError, RuntimeError) as error:
        raise SystemExit("FAIL activation: " + str(error)) from error
