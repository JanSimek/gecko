#include "ssl/Corpus.h"

#include <algorithm>
#include <atomic>
#include <exception>
#include <mutex>
#include <thread>
#include <vector>

namespace geck::ssl {

void loadScripts(const SourceProvider& provider, const std::vector<std::filesystem::path>& paths, const ScriptVisitor& visit,
    const PreprocessOptions& options) {
    std::atomic<std::size_t> next{ 0 };
    std::atomic<bool> failed{ false };
    std::exception_ptr error;
    std::mutex errorMutex;

    const auto work = [&next, &failed, &error, &errorMutex, &paths, &provider, &options, &visit]() {
        while (!failed) {
            const std::size_t index = next++;
            if (index >= paths.size()) {
                return;
            }
            try {
                visit(index, ScriptUnit::load(provider, paths[index], options));
            } catch (...) {
                const std::scoped_lock lock(errorMutex);
                if (!error) {
                    error = std::current_exception();
                }
                failed = true;
            }
        }
    };

    const std::size_t threads = std::clamp<std::size_t>(std::thread::hardware_concurrency(), 1, std::max<std::size_t>(paths.size(), 1));
    std::vector<std::thread> workers;
    for (std::size_t i = 1; i < threads; ++i) {
        workers.emplace_back(work);
    }
    work(); // this thread works too
    for (std::thread& worker : workers) {
        worker.join();
    }
    if (error) {
        std::rethrow_exception(error);
    }
}

} // namespace geck::ssl
