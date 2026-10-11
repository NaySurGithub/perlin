#pragma once
#include <atomic>
#include <condition_variable>
#include <functional>
#include <mutex>
#include <queue>
#include <thread>
#include <vector>

namespace perlin {
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
