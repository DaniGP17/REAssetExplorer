#ifndef REASSETEXPLORER_ASYNCLOADS_H
#define REASSETEXPLORER_ASYNCLOADS_H
#include <atomic>
#include <condition_variable>
#include <deque>
#include <functional>
#include <future>
#include <memory>
#include <mutex>
#include <string>
#include <thread>
#include <unordered_map>
#include <vector>

// Worker threads for loads that may queue further loads. Background tasks run only when no
// foreground task is waiting. Stop drops the tasks that have not started.
class LoadPool {
public:
    LoadPool();
    ~LoadPool();
    LoadPool(const LoadPool&) = delete;
    LoadPool& operator=(const LoadPool&) = delete;

    void Run(std::function<void()> task, bool background = false);
    void Stop();

private:
    void Work();

    std::mutex mutex;
    std::condition_variable wake;
    std::deque<std::function<void()>> foreground;
    std::deque<std::function<void()>> background;
    bool stopping = false;
    std::vector<std::thread> workers;
};

// Values keyed by path, each loaded once on a pool. A caller waiting for a load that has not
// started runs it itself.
template <typename T>
class AsyncLoads {
public:
    // False when the key was already requested.
    bool Request(LoadPool& pool, const std::string& key, std::function<T()> load, bool background = false) {
        auto task = std::make_shared<Task>(std::move(load));
        {
            std::lock_guard lock(mutex);
            if (!entries.emplace(key, Entry{ task, task->work.get_future().share() }).second) return false;
        }
        pool.Run([task] { task->Run(); }, background);
        return true;
    }

    // Waits for the load; nullptr when the key was never requested. Rethrows the load's error.
    std::shared_ptr<T> Get(const std::string& key) {
        Entry entry;
        {
            std::lock_guard lock(mutex);
            auto it = entries.find(key);
            if (it == entries.end()) return nullptr;
            entry = it->second;
        }
        return Wait(entry);
    }

    // Get for a value used once: later calls for the key return nullptr.
    std::shared_ptr<T> Take(const std::string& key) {
        Entry entry;
        {
            std::lock_guard lock(mutex);
            auto it = entries.find(key);
            if (it == entries.end() || !it->second.task) return nullptr;
            entry = std::move(it->second);
            it->second = {};
        }
        return Wait(entry);
    }

private:
    struct Task {
        explicit Task(std::function<T()> load)
            : work([load = std::move(load)] { return std::make_shared<T>(load()); }) {}
        void Run() {
            if (!claimed.exchange(true)) work();
        }
        std::atomic<bool> claimed{ false };
        std::packaged_task<std::shared_ptr<T>()> work;
    };
    struct Entry {
        std::shared_ptr<Task> task;
        std::shared_future<std::shared_ptr<T>> result;
    };

    static std::shared_ptr<T> Wait(Entry& entry) {
        entry.task->Run();
        return entry.result.get();
    }

    std::mutex mutex;
    std::unordered_map<std::string, Entry> entries;
};

#endif
