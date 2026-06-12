#pragma once

#include <cstdint>
#include <string>
#include <vector>

namespace geck {

/**
 * @brief Handles sophisticated critter FRM name resolution and FID encoding
 * Based on the legacy F2 Mapper implementation
 */
class CritterFrmResolver {
public:
    /**
     * @brief Generate FRM filename from FRM PID for critters
     * @param baseName Base critter name from LST (e.g., "harobe")
     * @param frmPid Full FRM PID containing animation encoding
     * @return Complete FRM filename (e.g., "harobeaa.frm")
     */
    static std::string generateCritterFrmName(const std::string& baseName, uint32_t frmPid);

    /**
     * @brief Derive FRM PID from critter FRM filename
     * @param baseName Base critter name from LST
     * @param frmFilename Complete FRM filename
     * @param baseIndex Index in the LST file
     * @return FRM PID that can be used for map storage
     */
    static uint32_t deriveCritterFrmPid(const std::string& baseName,
        const std::string& frmFilename,
        uint32_t baseIndex);

    /**
     * @brief Get animation type name from FRM filename
     * @param frmFilename Complete FRM filename
     * @return Human-readable animation type (e.g., "Standing", "Walking")
     */
    static std::string getAnimationTypeName(const std::string& frmFilename);

    /**
     * @brief Check if a FRM filename matches a base critter name
     * @param baseName Base critter name from LST
     * @param frmFilename Complete FRM filename
     * @return True if the filename belongs to this critter base
     */
    static bool matchesCritterBase(const std::string& baseName, const std::string& frmFilename);

private:
    /**
     * @brief Generate animation suffixes from ID values
     * Based on legacy mapper's getSuffixes() function
     */
    static bool getSuffixes(uint32_t id1, uint32_t id2, char& suffix1, char& suffix2);

    /**
     * @brief Parse animation suffixes from filename
     * @param frmFilename Complete FRM filename
     * @param suffix1 First animation suffix character
     * @param suffix2 Second animation suffix character
     * @param direction Direction number (0-5 or 'm')
     * @return True if parsing was successful
     */
    static bool parseAnimationSuffixes(const std::string& frmFilename,
        char& suffix1, char& suffix2, char& direction);

    /**
     * @brief Convert suffixes back to ID values
     * @param suffix1 First animation suffix character
     * @param suffix2 Second animation suffix character
     * @param id1 Output first ID value
     * @param id2 Output second ID value
     * @return True if conversion was successful
     */
    static bool suffixesToIds(char suffix1, char suffix2, uint32_t& id1, uint32_t& id2);
};

} // namespace geck