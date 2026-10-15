#pragma once
#include <atomic>
#include <algorithm>
#include <condition_variable>
#include <functional>
#include <memory>
#include <mutex>
#include <queue>
#include <thread>
#include <vector>

namespace perlin {
    inline unsigned HardwareThreads() {
        return std::max(1u, std::thread::hardware_concurrency());
    }

    class ThreadPool {
    public:
        explicit ThreadPool(size_t count) {
            for (size_t i = 0; i < count; ++i)
                workers.emplace_back([this] { Run(); });
        }

        ~ThreadPool() { Stop(); }

        void Submit(std::function<void()> job) {
            {
                std::lock_guard<std::mutex> lock(mutex);
                jobs.push(std::move(job));
            }
            cv.notify_one();
        }

        size_t Size() const { return workers.size(); }

        void ParallelFor(size_t count, const std::function<void(size_t)>& body) {
            if (count == 0)
                return;
            if (count == 1 || workers.empty()) {
                for (size_t i = 0; i < count; i++)
                    body(i);
                return;
            }
            struct Shared {
                std::atomic<size_t> next{0};
                std::atomic<size_t> finished{0};
                std::mutex m;
                std::condition_variable cv;
            };
            auto shared = std::make_shared<Shared>();
            const size_t helpers = std::min(workers.size(), count - 1);
            const auto drain = [shared, count, &body] {
                for (size_t i = shared->next.fetch_add(1); i < count; i = shared->next.fetch_add(1))
                    body(i);
            };
            for (size_t h = 0; h < helpers; h++)
                Submit([shared, drain] {
                    drain();
                    {
                        std::lock_guard<std::mutex> lock(shared->m);
                        shared->finished++;
                    }
                    shared->cv.notify_all();
                });
            drain();
            std::unique_lock<std::mutex> lock(shared->m);
            shared->cv.wait(lock, [&] { return shared->finished.load() >= helpers; });
        }

        void Stop() {
            {
                std::lock_guard<std::mutex> lock(mutex);
                stopping = true;
            }
            cv.notify_all();
            for (auto& t : workers)
                if (t.joinable())
                    t.join();
            workers.clear();
        }

    private:
        void Run() {
            for (;;) {
                std::function<void()> job;
                {
                    std::unique_lock<std::mutex> lock(mutex);
                    cv.wait(lock, [&] { return stopping || !jobs.empty(); });
                    if (stopping && jobs.empty())
                        return;
                    job = std::move(jobs.front());
                    jobs.pop();
                }
                job();
            }
        }

        std::vector<std::thread> workers;
        std::queue<std::function<void()>> jobs;
        std::mutex mutex;
        std::condition_variable cv;
        bool stopping = false;
    };
}
