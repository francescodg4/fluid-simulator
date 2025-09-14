#pragma once

#include <atomic>
#include <condition_variable>
#include <cstddef>
#include <functional>
#include <mutex>
#include <thread>
#include <vector>

namespace fluid {

/**
 * Minimal fork-join pool tuned for data-parallel loops (one parallelFor at a time).
 * The calling thread participates, so size() == workers + 1.
 */
class ThreadPool {
public:
    /** fn(begin, end, workerIndex) with workerIndex in [0, size()). */
    using RangeFn = std::function<void(std::size_t, std::size_t, unsigned)>;

    explicit ThreadPool(unsigned threads = std::thread::hardware_concurrency());
    ~ThreadPool();

    ThreadPool(const ThreadPool&) = delete;
    ThreadPool& operator=(const ThreadPool&) = delete;

    unsigned size() const { return static_cast<unsigned>(m_workers.size()) + 1; }

    /** Splits [begin, end) into chunks of @p grain and blocks until every chunk has run. */
    void parallelFor(std::size_t begin, std::size_t end, const RangeFn& fn, std::size_t grain = 1);

    static ThreadPool& shared();

private:
    void workerLoop(unsigned index);
    void runChunks(unsigned workerIndex);

    std::vector<std::thread> m_workers;
    std::mutex m_callMutex;
    std::mutex m_mutex;
    std::condition_variable m_wake;
    std::condition_variable m_done;
    std::uint64_t m_generation = 0;
    unsigned m_busy = 0;
    bool m_stop = false;

    const RangeFn* m_fn = nullptr;
    std::size_t m_end = 0;
    std::size_t m_grain = 1;
    std::atomic<std::size_t> m_next { 0 };
};

} // namespace fluid
