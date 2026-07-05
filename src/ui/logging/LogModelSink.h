#pragma once

#include <mutex>

#include <spdlog/sinks/base_sink.h>

namespace geck {

class LogModel;

/// spdlog sink that forwards each record to a LogModel. Safe to install on any logger:
/// LogModel::appendRecord marshals cross-thread calls to the model's (GUI) thread itself.
/// Call detach() before the model is destroyed if the sink outlives it.
class LogModelSink : public spdlog::sinks::base_sink<std::mutex> {
public:
    explicit LogModelSink(LogModel* model);

    /// Stop forwarding records (the model is about to go away).
    void detach();

protected:
    void sink_it_(const spdlog::details::log_msg& msg) override;
    void flush_() override;

private:
    LogModel* _model; // guarded by base_sink's mutex_
};

} // namespace geck
