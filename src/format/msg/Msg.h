#pragma once

#include <map>
#include <string>
#include <filesystem>

#include "../IFile.h"

namespace geck {

class Msg : public IFile {
public:
    struct Message {
        int id;
        std::string audio;
        std::string text;
    };

    Msg(std::filesystem::path path, std::map<int, Message> messages)
        : IFile(path)
        , _messages(messages) { }

    const Message& message(int id);
    const std::map<int, Message>& getMessages() const;

private:
    std::map<int, Message> _messages;
};

} // namespace
