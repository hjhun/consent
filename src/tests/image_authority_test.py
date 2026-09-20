#!/usr/bin/env python3
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

"""Exercise the real image generation CLI; requires real root, never fake root."""

import argparse
import configparser
import fcntl
import os
from pathlib import Path
import pwd
import signal
import stat
import subprocess
import tempfile
import time
import uuid


def require(condition, detail):
    if not condition:
        raise RuntimeError(detail)


def snapshot(directory):
    result = {}
    for path in sorted(directory.rglob("*")):
        relative = str(path.relative_to(directory))
        info = path.lstat()
        value = (info.st_mode, info.st_uid, info.st_gid, info.st_nlink)
        if stat.S_ISREG(info.st_mode):
            value += (path.read_bytes(),)
        elif stat.S_ISLNK(info.st_mode):
            value += (os.readlink(path),)
        result[relative] = value
    return result


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--authority-tool", required=True)
    parser.add_argument("--authority-path", default="/opt/var/lib/consent-authority")
    parser.add_argument("--state-root", default="/opt/var/lib")
    parser.add_argument("--account", default="nobody")
    args = parser.parse_args()
    if os.geteuid() != 0:
        print("SKIP image authority: real root is required", flush=True)
        return 77
    tool = str(Path(args.authority_tool).resolve(strict=True))
    account = pwd.getpwnam(args.account)
    require(account.pw_uid != 0, "traversal fixture requires a real nonroot account")
    relative = Path(args.authority_path)
    require(relative.is_absolute() and len(relative.parts) > 1 and ".." not in relative.parts,
            "authority path must be absolute without traversal")
    relative = Path(*relative.parts[1:])

    with tempfile.TemporaryDirectory(prefix="consent-image-authority-", dir=args.state_root) as temporary:
        base = Path(temporary)
        image = base / "image"
        image.mkdir(mode=0o700)
        authority = image / relative
        authority_file = authority / "installations.conf"

        def invoke(command, *arguments, root=image, success=True):
            completed = subprocess.run([tool, "--image-root", str(root), command, *arguments],
                                       text=True, stdout=subprocess.PIPE,
                                       stderr=subprocess.PIPE, timeout=5)
            if success:
                require(completed.returncode == 0,
                        "{} failed: {}".format(command, completed.stderr))
                value = completed.stdout.strip()
                require(str(uuid.UUID(value)) == value, "generation must be a canonical UUID")
                return value
            require(completed.returncode != 0, "{} unexpectedly succeeded".format(command))
            return None

        def read():
            data = configparser.ConfigParser(interpolation=None, strict=True)
            data.read_string(authority_file.read_text())
            return data

        def unchanged_failure(command, *arguments):
            before = snapshot(authority)
            invoke(command, *arguments, success=False)
            require(snapshot(authority) == before, "failed update changed authority")

        package = "image.package"
        generation = invoke("begin", package, "install-1", "absent")
        info = authority_file.stat()
        require(info.st_uid == 0 and info.st_gid == 0 and stat.S_IMODE(info.st_mode) == 0o600,
                "fresh image file must be root:root0600")
        require(stat.S_IMODE(authority.stat().st_mode) == 0o700,
                "fresh image authority directory must be0700")
        for parent in list(authority.parents)[:len(relative.parts) - 1]:
            require(stat.S_IMODE(parent.stat().st_mode) == 0o755,
                    "new image authority ancestors must be0755")
        # The fixture's own private parents are unrelated to target image modes.
        # Permit traversal through them only while a real nonroot child checks
        # newly created target ancestors and the still-private authority leaf.
        base.chmod(0o711)
        image.chmod(0o711)
        child = os.fork()
        if child == 0:
            try:
                os.setgroups([])
                os.setgid(account.pw_gid)
                os.setuid(account.pw_uid)
                require(os.getuid() == account.pw_uid and os.geteuid() != 0,
                        "child did not drop root")
                directory = os.open(authority.parent, os.O_RDONLY | os.O_DIRECTORY)
                os.close(directory)
                try:
                    directory = os.open(authority, os.O_RDONLY | os.O_DIRECTORY)
                except PermissionError:
                    os._exit(0)
                os.close(directory)
                os._exit(1)
            except BaseException as error:
                os.write(2, ("nonroot traversal failed: " + str(error) + "\n").encode())
                os._exit(1)
        try:
            deadline = time.monotonic() + 5
            while True:
                ended, status = os.waitpid(child, os.WNOHANG)
                if ended:
                    require(os.WIFEXITED(status) and os.WEXITSTATUS(status) == 0,
                            "nonroot ancestor traversal or private leaf protection failed")
                    break
                if time.monotonic() >= deadline:
                    os.kill(child, signal.SIGKILL)
                    os.waitpid(child, 0)
                    raise RuntimeError("nonroot traversal fixture timed out")
                time.sleep(0.01)
        finally:
            base.chmod(0o700)
            image.chmod(0o700)
        print("PASS image authority: real nonroot ancestor traversal and private leaf protection", flush=True)
        require(read()["package " + package]["state"] == "pending", "begin must fence package")
        first = authority_file.read_bytes()
        require(invoke("begin", package, "install-1", "absent") == generation,
                "exact begin replay must return the original generation")
        require(authority_file.read_bytes() == first, "exact replay changed authority contents")
        unchanged_failure("begin", "other.package", "install-1", "absent")
        unchanged_failure("commit", package, "empty-commit", generation)
        invoke("attach", package, "image.app", "attach-1", generation)
        invoke("attach", package, "image.second", "attach-2", generation)
        invoke("commit", package, "commit-1", generation)
        require(read()["image.app"]["state"] == "active" and
                read()["image.second"]["state"] == "active", "commit must activate both apps")
        print("PASS image authority: begin/attach/commit, exact replay and conflict", flush=True)

        unrelated = invoke("begin", "unrelated.package", "unrelated-begin", "absent")
        invoke("attach", "unrelated.package", "unrelated.app", "unrelated-attach", unrelated)
        invoke("commit", "unrelated.package", "unrelated-commit", unrelated)
        replacement = invoke("begin", package, "install-2", generation)
        require(replacement != generation, "replacement must receive a fresh generation")
        require(read()["image.app"]["state"] == "removed", "replacement must fence prior apps")
        unchanged_failure("attach", package, "image.app", "late-attach", generation)
        unchanged_failure("commit", package, "late-commit", generation)
        unchanged_failure("remove", package, "late-remove", generation)
        require(invoke("begin", package, "install-1", "absent") == generation,
                "old receipt must return original result")
        require(read()["package " + package]["generation"] == replacement,
                "old receipt restored obsolete generation")
        invoke("attach", package, "image.app", "attach-3", replacement)
        invoke("commit", package, "commit-2", replacement)
        invoke("remove", package, "remove-2", replacement)
        require(read()["package " + package]["state"] == "removed", "remove must persist tombstone")
        invoke("commit", package, "commit-2", replacement)
        invoke("commit", package, "commit-1", generation)
        require(read()["package " + package]["state"] == "removed" and
                read()["image.app"]["state"] == "removed", "old commit replay restored removed state")
        require(read()["unrelated.app"]["state"] == "active", "unrelated package changed")
        print("PASS image authority: replacement, stale rejection, tombstone and unrelated package", flush=True)

        # Preserve protected target group/mode without consulting target NSS.
        os.chown(authority_file, 0, 12345)
        authority_file.chmod(0o640)
        os.chown(authority, 0, 12345)
        authority.chmod(0o750)
        lifecycle = authority / "lifecycle.lock"
        os.chown(lifecycle, 0, 12345)
        lifecycle.chmod(0o640)
        third = invoke("begin", package, "install-3", replacement)
        info = authority_file.stat()
        require(info.st_uid == 0 and info.st_gid == 12345 and stat.S_IMODE(info.st_mode) == 0o640,
                "image update changed protected existing file ownership/mode")
        require(authority.stat().st_gid == 12345 and stat.S_IMODE(authority.stat().st_mode) == 0o750,
                "image open changed protected existing directory ownership/mode")
        with lifecycle.open("rb") as lock:
            fcntl.flock(lock, fcntl.LOCK_SH | fcntl.LOCK_NB)
            unchanged_failure("attach", package, "image.app", "held-lock", third)
        invoke("attach", package, "image.app", "after-lock", third)
        print("PASS image authority: protected metadata preservation and shared lifecycle lock refusal", flush=True)

        outside = base / "outside"
        outside.mkdir(mode=0o700)
        (outside / "sentinel").write_bytes(b"unchanged outside image\n")
        bad_image = base / "symlink-image"
        bad_image.mkdir(mode=0o700)
        (bad_image / relative.parts[0]).symlink_to(outside, target_is_directory=True)
        outside_before = snapshot(outside)
        invoke("begin", "escape.package", "escape", "absent", root=bad_image, success=False)
        require(snapshot(outside) == outside_before, "symlink traversal modified outside image")
        alias = base / "image-alias"
        alias.symlink_to(image, target_is_directory=True)
        before = snapshot(authority)
        invoke("begin", "escape.package", "root-alias", "absent", root=alias, success=False)
        require(snapshot(authority) == before, "symlink image root changed authority")
        saved = authority_file.read_bytes()
        authority_file.unlink()
        authority_file.symlink_to(outside / "sentinel")
        invoke("begin", "escape.package", "file-alias", "absent", success=False)
        require(snapshot(outside) == outside_before, "authority symlink modified outside image")
        authority_file.unlink()
        authority_file.write_bytes(saved)
        authority_file.chmod(0o600)
        hardlink = base / "authority-hardlink"
        os.link(authority_file, hardlink)
        unchanged_failure("begin", "escape.package", "file-hardlink", "absent")
        hardlink.unlink()
        require(not list(image.rglob("consent.db*")) and not list(image.rglob("definitions.registry*")),
                "generation CLI must not create policy DB or definition registry")
        print("PASS image authority: root/component/file symlink and hardlink rejection; no policy DB writes", flush=True)
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
