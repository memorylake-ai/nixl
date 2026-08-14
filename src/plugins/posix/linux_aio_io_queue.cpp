/*
 * SPDX-FileCopyrightText: Copyright (c) 2025-2026 NVIDIA CORPORATION & AFFILIATES. All rights reserved.
 * SPDX-License-Identifier: Apache-2.0
 *
 * Licensed under the Apache License, Version 2.0 (the "License");
 * you may not use this file except in compliance with the License.
 * You may obtain a copy of the License at
 *
 * http://www.apache.org/licenses/LICENSE-2.0
 *
 * Unless required by applicable law or agreed to in writing, software
 * distributed under the License is distributed on an "AS IS" BASIS,
 * WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
 * See the License for the specific language governing permissions and
 * limitations under the License.
 */

#include "io_queue.h"
#include <libaio.h>
#include "common/nixl_log.h"
#include <algorithm>
#include <cerrno>
#include <absl/strings/str_format.h>

#define MAX_IO_SUBMIT_BATCH_SIZE 64
#define MAX_IO_CHECK_COMPLETED_BATCH_SIZE 64

struct nixlPosixLinuxAioIO {
public:
    nixlPosixIOQueueDoneCb clb_;
    void *ctx_ = nullptr;
    size_t len_ = 0;
    bool in_flight_ = false;
    struct iocb io_;
};

class nixlPosixIOQueueLinuxAIO : public nixlPosixIOQueueImpl<nixlPosixLinuxAioIO> {
public:
    nixlPosixIOQueueLinuxAIO(uint32_t ios_pool_size, uint32_t kernel_queue_size);

    virtual nixl_status_t
    post(void) override;
    virtual nixl_status_t
    enqueue(int fd,
            void *buf,
            size_t len,
            off_t offset,
            bool read,
            nixlPosixIOQueueDoneCb clb,
            void *ctx) override;
    virtual nixl_status_t
    poll(void) override;
    virtual unsigned
    cancel(void *ctx, nixlPosixIOQueueCancelDoneCb clb) override;
    virtual ~nixlPosixIOQueueLinuxAIO() override;

protected:
    nixl_status_t
    doCheckCompleted(void);

private:
    void
    completeIO(nixlPosixLinuxAioIO *io, int64_t result);
    void
    failQueuedIOs(void *ctx);

    io_context_t io_ctx_; // I/O context
};

nixlPosixIOQueueLinuxAIO::nixlPosixIOQueueLinuxAIO(uint32_t ios_pool_size,
                                                   uint32_t kernel_queue_size)
    : nixlPosixIOQueueImpl<nixlPosixLinuxAioIO>(ios_pool_size, kernel_queue_size) {
    int res = io_queue_init(kernel_queue_size_, &io_ctx_);
    if (res) {
        throw std::runtime_error(
            absl::StrFormat("Failed to initialize io_queue: %s", nixl_strerror(-res)));
    }
}

nixl_status_t
nixlPosixIOQueueLinuxAIO::enqueue(int fd,
                                  void *buf,
                                  size_t len,
                                  off_t offset,
                                  bool read,
                                  nixlPosixIOQueueDoneCb clb,
                                  void *ctx) {
    if (free_ios_.empty()) {
        NIXL_ERROR << "No more free blocks available";
        return NIXL_ERR_NOT_ALLOWED;
    }
    nixlPosixLinuxAioIO *io = free_ios_.front();
    free_ios_.pop_front();

    if (read) {
        io_prep_pread(&io->io_, fd, buf, len, offset);
    } else {
        io_prep_pwrite(&io->io_, fd, buf, len, offset);
    }
    io->clb_ = clb;
    io->ctx_ = ctx;
    io->len_ = len;
    io->in_flight_ = false;
    io->io_.data = io;
    ios_to_submit_.push_back(io);

    return NIXL_SUCCESS;
}

nixlPosixIOQueueLinuxAIO::~nixlPosixIOQueueLinuxAIO() {
    io_queue_release(io_ctx_);
}

