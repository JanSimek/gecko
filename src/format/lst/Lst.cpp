#include "Lst.h"

namespace geck {

void Lst::setList(const std::vector<std::string>& list) {
    _list = list;
}

const std::vector<std::string>& Lst::list() const {
    return _list;
}

void Lst::setComments(const std::vector<std::string>& comments) {
    _comments = comments;
}

const std::vector<std::string>& Lst::comments() const {
    return _comments;
}

const std::string& Lst::at(int line) const {
    // TODO: check boundaries
    return _list.at(line);
}

} // namespace geck
