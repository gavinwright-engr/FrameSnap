#include "save_queue.h"

SaveQueue::SaveQueue() = default;

SaveQueue::~SaveQueue() {
    Stop();
}

void SaveQueue::Start() {
    if (running_.exchange(true)) {
        return;
    }
    thread_ = std::thread([this] { ThreadMain(); });
}

void SaveQueue::Stop() {
    if (!running_.exchange(false)) {
        return;
    }
    cv_.notify_all();
    if (thread_.joinable()) {
        thread_.join();
    }
}

bool SaveQueue::Enqueue(const SaveJob& job) {
    if (job.image == nullptr) {
        return false;
    }
    if (!running_) {
        Start();
    }
    const auto bytes = job.image->pixels.size();
    {
        std::scoped_lock lock(mutex_);
        if (bytes > kMaxQueuedBytes || queuedBytes_ > kMaxQueuedBytes - bytes) {
            if (!warned_.exchange(true)) {
                MessageBeep(MB_ICONWARNING);
            }
            return false;
        }
        queue_.push(job);
        queuedBytes_ += bytes;
    }
    cv_.notify_one();
    return true;
}

void SaveQueue::ThreadMain() {
    const HRESULT coInitializeResult = CoInitializeEx(nullptr, COINIT_MULTITHREADED);
    const bool coInitialized = SUCCEEDED(coInitializeResult);
    for (;;) {
        SaveJob job;
        {
            std::unique_lock lock(mutex_);
            cv_.wait(lock, [this] { return !running_ || !queue_.empty(); });
            if (!running_ && queue_.empty()) {
                break;
            }
            job = queue_.front();
            queue_.pop();
            if (job.image != nullptr) {
                queuedBytes_ -= job.image->pixels.size();
            }
        }
        if (job.image != nullptr) {
            imageIo_.SavePng(*job.image, job.path);
        }
    }
    if (coInitialized) {
        CoUninitialize();
    }
}
