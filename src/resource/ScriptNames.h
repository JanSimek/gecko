#pragma once

#include "format/lst/Lst.h"
#include "format/msg/Msg.h"
#include "resource/GameResources.h"
#include "resource/ResourcePaths.h"

#include <cstddef>
#include <exception>
#include <string>

namespace geck::resource {

/// The human-readable description for the script at 0-based scripts.lst `programIndex`, taken from
/// scrname.msg (the display name is `scrname.msg[programIndex + 101]` — verified against the data and the
/// engine's 1-based map script_id). Returns "" when scrname.msg isn't mounted or the entry is blank, so
/// callers fall back to the bare .lst name.
inline std::string scriptDisplayName(GameResources& resources, int programIndex) {
    if (programIndex < 0) {
        return {};
    }
    try {
        if (Msg* msg = resources.repository().load<Msg>("text/english/game/scrname.msg"); msg != nullptr) {
            // Use find() rather than message()/operator[], so a miss (a program index past scrname.msg)
            // doesn't insert an empty entry into the cached Msg — this is called per scripts.lst entry.
            const auto& messages = msg->getMessages();
            if (const auto it = messages.find(programIndex + 101); it != messages.end()) {
                return it->second.text;
            }
        }
    } catch (const std::exception&) {
        // scrname.msg not mounted -> no friendly name; callers use the .lst name.
    }
    return {};
}

/// The best human-readable label for the script at 0-based `programIndex`: the scrname.msg display name
/// if present, otherwise the scripts.lst trailing comment (the developer description the engine data
/// ships), otherwise "". scrname.msg only names talking/critter scripts, so this gives spatial-trigger
/// and generic-scenery scripts a friendly label too, instead of the bare `.int` filename. Both sources
/// are real engine data — no invented labels, per the engine-fidelity rule.
inline std::string scriptDescription(GameResources& resources, int programIndex) {
    if (std::string name = scriptDisplayName(resources, programIndex); !name.empty()) {
        return name;
    }
    if (programIndex < 0) {
        return {};
    }
    try {
        if (const Lst* lst = resources.repository().load<Lst>(ResourcePaths::Lst::SCRIPTS); lst != nullptr) {
            const auto& comments = lst->comments();
            if (static_cast<std::size_t>(programIndex) < comments.size()) {
                return comments[static_cast<std::size_t>(programIndex)];
            }
        }
    } catch (const std::exception&) {
        // scripts.lst not mounted -> no comment; callers fall back to the .lst filename.
    }
    return {};
}

} // namespace geck::resource
