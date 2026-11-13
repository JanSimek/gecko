#pragma once

#include <catch2/catch_test_macros.hpp>

#include <QAbstractButton>
#include <QApplication>
#include <QMessageBox>
#include <QTimer>

#include <chrono>
#include <exception>

namespace geck::test {

/// Clicks `button` on the next message box Qt shows while this object lives, so a confirmation the
/// code under test blocks on is answered without a human. Create it before the action that raises
/// the box: it polls from the event loop, which the box's own modal loop keeps running. When it goes
/// out of scope without having seen a box, the test fails — the confirmation it was meant to answer
/// never came.
class ScopedDialogAnswer {
public:
    explicit ScopedDialogAnswer(QMessageBox::StandardButton button,
        std::chrono::milliseconds timeout = std::chrono::seconds(5))
        : _button(button)
        , _deadline(std::chrono::steady_clock::now() + timeout) {
        _poll.setInterval(std::chrono::milliseconds(10));
        QObject::connect(&_poll, &QTimer::timeout, &_poll, [this]() { answerIfShown(); });
        _poll.start();
    }

    ~ScopedDialogAnswer() {
        _poll.stop();
        // Not while unwinding from a failed REQUIRE: that failure is the one worth reading.
        if (!_answered && std::uncaught_exceptions() == 0) {
            FAIL_CHECK("no message box appeared for ScopedDialogAnswer to answer");
        }
    }

    ScopedDialogAnswer(const ScopedDialogAnswer&) = delete;
    ScopedDialogAnswer& operator=(const ScopedDialogAnswer&) = delete;

    bool answered() const { return _answered; }

private:
    void answerIfShown() {
        for (QWidget* widget : QApplication::topLevelWidgets()) {
            auto* box = qobject_cast<QMessageBox*>(widget);
            if (box == nullptr || !box->isVisible()) {
                continue;
            }
            if (QAbstractButton* target = box->button(_button)) {
                _answered = true;
                _poll.stop();
                target->click();
                return;
            }
        }
        if (std::chrono::steady_clock::now() >= _deadline) {
            _poll.stop();
            // A box without the wanted button would block forever: dismiss it so the test fails
            // (in the destructor) instead of hanging the suite.
            for (QWidget* widget : QApplication::topLevelWidgets()) {
                if (auto* box = qobject_cast<QMessageBox*>(widget); box != nullptr && box->isVisible()) {
                    box->reject();
                }
            }
        }
    }

    QMessageBox::StandardButton _button;
    std::chrono::steady_clock::time_point _deadline;
    QTimer _poll;
    bool _answered = false;
};

} // namespace geck::test
