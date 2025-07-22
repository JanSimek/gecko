#include "MsgReader.h"

#include <regex>
#include <streambuf>

namespace geck {

std::unique_ptr<Msg> MsgReader::read() {
    std::map<int, Msg::Message> _messages;

    // load entire file content into string
    std::string s = _stream.readString(_stream.size());

    // FIXME: 1. multiline strings
    //        2. bug in CMBATAI2.MSG in messages #1382 and #32020 where we need to handle missing '}'
    std::regex re{ R"(\{(\d+)\}\{(.*)?\}\{(.*)?\})" };

    std::smatch matches;

    while (std::regex_search(s, matches, re)) {
        if (matches.size() != 4) {
            spdlog::error("Wrong message format in {}: {}", _path.filename().string(), matches[0].str());

        } else {
            int id = stoi(matches[1].str());
            std::string audio = matches[2].str();
            std::string message = matches[3].str();

            // remove new lines
            message.erase(std::remove(message.begin(), message.end(), '\n'), message.end()); // TODO: \r\n

            spdlog::debug("{} message {}: {}, audio: {}", _path.filename().string(), id, message, audio);

            _messages[id] = { id, audio, message };
        }
        s = matches.suffix(); // next
    }

    return std::make_unique<Msg>(_path, std::move(_messages));
}

} // namespace geck
