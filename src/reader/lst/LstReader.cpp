#include "LstReader.h"

#include <algorithm>
#include <array>

#include "../../format/lst/Lst.h"

namespace geck {

std::string parseLine(std::string line) {
    // strip comments
    if (auto pos = line.find(';')) {
        line = line.substr(0, pos);
    }

    // rtrim
    line.erase(std::find_if(line.rbegin(), line.rend(), [](unsigned char c) {
        return !std::isspace(c);
    }).base(),
        line.end());

    // replace slashes
    std::replace(line.begin(), line.end(), '\\', '/');

    // to lower
    std::transform(line.begin(), line.end(), line.begin(), ::tolower);

    return line;
}

std::unique_ptr<Lst> LstReader::read() {
    _stream.setPosition(0);

    std::vector<std::string> list;
    std::string line;
    unsigned char ch = 0;
    for (unsigned int i = 0; i != _stream.size(); ++i) {
        _stream >> ch;
        if (ch == 0x0D) // \r
        {
            // do nothing
        } else if (ch == 0x0A) // \n
        {
            list.push_back(parseLine(line));
            line.clear();
        } else {
            line += ch;
        }
    }
    if (line.size() != 0) {
        list.push_back(parseLine(line));
    }

    auto lst = std::make_unique<Lst>(_path);
    lst->setList(list);
    return lst;
}

} // namespace geck
