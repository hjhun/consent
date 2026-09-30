/*
 * Copyright (c) 2026 Samsung Electronics Co., Ltd. All rights reserved.
 *
 * Licensed under the Apache License, Version 2.0 (the "License");
 * you may not use this file except in compliance with the License.
 * You may obtain a copy of the License at
 *
 *     http://www.apache.org/licenses/LICENSE-2.0
 *
 * Unless required by applicable law or agreed to in writing, software
 * distributed under the License is distributed on an "AS IS" BASIS,
 * WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
 * See the License for the specific language governing permissions and
 * limitations under the License.
 */

#ifndef CONSENT_TESTS_SMOKE_TOOL_PROCESS_H_
#define CONSENT_TESTS_SMOKE_TOOL_PROCESS_H_

/* Takes a verified executable FD; always reaps the provider and drains pipes.
 * Returns 1 success, -1 native JSON-RPC error, 0 unknown/failed execution. */
int tool_execute(int executable_fd, const char* request, const char* id);
/* Allocates a validated native reply, or NULL; caller frees with g_free. */
int tool_execute_capture(int executable_fd, const char* request, const char* id,
                         char** response, int* wait_status, int verbose);
void tool_install_signal_handlers(void);
int tool_interrupted(void);
int tool_open(const char* path, int executable);

#ifdef TOOL_PROCESS_TESTING
#include <sys/types.h>
void tool_process_test_fail_nonblocking(void);
pid_t tool_process_test_pid(void);
#endif

#endif  // CONSENT_TESTS_SMOKE_TOOL_PROCESS_H_
