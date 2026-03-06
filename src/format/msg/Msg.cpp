#include "Msg.h"

namespace geck {

const Msg::Message& Msg::message(int id) {
    return _messages[id];
}

const std::map<int, Msg::Message>& Msg::getMessages() const {
    return _messages;
}

} // namespace geck
