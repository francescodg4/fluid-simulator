#include "fluid/ThreadPool.hpp"

#include <algorithm>

namespace fluid {

ThreadPool::ThreadPool(unsigned threads)
{
    const unsigned workers = std::max(1u, threads) - 1;
    m_workers.reserve(workers);
    for (unsigned i = 0; i < workers; ++i) {
        m_workers.emplace_back([this, i] { workerLoop(i + 1); });
    }
}

ThreadPool::~ThreadPool()
{
    {
        std::lock_guard lock(m_mutex);
        m_stop = true;
    }
    m_wake.notify_all();
    for (std::thread& t : m_workers) {
        t.join();
    }
}

ThreadPool& ThreadPool::shared()
{
    static ThreadPool pool;
    return pool;
}

void ThreadPool::parallelFor(std::size_t begin, std::size_t end, const RangeFn& fn, std::size_t grain)
{
    if (end <= begin) {
        return;
    }
    grain = std::max<std::size_t>(1, grain);
    if (m_workers.empty() || end - begin <= grain) {
        fn(begin, end, 0);
        return;
    }

    std::lock_guard call(m_callMutex);
    {
        std::lock_guard lock(m_mutex);
        m_fn = &fn;
        m_end = end;
        m_grain = grain;
        m_next.store(begin, std::memory_order_relaxed);
        m_busy = static_cast<unsigned>(m_workers.size());
        ++m_generation;
    }
    m_wake.notify_all();

    runChunks(0);

    std::unique_lock lock(m_mutex);
    m_done.wait(lock, [this] { return m_busy == 0; });
    m_fn = nullptr;
}

void ThreadPool::runChunks(unsigned workerIndex)
{
    for (;;) {
        const std::size_t b = m_next.fetch_add(m_grain, std::memory_order_relaxed);
        if (b >= m_end) {
            break;
        }
        (*m_fn)(b, std::min(b + m_grain, m_end), workerIndex);
    }
}

void ThreadPool::workerLoop(unsigned index)
{
    std::uint64_t seen = 0;
    for (;;) {
        std::unique_lock lock(m_mutex);
        m_wake.wait(lock, [&] { return m_stop || m_generation != seen; });
        if (m_stop) {
            return;
        }
        seen = m_generation;
        lock.unlock();

        runChunks(index);

        lock.lock();
        if (--m_busy == 0) {
            m_done.notify_one();
        }
    }
}

} // namespace fluid
