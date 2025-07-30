#pragma once

#include "../FileWriter.h"
#include "../../format/pro/Pro.h"

namespace geck {

/**
 * Writer for Fallout PRO (Prototype) files.
 * PRO files contain object definitions and properties.
 */
class ProWriter : public FileWriter<Pro> {
public:
    ProWriter() = default;
    virtual ~ProWriter() = default;

    bool write(const Pro& pro) override;

private:
    void writeHeader(const Pro& pro);
    void writeItemData(const Pro& pro);
    void writeCritterData(const Pro& pro);
    void writeSceneryData(const Pro& pro);
    void writeWallData(const Pro& pro);
    void writeTileData(const Pro& pro);
    void writeMiscData(const Pro& pro);
    
    // Item-specific write methods
    void writeArmorData(const Pro& pro);
    void writeContainerData(const Pro& pro);
    void writeDrugData(const Pro& pro);
    void writeWeaponData(const Pro& pro);
    void writeAmmoData(const Pro& pro);
    void writeMiscItemData(const Pro& pro);
    void writeKeyData(const Pro& pro);
};

} // namespace geck