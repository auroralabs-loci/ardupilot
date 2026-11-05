#pragma once

#include "StorageManager.h"

/*
  small records kept in the StoragePersistent area. Each record carries
  a magic and a CRC32, so it is found by scanning rather than by offset.
  StorageManager::erase() writes the records back after the erase, and
  records left elsewhere in storage by a layout that placed the area
  at a different offset are recovered into the current area.
 */
class PersistentStorage {
    friend class StorageManager;
public:
    enum class Type : uint8_t {
        BARO_PROM = 1,
    };

    // copy the data of the record with this type and id, false if there
    // is no valid record or its length differs
    static bool read(Type type, uint32_t id, void *data, uint8_t length);

    // add or update the record with this type and id, false if it
    // doesn't fit. Unchanged data is not rewritten
    static bool write(Type type, uint32_t id, const void *data, uint8_t length);

    static constexpr uint16_t MAX_SIZE = 128;

private:
    // load the area's records into buf, recovering records from the
    // rest of storage when the area is empty. Returns the bytes used
    static uint16_t load(uint8_t buf[MAX_SIZE]);

    // append valid records found anywhere in storage whose type and id
    // aren't in buf yet. Returns the new used size
    static uint16_t recover(uint8_t buf[MAX_SIZE], uint16_t used);

    // write buf[from..to) to the area
    static void store(const uint8_t buf[MAX_SIZE], uint16_t from, uint16_t to);
};
