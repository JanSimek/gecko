#pragma once

#include <fstream>
#include <vector>

#include "format/IFile.h"

namespace geck {

class Lst : public IFile {
private:
    std::vector<std::string> _list;
    std::vector<std::string> _comments; // the trailing ';' comment of each entry, aligned with _list

public:
    Lst(std::filesystem::path path)
        : IFile(path) { }

    const std::vector<std::string>& list() const;
    void setList(const std::vector<std::string>& list);

    /// The per-entry trailing comment (the text after ';', e.g. a script's description in scripts.lst),
    /// aligned 1:1 with list(). Empty where an entry has no comment.
    const std::vector<std::string>& comments() const;
    void setComments(const std::vector<std::string>& comments);

    const std::string& at(int line) const;
};

} // namespace geck
