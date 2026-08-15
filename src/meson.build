# SPDX-FileCopyrightText: Copyright (c) 2025 NVIDIA CORPORATION & AFFILIATES. All rights reserved.
# SPDX-License-Identifier: Apache-2.0
#
# Licensed under the Apache License, Version 2.0 (the "License");
# you may not use this file except in compliance with the License.
# You may obtain a copy of the License at
#
# http://www.apache.org/licenses/LICENSE-2.0
#
# Unless required by applicable law or agreed to in writing, software
# distributed under the License is distributed on an "AS IS" BASIS,
# WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
# See the License for the specific language governing permissions and
# limitations under the License.

subdir('api')

subdir('utils')

#subdir('arch')
subdir('infra')
subdir('plugins')
subdir('core')

# Test utilities library - created after nixl_dep is available to avoid circular dependencies.
# Left as a disabler when unavailable so that consumers (tests, examples) are skipped
# instead of failing configuration on an undefined variable.
nixl_test_utils_dep = disabler()
if get_option('build_test_utils')
  nixl_test_utils_lib = library('nixl_test_utils',
                               'infra/test_utils.cpp',
                               include_directories: [ nixl_inc_dirs, utils_inc_dirs ],
                               dependencies: [nixl_dep, nixl_common_dep, absl_log_dep],
                               install: true)

  nixl_test_utils_dep = declare_dependency(
      include_directories: include_directories('infra'),
      link_with: nixl_test_utils_lib,
      dependencies: [nixl_dep, nixl_common_dep]
  )
endif

if get_option('build_python_bindings') or get_option('build_c_api_binding') or get_option('rust')
  subdir('bindings')
endif