// Note: post() must return NIXL_IN_PROG in case of success.
nixl_status_t
nixlPosixIOQueueLinuxAIO::post(void) {
    struct iocb *ios[MAX_IO_SUBMIT_BATCH_SIZE];
    nixlPosixLinuxAioIO *to_submit[MAX_IO_SUBMIT_BATCH_SIZE];

    if (ios_to_submit_.empty()) {
        return NIXL_IN_PROG;
    }

    int num_ios = std::min(MAX_IO_SUBMIT_BATCH_SIZE, (int)ios_to_submit_.size());
    for (int i = 0; i < num_ios; i++) {
        nixlPosixLinuxAioIO *io = ios_to_submit_.front();
        ios_to_submit_.pop_front();

        ios[i] = &io->io_;
        to_submit[i] = io;
    }

    int ret = io_submit(io_ctx_, num_ios, ios);
    nixl_status_t status = NIXL_IN_PROG;
    if (ret < 0) {
        if (ret != -EAGAIN && ret != -EINTR) {
            NIXL_ERROR << "io_submit failed: " << nixl_strerror(-ret);
            status = NIXL_ERR_BACKEND;
        }
        ret = 0;
    }

    for (int i = 0; i < ret; i++) {
        to_submit[i]->in_flight_ = true;
    }

    for (int i = num_ios - 1; i >= ret; i--) {
        ios_to_submit_.push_front(to_submit[i]);
    }

    return status;
}

void
nixlPosixIOQueueLinuxAIO::completeIO(nixlPosixLinuxAioIO *io, int64_t result) {
    NIXL_ASSERT(io->in_flight_);
    io->in_flight_ = false;

    int error = result < 0 || static_cast<size_t>(result) != io->len_;
    if (error) {
        NIXL_DEBUG << absl::StrFormat(
            "AIO operation incomplete: result %ld, expected %zu", result, io->len_);
    }
    if (io->clb_) {
        io->clb_(io->ctx_, error ? 0 : static_cast<uint32_t>(result), error);
    }

    free_ios_.push_back(io);
}

inline nixl_status_t
nixlPosixIOQueueLinuxAIO::doCheckCompleted(void) {
    struct io_event events[MAX_IO_CHECK_COMPLETED_BATCH_SIZE];
    struct timespec timeout = {0, 0};

    if (free_ios_.size() == ios_pool_size_) {
        return NIXL_SUCCESS;
    }

    int rc = io_getevents(io_ctx_, 0, MAX_IO_CHECK_COMPLETED_BATCH_SIZE, events, &timeout);
    if (rc < 0) {
        if (rc == -EINTR) {
            return NIXL_IN_PROG;
        }
        NIXL_ERROR << "io_getevents error: " << nixl_strerror(-rc);
        return NIXL_ERR_BACKEND;
    }

    for (int i = 0; i < rc; i++) {
        auto *io = static_cast<nixlPosixLinuxAioIO *>(events[i].obj->data);
        completeIO(io, events[i].res);
    }

    return free_ios_.size() == ios_pool_size_ ? NIXL_SUCCESS : NIXL_IN_PROG;
}

void
nixlPosixIOQueueLinuxAIO::failQueuedIOs(void *ctx) {
    for (auto it = ios_to_submit_.begin(); it != ios_to_submit_.end();) {
        nixlPosixLinuxAioIO *io = *it;
        if (io->ctx_ != ctx) {
            ++it;
            continue;
        }

        if (io->clb_) {
            io->clb_(io->ctx_, 0, 1);
        }
        it = ios_to_submit_.erase(it);
        free_ios_.push_back(io);
    }
}

unsigned
nixlPosixIOQueueLinuxAIO::cancel(void *ctx, nixlPosixIOQueueCancelDoneCb) {
    if (!ctx) {
        return 0;
    }

    failQueuedIOs(ctx);

    for (auto &io : ios_) {
        if (!io.in_flight_ || io.ctx_ != ctx) {
            continue;
        }

        struct io_event event{};
        if (io_cancel(io_ctx_, &io.io_, &event) == 0) {
            // io_cancel returns the canceled operation's completion synchronously.
            completeIO(&io, event.res);
        }
        // If the kernel cannot cancel an operation, its ordinary completion callback will
        // retire it. Linux AIO never queues a separate asynchronous cancellation completion.
    }

    return 0;
}

nixl_status_t
nixlPosixIOQueueLinuxAIO::poll(void) {
    nixl_status_t submit_status = post();
    nixl_status_t completion_status = doCheckCompleted();

    return submit_status < 0 ? submit_status : completion_status;
}

std::unique_ptr<nixlPosixIOQueue>
nixlPosixIOQueueLinuxAIOCreate(uint32_t ios_pool_size, uint32_t kernel_queue_size) {
    return std::make_unique<nixlPosixIOQueueLinuxAIO>(ios_pool_size, kernel_queue_size);
}
