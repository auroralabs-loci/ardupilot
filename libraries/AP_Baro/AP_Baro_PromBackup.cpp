#include "AP_Baro.h"

#if AP_BARO_PROM_BACKUP_ENABLED

#include <AP_Math/crc.h>
#include <StorageManager/PersistentStorage.h>

// a different chip differs in far more bits than this
#define AP_BARO_PROM_MAX_BIT_DIFF 4

void AP_Baro::save_prom(const uint16_t prom[8], uint32_t bus_id)
{
    PersistentStorage::write(PersistentStorage::Type::BARO_PROM, bus_id, prom, 8 * sizeof(prom[0]));
}

// true if stored can replace prom: it passes its CRC and differs from
// prom by at most AP_BARO_PROM_MAX_BIT_DIFF bits
static bool prom_matches(const uint16_t stored[8], const uint16_t prom[8])
{
    uint8_t bit_diff = 0;
    for (uint8_t i = 0; i < 8; i++) {
        bit_diff += __builtin_popcount(stored[i] ^ prom[i]);
    }
    if (bit_diff > AP_BARO_PROM_MAX_BIT_DIFF) {
        return false;
    }
    // MS5611 CRC4 is computed with the CRC nibble cleared
    uint16_t crc_check[8];
    memcpy(crc_check, stored, sizeof(crc_check));
    crc_check[7] &= 0xff00;
    return (stored[7] & 0xf) == crc_crc4(crc_check);
}

bool AP_Baro::find_stored_prom(uint16_t prom[8], uint32_t bus_id) const
{
    uint16_t stored[8];
    if (!PersistentStorage::read(PersistentStorage::Type::BARO_PROM, bus_id, stored, sizeof(stored)) ||
        !prom_matches(stored, prom)) {
        return false;
    }
    memcpy(prom, stored, sizeof(stored));
    return true;
}

#endif  // AP_BARO_PROM_BACKUP_ENABLED
