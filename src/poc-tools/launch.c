/*
 * Copyright (c) 2026 Samsung Electronics Co., Ltd. All Rights Reserved
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
#include <app_control.h>
#include <stdio.h>
#include <string.h>

int main(int argc, char **argv) {
  if (argc != 4 ||
      (strcmp(argv[1], "org.tizen.consentui") &&
       strcmp(argv[1], "org.tizen.consentui.negative")) ||
      !argv[2][0] || strlen(argv[2]) > 128 ||
      (strcmp(argv[3], "en-US") && strcmp(argv[3], "ko-KR")))
    return 2;
  app_control_h control = NULL;
  int status = app_control_create(&control);
  if (!status) status = app_control_set_app_id(control, argv[1]);
  if (!status) status = app_control_set_operation(control, APP_CONTROL_OPERATION_VIEW);
  if (!status) status = app_control_add_extra_data(control, "request_id", argv[2]);
  if (!status) status = app_control_add_extra_data(control, "locale", argv[3]);
  if (!status) status = app_control_send_launch_request(control, NULL, NULL);
  if (control) app_control_destroy(control);
  printf("event=launch-submitted status=%d\n", status);
  return status ? 1 : 0;
}
