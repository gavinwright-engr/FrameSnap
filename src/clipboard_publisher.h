#pragma once

#include "common.h"
#include "image_io.h"
#include "types.h"

class ClipboardPublisher {
public:
    ClipboardPublisher();
    ~ClipboardPublisher();

    bool Start();
    void Stop();
    bool PublishBlocking(const std::shared_ptr<ImageData>& image);

private:
    struct Request {
        std::shared_ptr<ImageData> image;
        HANDLE doneEvent{nullptr};
        bool success{};
    };

    void ThreadMain();
    bool PublishNow(const std::shared_ptr<ImageData>& image);
    HGLOBAL CreateDibv5(const ImageData& image) const;

    std::atomic<bool> running_{false};
    std::thread thread_;
    std::mutex mutex_;
    std::condition_variable cv_;
    std::queue<std::shared_ptr<Request>> queue_;
    ImageIo imageIo_;
};
