#include "Explorer/AsyncLoads.h"

#include <algorithm>

LoadPool::LoadPool() {
    unsigned count = std::max(1u, std::thread::hardware_concurrency());
    for (unsigned i = 0; i < count; i++) workers.emplace_back([this] { Work(); });
}

LoadPool::~LoadPool() {
    Stop();
}

void LoadPool::Run(std::function<void()> task, bool inBackground) {
    {
        std::lock_guard lock(mutex);
        if (stopping) return;
        (inBackground ? background : foreground).push_back(std::move(task));
    }
    wake.notify_one();
}

void LoadPool::Stop() {
    {
        std::lock_guard lock(mutex);
        stopping = true;
        foreground.clear();
        background.clear();
    }
    wake.notify_all();
    for (std::thread& worker : workers) {
        if (worker.joinable()) worker.join();
    }
}

void LoadPool::Work() {
    for (;;) {
        std::function<void()> task;
        {
            std::unique_lock lock(mutex);
            wake.wait(lock, [this] { return stopping || !foreground.empty() || !background.empty(); });
            if (stopping) return;
            std::deque<std::function<void()>>& queue = foreground.empty() ? background : foreground;
            task = std::move(queue.front());
            queue.pop_front();
        }
        task();
    }
}
