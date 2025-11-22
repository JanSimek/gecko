#include "CritterFrmResolver.h"
#include <spdlog/spdlog.h>
#include <algorithm>
#include <unordered_map>

namespace geck {

std::string CritterFrmResolver::generateCritterFrmName(const std::string& baseName, uint32_t frmPid) {
    // Extract components from FRM PID (based on legacy mapper logic)
    uint32_t index = frmPid & 0x00000FFF;
    uint32_t id1 = (frmPid & 0x0000F000) >> 12;
    uint32_t id2 = (frmPid & 0x00FF0000) >> 16;
    uint32_t id3 = (frmPid & 0x70000000) >> 28;
    
    spdlog::debug("CritterFrmResolver: Generating FRM name for PID 0x{:08X} - index={}, id1={}, id2={}, id3={}", 
                 frmPid, index, id1, id2, id3);
    
    // Generate animation suffixes
    char suffix1, suffix2;
    if (!getSuffixes(id1, id2, suffix1, suffix2)) {
        spdlog::warn("CritterFrmResolver: Invalid suffix combination for id1={}, id2={}", id1, id2);
        return "";
    }
    
    // Build final filename: baseName + suffix1 + suffix2 + ".fr" + direction
    std::string filename = baseName;
    filename += suffix1;
    filename += suffix2;
    filename += ".fr";
    filename += (id3 && id3 <= 5) ? char('0' + id3 - 1) : 'm';
    
    spdlog::debug("CritterFrmResolver: Generated filename: {}", filename);
    return filename;
}

uint32_t CritterFrmResolver::deriveCritterFrmPid(const std::string& /* baseName */, 
                                                const std::string& frmFilename, 
                                                uint32_t baseIndex) {
    char suffix1, suffix2, direction;
    if (!parseAnimationSuffixes(frmFilename, suffix1, suffix2, direction)) {
        return 0;
    }
    
    uint32_t id1, id2;
    if (!suffixesToIds(suffix1, suffix2, id1, id2)) {
        return 0;
    }
    
    uint32_t id3 = 0;
    if (direction >= '0' && direction <= '5') {
        id3 = (direction - '0') + 1;
    } else if (direction == 'm') {
        id3 = 0;
    } else {
        return 0;
    }
    
    uint32_t frmPid = (id3 << 28) | (1 << 24) | (id2 << 16) | (id1 << 12) | baseIndex;
    
    return frmPid;
}

std::string CritterFrmResolver::getAnimationTypeName(const std::string& frmFilename) {
    // Extract animation suffixes
    char suffix1, suffix2, direction;
    if (!parseAnimationSuffixes(frmFilename, suffix1, suffix2, direction)) {
        return "Unknown";
    }
    
    // Map common animation combinations to readable names
    static const std::unordered_map<std::string, std::string> animationNames = {
        {"aa", "Standing"},
        {"ab", "Walking"},
        {"ad", "Running"},
        {"ae", "Sneaking"},
        {"af", "Single Attack"},
        {"ag", "Burst Attack"},
        {"ah", "Thrust Attack"},
        {"ai", "Throw Attack"},
        {"aj", "Dodge"},
        {"ak", "Damage"},
        {"al", "Dead (Front)"},
        {"an", "Dead (Back)"},
        {"ao", "Unconscious"},
        {"ap", "Pickup"},
        {"aq", "Use"},
        {"ar", "Climb"},
        {"as", "Special 1"},
        {"at", "Special 2"},
        {"ba", "Weapon Idle"},
        {"bb", "Weapon Walk"},
        {"bd", "Weapon Run"},
        {"be", "Weapon Sneak"},
        {"bf", "Weapon Single"},
        {"bg", "Weapon Burst"},
        {"bh", "Weapon Thrust"},
        {"bi", "Weapon Throw"},
        {"bj", "Weapon Dodge"},
        {"bk", "Weapon Damage"},
        {"bl", "Weapon Dead"},
        {"ch", "Called Shot Head"},
        {"cj", "Called Shot Groin"},
        {"da", "Magic Hands Begin"},
        {"db", "Magic Hands Middle"},
        {"dc", "Magic Hands End"},
        {"ra", "Rotation"},
    };
    
    std::string suffixKey = std::string(1, suffix1) + std::string(1, suffix2);
    std::transform(suffixKey.begin(), suffixKey.end(), suffixKey.begin(), ::tolower);
    
    auto it = animationNames.find(suffixKey);
    if (it != animationNames.end()) {
        return it->second;
    }
    
    return std::string("Animation ") + suffixKey;
}

bool CritterFrmResolver::matchesCritterBase(const std::string& baseName, const std::string& frmFilename) {
    if (baseName.empty() || frmFilename.empty()) {
        return false;
    }
    
    std::string filename = frmFilename;
    size_t lastSlash = filename.find_last_of("/\\");
    if (lastSlash != std::string::npos) {
        filename = filename.substr(lastSlash + 1);
    }
    
    size_t dotPos = filename.find_last_of('.');
    if (dotPos != std::string::npos) {
        std::string extension = filename.substr(dotPos + 1);
        if (extension == "frm" || (extension.length() == 3 && extension.substr(0, 2) == "fr")) {
            filename = filename.substr(0, dotPos);
        } else {
            return false;
        }
    }
    
    std::string lowerFilename = filename;
    std::string lowerBaseName = baseName;
    std::transform(lowerFilename.begin(), lowerFilename.end(), lowerFilename.begin(), ::tolower);
    std::transform(lowerBaseName.begin(), lowerBaseName.end(), lowerBaseName.begin(), ::tolower);
    
    if (lowerFilename.length() != lowerBaseName.length() + 2) {
        return false;
    }
    
    if (lowerFilename.substr(0, lowerBaseName.length()) != lowerBaseName) {
        return false;
    }
    
    char suffix1 = lowerFilename[lowerBaseName.length()];
    char suffix2 = lowerFilename[lowerBaseName.length() + 1];
    
    return std::islower(suffix1) && std::islower(suffix2);
}

bool CritterFrmResolver::getSuffixes(uint32_t id1, uint32_t id2, char& suffix1, char& suffix2) {
    // Based on legacy mapper's getSuffixes() function
    if (id1 >= 0x0B) {
        return false;
    }
    
    if (id2 >= 0x26 && id2 <= 0x2F) {
        suffix2 = char(id2) + 0x3D;
        if (id1 == 0) {
            return false;
        }
        suffix1 = char(id1) + 'c';
        return true;
    }
    
    if (id2 == 0x24) {
        suffix2 = 'h';
        suffix1 = 'c';
        return true;
    }
    
    if (id2 == 0x25) {
        suffix2 = 'j';
        suffix1 = 'c';
        return true;
    }
    
    if (id2 == 0x40) {
        suffix2 = 'a';
        suffix1 = 'n';
        return true;
    }
    
    if (id2 >= 0x30) {
        suffix2 = char(id2) + 0x31;
        suffix1 = 'r';
        return true;
    }
    
    if (id2 >= 0x14) {
        suffix2 = char(id2) + 0x4D;
        suffix1 = 'b';
        return true;
    }
    
    if (id2 == 0x12) {
        if (id1 == 0x01) {
            suffix1 = 'd';
            suffix2 = 'm';
            return true;
        }
        
        if (id1 == 0x04) {
            suffix1 = 'g';
            suffix2 = 'm';
            return true;
        }
        
        suffix1 = 'a';
        suffix2 = 's';
        return true;
    }
    
    if (id2 == 0x0D) {
        if (id1 > 0x00) {
            suffix1 = char(id1) + 'c';
            suffix2 = 'e';
            return true;
        }
        
        suffix1 = 'a';
        suffix2 = 'n';
        return true;
    }
    
    suffix2 = char(id2) + 'a';
    
    if (id2 <= 1 && id1 > 0) {
        suffix1 = char(id1) + 'c';
        return true;
    }
    
    suffix1 = 'a';
    return true;
}

bool CritterFrmResolver::parseAnimationSuffixes(const std::string& frmFilename, 
                                               char& suffix1, char& suffix2, char& direction) {
    if (frmFilename.empty()) {
        return false;
    }
    
    std::string filename = frmFilename;
    size_t lastSlash = filename.find_last_of("/\\");
    if (lastSlash != std::string::npos) {
        filename = filename.substr(lastSlash + 1);
    }
    
    std::transform(filename.begin(), filename.end(), filename.begin(), ::tolower);
    
    size_t dotPos = filename.find_last_of('.');
    if (dotPos == std::string::npos || dotPos < 2) {
        return false;
    }
    
    std::string extension = filename.substr(dotPos + 1);
    filename = filename.substr(0, dotPos);
    
    if (extension == "frm") {
        direction = 'm';
    } else if (extension.length() == 3 && extension.substr(0, 2) == "fr") {
        char dirChar = extension[2];
        if (dirChar >= '0' && dirChar <= '5') {
            direction = dirChar;
        } else {
            return false;
        }
    } else {
        return false;
    }
    
    if (filename.length() < 2) {
        return false;
    }
    
    suffix1 = filename[filename.length() - 2];
    suffix2 = filename[filename.length() - 1];
    
    return std::islower(suffix1) && std::islower(suffix2);
}

bool CritterFrmResolver::suffixesToIds(char suffix1, char suffix2, uint32_t& id1, uint32_t& id2) {
    if (suffix1 == 'c' && suffix2 == 'h') {
        id1 = 0; id2 = 0x24;
        return true;
    }
    
    if (suffix1 == 'c' && suffix2 == 'j') {
        id1 = 0; id2 = 0x25;
        return true;
    }
    
    if (suffix1 == 'n' && suffix2 == 'a') {
        id1 = 0; id2 = 0x40;
        return true;
    }
    
    if (suffix1 == 'd' && suffix2 == 'm') {
        id1 = 1; id2 = 0x12;
        return true;
    }
    
    if (suffix1 == 'g' && suffix2 == 'm') {
        id1 = 4; id2 = 0x12;
        return true;
    }
    
    if (suffix1 == 'a' && suffix2 == 's') {
        id1 = 0; id2 = 0x12;
        return true;
    }
    
    if (suffix1 == 'a' && suffix2 == 'n') {
        id1 = 0; id2 = 0x0D;
        return true;
    }
    
    // Pattern matching for ranges
    if (suffix1 == 'r' && suffix2 >= 'q' && suffix2 <= 'z') {
        id1 = 0;
        id2 = suffix2 - 0x31;
        return true;
    }
    
    if (suffix1 == 'b' && suffix2 >= 'a' && suffix2 <= 'z') {
        id1 = 0;
        id2 = suffix2 - 0x4D;
        return true;
    }
    
    if (suffix1 >= 'd' && suffix1 <= 'n' && suffix2 >= 'c' && suffix2 <= 'r') {
        id1 = suffix1 - 'c';
        id2 = suffix2 - 0x3D;
        return true;
    }
    
    if (suffix1 >= 'd' && suffix1 <= 'n' && suffix2 == 'e') {
        id1 = suffix1 - 'c';
        id2 = 0x0D;
        return true;
    }
    
    if (suffix1 >= 'd' && suffix1 <= 'n' && suffix2 <= 'b') {
        id1 = suffix1 - 'c';
        id2 = suffix2 - 'a';
        return true;
    }
    
    if (suffix1 == 'a' && suffix2 >= 'a' && suffix2 <= 'b') {
        id1 = 0;
        id2 = suffix2 - 'a';
        return true;
    }
    
    return false;
}

} // namespace geck