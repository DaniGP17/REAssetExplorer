#ifndef REASSETEXPLORER_PARALLELFOR_H
#define REASSETEXPLORER_PARALLELFOR_H
#include <algorithm>
#include <atomic>
#include <cstddef>
#include <exception>
#include <mutex>
#include <thread>
#include <vector>

// Runs work(i) for every i on all cores; the first exception stops the rest and is rethrown.
template <typename Work>
void ParallelFor(std::size_t count, Work&& work) {
    std::atomic<std::size_t> next{ 0 };
    std::exception_ptr error;
    std::mutex errorMutex;
    auto run = [&] {
        try {
            for (std::size_t i = next++; i < count; i = next++) work(i);
        } catch (...) {
            std::lock_guard lock(errorMutex);
            if (!error) error = std::current_exception();
            next = count;
        }
    };
    std::size_t threads = std::min<std::size_t>(count, std::max(1u, std::thread::hardware_concurrency()));
    std::vector<std::thread> workers;
    for (std::size_t i = 1; i < threads; i++) workers.emplace_back(run);
    run();
    for (std::thread& worker : workers) worker.join();
    if (error) std::rethrow_exception(error);
}

#endif
