#include "MsgReader.h"

#include <regex>
#include <streambuf>
#include <spdlog/spdlog.h>

#include "../ErrorMessages.h"

namespace geck {

std::unique_ptr<Msg> MsgReader::read() {
    try {
        auto& utils = getBinaryUtils();
        spdlog::debug("Reading MSG file: {}", _path.string());
        
        // Validate file size
        auto pos = utils.getPosition();
        if (pos.total == 0) {
            throw UnsupportedFormatException("Empty MSG file", _path);
        }
        
        // Add MSG format validation
        FormatValidator::validateMsgFile(utils, _path);
        
        spdlog::trace("MSG file size: {} bytes", pos.total);
        
        std::map<int, Msg::Message> _messages;
        int messages_found = 0;

        // Load entire file content into string using BinaryUtils
        std::string s = utils.readFixedString(pos.total);
        
        // Validate that content looks like MSG format
        if (s.find('{') == std::string::npos || s.find('}') == std::string::npos) {
            spdlog::warn("MSG file may not contain valid message format");
        }

        // Enhanced regex for MSG parsing
        // NOTE: Known issues to handle:
        // 1. Multiline strings (not yet implemented)
        // 2. Missing '}' in some files (e.g., CMBATAI2.MSG messages #1382, #32020)
        std::regex re{ R"(\{(\d+)\}\{(.*)?\}\{(.*)?\})" };

        std::smatch matches;
        
        while (std::regex_search(s, matches, re)) {
            if (matches.size() != 4) {
                spdlog::error("Wrong message format in {}: {}", _path.filename().string(), matches[0].str());
                
                // Continue parsing to be more resilient
                s = matches.suffix();
                continue;
            }
            
            try {
                int id = std::stoi(matches[1].str());
                std::string audio = matches[2].str();
                std::string message = matches[3].str();
                
                // Validate message ID range
                if (id < 0 || id > 99999) {
                    spdlog::warn("MSG file has unusual message ID: {}", id);
                }
                
                // Clean up message text
                // Remove newlines (TODO: better handling of \r\n sequences)
                message.erase(std::remove(message.begin(), message.end(), '\n'), message.end());
                message.erase(std::remove(message.begin(), message.end(), '\r'), message.end());
                
                // Check for duplicate message IDs
                if (_messages.find(id) != _messages.end()) {
                    spdlog::warn("Duplicate message ID {} in MSG file", id);
                }
                
                spdlog::trace("{} message {}: '{}', audio: '{}'", _path.filename().string(), id, message, audio);
                
                _messages[id] = { id, audio, message };
                messages_found++;
                
            } catch (const std::invalid_argument& e) {
                spdlog::error("Invalid message ID in MSG file: {}", matches[1].str());
            }
            
            s = matches.suffix(); // Move to next match
        }
        
        spdlog::debug("Successfully read MSG file: {} messages found", messages_found);

        return std::make_unique<Msg>(_path, std::move(_messages));
        
    } catch (const FileReaderException&) {
        throw;
    } catch (const std::exception& e) {
        throw ParseException(
            "Failed to parse MSG file: " + std::string(e.what()), _path);
    }
}

} // namespace geck