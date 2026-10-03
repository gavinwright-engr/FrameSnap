#pragma once

#include "image_io.h"

class SaveQueue {
public:
    ~SaveQueue();
    void Stop();
    // Called on the UI thread. Completion is posted without sharing mutable pixels.
    bool Enqueue(const SaveJob& job, HWND notifyWindow);

private:
    void ThreadMain(HWND notifyWindow);
    // Includes the in-flight image, not just jobs waiting in the queue.
    static constexpr std::size_t kMaxQueuedBytes = 128ULL * 1024 * 1024;
    std::thread thread_;
    std::mutex mutex_;
    std::queue<SaveJob> queue_;
    std::size_t queuedBytes_{};
    bool active_{};
    bool stopping_{};
};
