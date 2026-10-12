// Cooperative cancellation for callback-based async chains (Swift's Task
// cancellation, translated). A token is shared by every step of one operation;
// cancel() aborts the in-flight reply and every later step checks the flag.
#pragma once

#include <QNetworkReply>
#include <QPointer>
#include <memory>

namespace jot {

struct CancelState {
    bool cancelled = false;
    QPointer<QNetworkReply> current;
    void cancel()
    {
        cancelled = true;
        if (current) current->abort();
    }
};
using CancelToken = std::shared_ptr<CancelState>;
inline CancelToken makeToken() { return std::make_shared<CancelState>(); }

} // namespace jot
