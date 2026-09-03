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

# The version lives in project() and must not be restated by hand, so substitute it into a public
# header here. The output lands in this directory's build dir, which nixl_inc_dirs already puts on
# the include path, so nixl.h finds it both in-tree and from the install prefix.
nixl_version_parts = meson.project_version().split('.')
nixl_version_h = configure_file(
    input: 'nixl_version.h.in',
    output: 'nixl_version.h',
    configuration: {
        'NIXL_VERSION_MAJOR': nixl_version_parts[0],
        'NIXL_VERSION_MINOR': nixl_version_parts[1],
        'NIXL_VERSION_PATCH': nixl_version_parts[2],
        'NIXL_VERSION_STRING': meson.project_version(),
    },
)
