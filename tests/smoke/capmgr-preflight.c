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

#include <dlfcn.h>
#include <stdio.h>
#include <string.h>

/* capmgr.h defines opaque void* and permission denial -2. Load the actual
 * installed ABI without making the production consent RPM depend on CM. */
int main(int argc, char** argv) {
  if (argc > 2 || (argc == 2 && strcmp(argv[1], "--require-product"))) {
    fprintf(stderr, "Usage: %s [--require-product]\n", argv[0]);
    return 2;
  }
  int require_product = argc == 2;
  void* library = dlopen("libcapmgr.so.0", RTLD_NOW | RTLD_LOCAL);
  if (!library) {
    fprintf(stderr, "product_public_api/MISSING %s\n", dlerror());
    return 2;
  }
  int (*create)(void**) =
      (int (*)(void**))dlsym(library, "capmgr_client_create");
  int (*destroy)(void*) =
      (int (*)(void*))dlsym(library, "capmgr_client_destroy");
  if (!create || !destroy) {
    fputs("product_public_api/ABI_ERROR missing symbol\n", stderr);
    dlclose(library);
    return 2;
  }
  void* client = NULL;
  int status = create(&client);
  const char* category = status == -2 && !client
                             ? "BLOCKED"
                             : (status == 0 && client ? "AVAILABLE" : "ERROR");
  printf("product_public_api/%s create_status=%d handle=%s\n", category, status,
         client ? "present" : "null");
  int exit_status = !strcmp(category, "BLOCKED")
                        ? (require_product ? 1 : 3)
                        : (!strcmp(category, "AVAILABLE") ? 0 : 2);
  if (client) {
    int destroy_status = destroy(client);
    printf("product_public_api destroy_status=%d\n", destroy_status);
    if (destroy_status) exit_status = 2;
  }
  if (dlclose(library)) exit_status = 2;
  return exit_status;
}
