#include <spdlog/spdlog.h>

#include "Application.h"

int main(int argc, char** argv) {
    spdlog::set_pattern("[%^%l%$] %v");

    geck::Application app{ argc, argv };
    app.run();
    return 0;
}
