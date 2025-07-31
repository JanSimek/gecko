#include "Lst.h"

namespace geck {

void Lst::setList(const std::vector<std::string>& list) {
    _list = list;
}

const std::vector<std::string>& Lst::list() const {
    return _list;
}

const std::string& Lst::at(int line) const {
    // TODO: check boundaries
    return _list.at(line);
}

} // namespace geck
