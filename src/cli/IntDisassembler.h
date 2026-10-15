#pragma once

#include "ssl/IntProgram.h"

#include <nlohmann/json_fwd.hpp>

namespace geck::cli {

/// The program as JSON: procedures in table order, each with its code as one compact line per
/// instruction ("offset name operand ; note").
nlohmann::ordered_json intProgramToJson(const ssl::IntProgram& program);

} // namespace geck::cli
