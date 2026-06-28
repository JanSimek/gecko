#pragma once

#include <string>

namespace geck {
class ObjectCommandController;
} // namespace geck

namespace geck::pattern {

struct FillPlan;

/// Commits a FillPlan to the map as a SINGLE undo entry. This is the shared apply path for both
/// prefab stamps (PatternStamper) and area fills (MapScriptApi's plan-sink): the objects and tiles
/// are already built/resolved in the plan, so replay only registers them — nothing is rebuilt, so
/// the committed result matches whatever produced the plan (a preview, a seeded run) byte for byte.
struct PlacementBatch {
    struct Result {
        int objectsPlaced = 0;
        int objectsFailed = 0; ///< an entry the controller refused (e.g. a null visual when sprites are required)
        int tilesPainted = 0;
    };

    /// Replay `plan` into one ScopedUndoBatch labelled `description`. `buildSprites` mirrors the
    /// producer: when true each entry is registered with its visual Object (registerObjectPlacement);
    /// when false the entry's MapObject is recorded as data only (registerObjectData), so a plan
    /// built headlessly lands without a GL context. An empty plan registers nothing.
    static Result replay(ObjectCommandController& controller, const FillPlan& plan,
        bool buildSprites, const std::string& description);
};

} // namespace geck::pattern
