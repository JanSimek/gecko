#pragma once

#include <fstream>
#include <vector>

#include "format/IFile.h"

namespace geck {

class Lst : public IFile {
private:
    std::vector<std::string> _list;
    std::vector<std::string> _comments; // the trailing ';' comment of each entry, aligned with _list
    std::vector<int> _localVars;        // scripts.lst "# local_vars=N" of each entry, aligned with _list

public:
    Lst(std::filesystem::path path)
        : IFile(path) { }

    const std::vector<std::string>& list() const;
    void setList(const std::vector<std::string>& list);

    /// The per-entry trailing comment (the text after ';', e.g. a script's description in scripts.lst),
    /// aligned 1:1 with list(). Empty where an entry has no comment.
    const std::vector<std::string>& comments() const;
    void setComments(const std::vector<std::string>& comments);

    /// The `local_vars=N` count of each entry (scripts.lst), aligned 1:1 with list(): how many local
    /// variable slots the engine gives the script (fallout2-ce scripts.cc). 0 where an entry has none.
    const std::vector<int>& localVars() const;
    void setLocalVars(const std::vector<int>& localVars);

    const std::string& at(int line) const;
};

} // namespace geck
