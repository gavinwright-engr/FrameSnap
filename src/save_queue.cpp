#include "save_queue.h"

SaveQueue::~SaveQueue() { Stop(); }

void SaveQueue::Stop() {
    {
        std::scoped_lock lock(mutex_);
        stopping_ = true;
    }
    // Accepted saves finish; the worker never waits for the UI thread.
    if (thread_.joinable()) thread_.join();
}

bool SaveQueue::Enqueue(const SaveJob& job, HWND notifyWindow) {
    if (job.image == nullptr || !job.image->IsValid()) return false;
    const auto bytes = job.image->pixels.size();
    std::unique_lock lock(mutex_);
    if (stopping_ || bytes > kMaxQueuedBytes || queuedBytes_ > kMaxQueuedBytes - bytes) return false;
    if (!active_ && thread_.joinable()) {
        lock.unlock();
        thread_.join();
        lock.lock();
    }
    queue_.push(job);
    queuedBytes_ += bytes;
    if (!active_) {
        active_ = true;
        thread_ = std::thread([this, notifyWindow] { ThreadMain(notifyWindow); });
    }
    return true;
}

void SaveQueue::ThreadMain(HWND notifyWindow) {
    const HRESULT initialized = CoInitializeEx(nullptr, COINIT_MULTITHREADED);
    {
        ImageIo imageIo; // Release COM objects before CoUninitialize.
        for (;;) {
            SaveJob job;
            {
                std::scoped_lock lock(mutex_);
                if (queue_.empty()) {
                    active_ = false;
                    break;
                }
                job = std::move(queue_.front());
                queue_.pop();
            }
            bool saved = false;
            try {
                saved = SUCCEEDED(initialized) && imageIo.SavePng(*job.image, job.path);
            } catch (const std::exception&) {
                // Allocation/filesystem failures must report a failed save, not terminate the app.
            }
            {
                std::scoped_lock lock(mutex_);
                queuedBytes_ -= job.image->pixels.size();
            }
            PostMessageW(notifyWindow, WM_APP_SAVE_COMPLETED, saved ? 1 : 0, 0);
        }
    }
    if (SUCCEEDED(initialized)) CoUninitialize();
}
