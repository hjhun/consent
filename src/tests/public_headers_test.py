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

"""Compile feature headers with only their public dependencies, in C and C++."""

import argparse
from pathlib import Path
import subprocess


FEATURES = {
    "consent_common.h": ("consent_error_string",),
    "consent_client.h": (
        "consent_client_create", "consent_client_create_with_context",
        "consent_client_create_offline_registration", "consent_client_destroy",
    ),
    "consent_params.h": (
        "consent_params_create", "consent_params_free", "consent_params_set",
        "consent_params_set_int64", "consent_params_set_check_mode",
        "consent_params_add_requirement",
    ),
    "consent_registration.h": (
        "consent_register", "consent_update", "consent_unregister", "consent_revoke",
    ),
    "consent_request.h": (
        "consent_request", "consent_check", "consent_request_async",
        "consent_check_async", "consent_async_detach", "consent_get_request_result",
        "consent_cancel_request",
    ),
    "consent_prompt.h": (
        "consent_get_prompt", "consent_respond", "consent_prompt_format",
    ),
    "consent_session.h": (
        "consent_session_open", "consent_session_suspend", "consent_session_resume",
        "consent_session_heartbeat", "consent_session_close", "consent_session_get_state",
    ),
    "consent_data.h": (
        "consent_data_register", "consent_data_register_derived", "consent_data_release",
        "consent_cleanup_get_state", "consent_cleanup_get_pending",
    ),
    "consent_result.h": (
        "consent_result_free", "consent_result_clone", "consent_result_get_decision",
        "consent_result_get", "consent_result_size", "consent_result_get_at",
    ),
}


def compile_consumer(compiler, language, standard, includes, headers, symbols):
    # Duplicate includes verify header guards. No private source include path or
    # preincluded umbrella may hide a missing public dependency.
    source = "".join("#include <{}>\n".format(name) for name in headers * 2)
    source += "int main(void) {\n"
    source += "".join("  (void)&{};\n".format(symbol) for symbol in symbols)
    source += "  return 0;\n}\n"
    command = [compiler, "-std=" + standard, "-Wall", "-Wextra", "-Werror", "-fsyntax-only"]
    for include in includes:
        command.extend(["-I", include])
    command.extend(["-x", language, "-"])
    result = subprocess.run(command, input=source, text=True, stdout=subprocess.PIPE,
                            stderr=subprocess.STDOUT, timeout=20)
    if result.returncode:
        raise RuntimeError("{} consumer {} failed:\n{}".format(
            language, ",".join(headers), result.stdout))


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--cc", required=True)
    parser.add_argument("--cxx", required=True)
    parser.add_argument("--public-dir", required=True)
    parser.add_argument("--include", action="append", default=[])
    args = parser.parse_args()
    public_dir = Path(args.public_dir).resolve(strict=True)
    expected = set(FEATURES) | {"consent.h"}
    actual = {path.name for path in public_dir.glob("*.h")}
    if actual != expected:
        raise RuntimeError("Public header coverage changed: expected {}, actual {}".format(
            sorted(expected), sorted(actual)))
    includes = [str(public_dir)] + args.include
    all_symbols = [symbol for symbols in FEATURES.values() for symbol in symbols]
    for compiler, language, standard in ((args.cc, "c", "c11"), (args.cxx, "c++", "c++17")):
        for header, symbols in FEATURES.items():
            compile_consumer(compiler, language, standard, includes, [header], symbols)
        compile_consumer(compiler, language, standard, includes, ["consent.h"], all_symbols)
        compile_consumer(compiler, language, standard, includes,
                         list(reversed(FEATURES)) + ["consent.h"], all_symbols)
        print("PASS {}: 10 standalone public headers, duplicate/reverse include order, {} declarations".format(
            language, len(all_symbols)), flush=True)


if __name__ == "__main__":
    main()
