#pragma once

#include "common.h"
#include "image_io.h"
#include "types.h"

class SaveQueue {
public:
    SaveQueue();
    ~SaveQueue();

    void Start();
    void Stop();
    bool Enqueue(const SaveJob& job);

private:
    void ThreadMain();

    static constexpr std::size_t kMaxQueuedBytes = 1024ULL * 1024ULL * 1024ULL;

    std::atomic<bool> running_{false};
    std::atomic<bool> warned_{false};
    std::thread thread_;
    std::mutex mutex_;
    std::condition_variable cv_;
    std::queue<SaveJob> queue_;
    std::size_t queuedBytes_{};
    ImageIo imageIo_;
};
