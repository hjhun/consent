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

#include "tool-process.h"

#include <errno.h>
#include <fcntl.h>
#include <signal.h>
#include <stdio.h>
#include <string.h>
#include <sys/wait.h>
#include <unistd.h>

#include "common.h"

int main(int argc, char** argv) {
  if (argc > 1 && !strcmp(argv[1], "--json")) {
    for (;;)
      pause();
  }
  int executable = open("/proc/self/exe", O_RDONLY | O_CLOEXEC);
  smoke_expect(executable >= 0, "self provider inode");
  tool_process_test_fail_nonblocking();
  char* response = NULL;
  int status = -1;
  int result = tool_execute_capture(executable, "{}", "fault", &response,
                                    &status, 0);
  close(executable);
  smoke_expect(result == 0 && response == NULL, "setup execution failed");
  smoke_expect(WIFSIGNALED(status) && WTERMSIG(status) == SIGKILL,
               "setup failure kills provider");
  pid_t pid = tool_process_test_pid();
  smoke_expect(pid > 0, "provider was actually spawned");
  errno = 0;
  smoke_expect(kill(pid, 0) == -1 && errno == ESRCH, "provider gone");
  errno = 0;
  smoke_expect(waitpid(pid, NULL, WNOHANG) == -1 && errno == ECHILD,
               "provider already reaped");
  puts("PASS nonblocking failure killed and reaped provider");
  return 0;
}
