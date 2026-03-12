#pragma once

#include "format/pro/Pro.h"

namespace geck {

class Lst;
class Msg;
namespace resource {
    class GameResources;
}

class ProHelper {
public:
    static Msg* protoMsgFile(resource::GameResources& resources);
    static Msg* statMsgFile(resource::GameResources& resources);
    static Msg* perkMsgFile(resource::GameResources& resources);
    static Msg* msgFile(resource::GameResources& resources, Pro::OBJECT_TYPE type);

    static Lst* lstFile(resource::GameResources& resources, uint32_t PID);

    static std::string basePath(resource::GameResources& resources, uint32_t PID);
};

}
