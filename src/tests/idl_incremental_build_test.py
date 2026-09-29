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
"""Rebuild both native endpoints after an isolated, valid IDL edit."""

import argparse
import hashlib
import json
from pathlib import Path
import shlex
import shutil
import subprocess
import sys
import tempfile
import time


ROOT = Path(__file__).resolve().parents[2]
IDL = Path("src/protocol/consent.idl.json")
GENERATOR = Path("src/tools/parcel_codegen.py")
HEADER = Path("generated/consent_wire.hh")
SOURCES = {
    "client": "src/consent/client.cc",
    "daemon": "src/consentd/server.cc",
}


def digest(path):
    return hashlib.sha256(path.read_bytes()).hexdigest()


def require(condition, description):
    if not condition:
        raise RuntimeError(description)


def run(command, timeout):
    print("COMMAND " + shlex.join(command), flush=True)
    result = subprocess.run(
        command, text=True, stdout=subprocess.PIPE,
        stderr=subprocess.STDOUT, timeout=timeout
    )
    print("EXIT {}".format(result.returncode), flush=True)
    if result.returncode:
        print(result.stdout, flush=True)
        raise RuntimeError("nested CMake command failed")
    return result.stdout


def object_entries(build):
    commands = json.loads((build / "compile_commands.json").read_text())
    result = {}
    for name, suffix in SOURCES.items():
        found = [
            row for row in commands if row["file"].endswith(suffix)
        ]
        require(len(found) == 1, "missing unique " + suffix + " object")
        result[name] = found[0]
    return result


def object_path(build, entry):
    output = Path(entry["output"])
    return output if output.is_absolute() else build / output


def commands_for(output, entries):
    lines = output.splitlines()
    for name, entry in entries.items():
        source = entry["file"]
        object_name = Path(entry["output"]).name
        commands = [
            line for line in lines
            if " -c " + source in line and "-o " in line
            and object_name in line
        ]
        require(len(commands) == 1, "no actual compile for " + name)
        print("COMPILED {} {}".format(name, commands[0]), flush=True)
    for name, ending in (
        ("client", "libconsent.so.0.1.0"),
        ("daemon", "consentd"),
    ):
        commands = []
        for line in lines:
            if " -o " not in line:
                continue
            arguments = shlex.split(line)
            if "-o" not in arguments:
                continue
            linked = arguments[arguments.index("-o") + 1]
            if linked.endswith(ending):
                commands.append(line)
        require(len(commands) == 1, "no actual link for " + name)
        print("LINKED {} {}".format(name, commands[0]), flush=True)


def check_license(header, schema):
    text = header.read_text()
    notice = schema["license"]["notice"].splitlines()
    require(schema["license"]["copyright"] in text,
            "generated copyright missing")
    require("Apache License, Version 2.0" in text,
            "generated Apache notice missing")
    for line in notice:
        if line.strip():
            require(line.strip() in text, "generated license notice changed")


def copy_source(destination):
    skipped = shutil.ignore_patterns(
        "consent-ui", "obj", "bin", "__pycache__", "*.pyc"
    )
    shutil.copytree(ROOT / "src", destination / "src", ignore=skipped)
    shutil.copytree(ROOT / "packaging", destination / "packaging")
    shutil.copytree(ROOT / "docs", destination / "docs")
    for name in ("CMakeLists.txt", "README.md", "AGENTS.md", "LICENSE"):
        shutil.copy2(ROOT / name, destination / name)


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--cmake", required=True)
    parser.add_argument("--cc", required=True)
    parser.add_argument("--cxx", required=True)
    parser.add_argument("--pkg-config", required=True)
    parser.add_argument("--c-flags", default="")
    parser.add_argument("--cxx-flags", default="")
    parser.add_argument("--exe-linker-flags", default="")
    parser.add_argument("--shared-linker-flags", default="")
    parser.add_argument("--build-type", default="")
    args = parser.parse_args()
    original_hashes = {name: digest(ROOT / name) for name in (IDL, GENERATOR)}

    with tempfile.TemporaryDirectory(prefix="consent-idl-build-") as temp:
        source = Path(temp) / "source"
        build = Path(temp) / "build"
        source.mkdir()
        copy_source(source)
        idl = source / IDL
        original_idl = idl.read_bytes()
        schema = json.loads(original_idl)
        configure = [
            args.cmake, "-S", str(source), "-B", str(build),
            "-DBUILD_TESTING=OFF", "-DCONSENT_BUILD_TOOLS=OFF",
            "-DCONSENT_BUILD_TEST_DAEMON=OFF", "-DCONSENT_BUILD_POC=OFF",
            "-DCMAKE_EXPORT_COMPILE_COMMANDS=ON",
            "-DCMAKE_C_COMPILER=" + args.cc,
            "-DCMAKE_CXX_COMPILER=" + args.cxx,
            "-DPKG_CONFIG_EXECUTABLE=" + args.pkg_config,
            "-DCMAKE_C_FLAGS=" + args.c_flags,
            "-DCMAKE_CXX_FLAGS=" + args.cxx_flags,
            "-DCMAKE_EXE_LINKER_FLAGS=" + args.exe_linker_flags,
            "-DCMAKE_SHARED_LINKER_FLAGS=" + args.shared_linker_flags,
            "-DCMAKE_BUILD_TYPE=" + args.build_type,
        ]
        run(configure, 60)
        build_command = [
            args.cmake, "--build", str(build), "--target", "consent",
            "consentd", "--parallel", "2", "--verbose"
        ]
        first_log = run(build_command, 180)
        entries = object_entries(build)
        commands_for(first_log, entries)
        header = build / HEADER
        baseline_header = header.read_bytes()
        baseline_objects = {
            name: object_path(build, entry).stat().st_mtime_ns
            for name, entry in entries.items()
        }
        require(baseline_header, "empty initial generated header")

        edited = json.loads(original_idl)
        field = edited["records"][0]["fields"][0]
        require(field["name"] == "key" and field["max_bytes"] == 128,
                "unexpected IDL field baseline")
        field["max_bytes"] = 127
        time.sleep(1.1)
        idl.write_text(json.dumps(edited, indent=2) + "\n")
        second_log = run(build_command, 180)
        commands_for(second_log, entries)
        changed_header = header.read_bytes()
        require(changed_header != baseline_header,
                "valid IDL edit did not regenerate header")
        check_license(header, schema)
        for name, entry in entries.items():
            require(object_path(build, entry).stat().st_mtime_ns >
                    baseline_objects[name],
                    "endpoint object did not change after edit: " + name)
        print("PASS edited IDL generated and both endpoints recompiled/linked",
              flush=True)

        time.sleep(1.1)
        idl.write_bytes(original_idl)
        third_log = run(build_command, 180)
        commands_for(third_log, entries)
        require(idl.read_bytes() == original_idl,
                "temporary IDL restore changed bytes")
        require(header.read_bytes() == baseline_header,
                "generated header did not return to baseline bytes")
        print("PASS restored IDL and generated header exact bytes", flush=True)

    for name, expected in original_hashes.items():
        require(digest(ROOT / name) == expected,
                "real source input changed: " + str(name))
    print("PASS real IDL and generator inputs unchanged", flush=True)


if __name__ == "__main__":
    try:
        main()
    except (OSError, ValueError, RuntimeError, subprocess.TimeoutExpired) as e:
        print("FAIL edited-IDL build: {}".format(e), file=sys.stderr)
        sys.exit(1)
