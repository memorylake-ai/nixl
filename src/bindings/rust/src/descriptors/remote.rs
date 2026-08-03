// SPDX-FileCopyrightText: Copyright (c) 2026 NVIDIA CORPORATION & AFFILIATES. All rights reserved.
// SPDX-License-Identifier: Apache-2.0
//
// Licensed under the Apache License, Version 2.0 (the "License");
// you may not use this file except in compliance with the License.
// You may obtain a copy of the License at
//
// http://www.apache.org/licenses/LICENSE-2.0
//
// Unless required by applicable law or agreed to in writing, software
// distributed under the License is distributed on an "AS IS" BASIS,
// WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
// See the License for the specific language governing permissions and
// limitations under the License.

use super::*;

/// A remote buffer descriptor, naming the agent that owns it. `None` is a
/// null-agent placeholder, which keeps descriptor indices aligned when only
/// some peers are being addressed.
#[derive(Debug)]
pub struct RemoteDescriptor<'a> {
    pub addr: usize,
    pub len: usize,
    pub dev_id: u64,
    pub remote_agent: Option<&'a str>,
}

/// A descriptor list for remote buffers, where each descriptor carries the name
/// of the agent that owns it. The lifetime binds the list to the descriptors it
/// was built from, as `RegDescList` binds to the buffers it describes.
#[derive(Debug)]
pub struct RemoteDescList<'a> {
    inner: NonNull<bindings::nixl_capi_remote_dlist_s>,
    _phantom: PhantomData<&'a RemoteDescriptor<'a>>,
}

impl<'a> RemoteDescList<'a> {
    pub fn new(mem_type: MemType) -> Result<Self, NixlError> {
        let mut dlist = ptr::null_mut();
        let status =
            unsafe { nixl_capi_create_remote_dlist(mem_type as nixl_capi_mem_type_t, &mut dlist) };

        match status {
            // SAFETY: on success dlist is non-null
            NIXL_CAPI_SUCCESS => Ok(Self {
                inner: unsafe { NonNull::new_unchecked(dlist) },
                _phantom: PhantomData,
            }),
            NIXL_CAPI_ERROR_INVALID_PARAM => Err(NixlError::InvalidParam),
            _ => Err(NixlError::BackendError),
        }
    }

    /// Adds a descriptor to the list. The borrow ties the list's lifetime to
    /// the descriptor, so the list cannot outlive what it was built from.
    pub fn add_desc(&mut self, descriptor: &'a RemoteDescriptor<'a>) -> Result<(), NixlError> {
        let c_agent = descriptor.remote_agent.map(CString::new).transpose()?;
        let status = unsafe {
            nixl_capi_remote_dlist_add_desc(
                self.inner.as_ptr(),
                descriptor.addr as uintptr_t,
                descriptor.len,
                descriptor.dev_id,
                c_agent.as_ref().map_or(ptr::null(), |name| name.as_ptr()),
            )
        };

        match status {
            NIXL_CAPI_SUCCESS => Ok(()),
            NIXL_CAPI_ERROR_INVALID_PARAM => Err(NixlError::InvalidParam),
            _ => Err(NixlError::BackendError),
        }
    }

    pub(crate) fn handle(&self) -> *mut bindings::nixl_capi_remote_dlist_s {
        self.inner.as_ptr()
    }
}

impl Drop for RemoteDescList<'_> {
    fn drop(&mut self) {
        let status = unsafe { nixl_capi_destroy_remote_dlist(self.inner.as_ptr()) };
        if status != NIXL_CAPI_SUCCESS {
            tracing::debug!(status, "Failed to destroy remote descriptor list");
        }
    }
}
