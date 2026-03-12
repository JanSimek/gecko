#pragma once

#include <array>
#include <cstdint>
#include <string>

namespace geck {
class Lst;
}

namespace geck::resource {

class ResourceRepository;

class FrmResolver final {
public:
    explicit FrmResolver(ResourceRepository& repository);

    [[nodiscard]] std::string resolve(uint32_t fid);

private:
    ResourceRepository& _repository;
};

} // namespace geck::resource
