#include "LogModelSink.h"

#include <chrono>

#include <QDateTime>
#include <QString>

#include "LogModel.h"

namespace geck {

namespace {

    LogModel::Level toModelLevel(spdlog::level::level_enum level) {
        switch (level) {
            case spdlog::level::trace:
            case spdlog::level::debug:
                return LogModel::Level::Debug;
            case spdlog::level::info:
                return LogModel::Level::Info;
            case spdlog::level::warn:
                return LogModel::Level::Warning;
            default:
                return LogModel::Level::Error;
        }
    }

} // namespace

LogModelSink::LogModelSink(LogModel* model)
    : _model(model) {
}

void LogModelSink::detach() {
    std::lock_guard<std::mutex> lock(mutex_);
    _model = nullptr;
}

void LogModelSink::sink_it_(const spdlog::details::log_msg& msg) {
    if (!_model) {
        return;
    }

    const auto msecs = std::chrono::duration_cast<std::chrono::milliseconds>(msg.time.time_since_epoch()).count();
    _model->appendRecord(toModelLevel(msg.level),
        QString::fromUtf8(msg.payload.data(), static_cast<qsizetype>(msg.payload.size())),
        QDateTime::fromMSecsSinceEpoch(msecs));
}

void LogModelSink::flush_() {
}

} // namespace geck
