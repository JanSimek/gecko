#ifndef GECK_MAPPER_LOADER_H
#define GECK_MAPPER_LOADER_H

#include <atomic>
#include <thread>
#include <shared_mutex>

namespace geck {

class Loader {

protected:
    std::thread _thread;
    std::shared_mutex _mutex;

    std::atomic<int> _percentDone = 0;
    std::string _status;
    std::string _progress;

public:
    virtual ~Loader() = default;

    virtual void init() = 0;
    virtual void load() = 0;
    virtual bool isDone() = 0;
    virtual void onDone() = 0;

    const std::string& status() {
        std::shared_lock lock(_mutex);
        return _status;
    }

    const std::string& progress() {
        std::shared_lock lock(_mutex);
        return _progress;
    }

    void setStatus(const std::string& status) {
        this->_status = status;
    }

    void setProgress(const std::string& progress) {
        this->_progress = progress;
    }

    int percentDone() const {
        return this->_percentDone;
    }
};

} // namespace geck

#endif // GECK_MAPPER_LOADER_H
