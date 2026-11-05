#include "PersistentStorage.h"

#include <AP_Math/AP_Math.h>
#include <AP_Math/crc.h>

extern const AP_HAL::HAL& hal;

/*
  record layout: header, data[length], CRC32. The CRC covers everything
  after the magic up to the CRC
 */
struct PACKED RecordHeader {
    uint16_t magic;
    uint8_t type;
    uint8_t length;
    uint32_t id;
};

#define PERSISTENT_RECORD_MAGIC 0x5250
static constexpr uint16_t RECORD_OVERHEAD = sizeof(RecordHeader) + sizeof(uint32_t);

static uint16_t area_size()
{
    return MIN(StorageAccess(StorageManager::StoragePersistent).size(), PersistentStorage::MAX_SIZE);
}

static uint32_t record_crc(const uint8_t *rec, uint8_t length)
{
    return crc_crc32(0, rec + sizeof(uint16_t), sizeof(RecordHeader) - sizeof(uint16_t) + length);
}

// size of the valid record at the start of rec, 0 if there isn't one
static uint16_t record_size(const uint8_t *rec, uint16_t available)
{
    RecordHeader hdr;
    if (available < RECORD_OVERHEAD) {
        return 0;
    }
    memcpy(&hdr, rec, sizeof(hdr));
    const uint16_t size = RECORD_OVERHEAD + hdr.length;
    if (hdr.magic != PERSISTENT_RECORD_MAGIC || size > available) {
        return 0;
    }
    uint32_t crc;
    memcpy(&crc, &rec[size - sizeof(crc)], sizeof(crc));
    return crc == record_crc(rec, hdr.length) ? size : 0;
}

// offset of the record with this type and id in buf[0..used), -1 if none
static int16_t find_record(const uint8_t *buf, uint16_t used, uint8_t type, uint32_t id)
{
    uint16_t ofs = 0;
    while (ofs < used) {
        RecordHeader hdr;
        memcpy(&hdr, &buf[ofs], sizeof(hdr));
        if (hdr.type == type && hdr.id == id) {
            return ofs;
        }
        ofs += RECORD_OVERHEAD + hdr.length;
    }
    return -1;
}

static uint16_t make_record(uint8_t *rec, uint8_t type, uint32_t id, const void *data, uint8_t length)
{
    const RecordHeader hdr { PERSISTENT_RECORD_MAGIC, type, length, id };
    memcpy(rec, &hdr, sizeof(hdr));
    memcpy(&rec[sizeof(hdr)], data, length);
    const uint32_t crc = record_crc(rec, length);
    memcpy(&rec[sizeof(hdr) + length], &crc, sizeof(crc));
    return RECORD_OVERHEAD + length;
}

/*
  if rec[0..available) starts with a valid record, append it to buf
  unless a record with its type and id is already there. Returns the
  size of the record, 0 if there isn't a valid one
 */
static uint16_t append_valid(uint8_t *buf, uint16_t &used, const uint8_t *rec, uint16_t available)
{
    const uint16_t size = record_size(rec, available);
    if (size == 0) {
        return 0;
    }
    RecordHeader hdr;
    memcpy(&hdr, rec, sizeof(hdr));
    if (find_record(buf, used, hdr.type, hdr.id) < 0 && used + size <= area_size()) {
        memcpy(&buf[used], rec, size);
        used += size;
    }
    return size;
}

uint16_t PersistentStorage::load(uint8_t buf[MAX_SIZE])
{
    memset(buf, 0, MAX_SIZE);
    const uint16_t size = area_size();
    uint8_t area[MAX_SIZE] {};
    if (size == 0 || !StorageAccess(StorageManager::StoragePersistent).read_block(area, 0, size)) {
        return 0;
    }
    // scan byte by byte so a corrupted record doesn't hide the ones after it
    uint16_t used = 0;
    for (uint16_t ofs = 0; ofs < size; ofs++) {
        const uint16_t rec_size = append_valid(buf, used, &area[ofs], size - ofs);
        if (rec_size > 0) {
            ofs += rec_size - 1;
        }
    }
    if (used == 0) {
        used = recover(buf, 0);
    }
    if (memcmp(buf, area, size) != 0) {
        // records were recovered, or the area had gaps or corruption
        store(buf, 0, size);
    }
    return used;
}

uint16_t PersistentStorage::recover(uint8_t buf[MAX_SIZE], uint16_t used)
{
    uint8_t rec[MAX_SIZE];
    for (uint32_t ofs = 0; ofs + RECORD_OVERHEAD <= HAL_STORAGE_SIZE; ofs++) {
        uint16_t magic;
        hal.storage->read_block(&magic, ofs, sizeof(magic));
        if (magic != PERSISTENT_RECORD_MAGIC) {
            continue;
        }
        const uint16_t available = MIN(sizeof(rec), HAL_STORAGE_SIZE - ofs);
        hal.storage->read_block(rec, ofs, available);
        const uint16_t rec_size = append_valid(buf, used, rec, available);
        if (rec_size > 0) {
            ofs += rec_size - 1;
        }
    }
    return used;
}

void PersistentStorage::store(const uint8_t buf[MAX_SIZE], uint16_t from, uint16_t to)
{
    if (to > from) {
        StorageAccess(StorageManager::StoragePersistent).write_block(from, &buf[from], to - from);
    }
}

bool PersistentStorage::read(Type type, uint32_t id, void *data, uint8_t length)
{
    uint8_t buf[MAX_SIZE];
    const uint16_t used = load(buf);
    const int16_t ofs = find_record(buf, used, uint8_t(type), id);
    if (ofs < 0 || buf[ofs + offsetof(RecordHeader, length)] != length) {
        return false;
    }
    memcpy(data, &buf[ofs + sizeof(RecordHeader)], length);
    return true;
}

bool PersistentStorage::write(Type type, uint32_t id, const void *data, uint8_t length)
{
    uint8_t buf[MAX_SIZE];
    uint16_t used = load(buf);
    const uint16_t old_used = used;
    const int16_t ofs = find_record(buf, used, uint8_t(type), id);
    uint16_t first_changed = used;
    if (ofs >= 0) {
        const uint16_t old_size = RECORD_OVERHEAD + buf[ofs + offsetof(RecordHeader, length)];
        if (old_size == RECORD_OVERHEAD + length) {
            if (memcmp(&buf[ofs + sizeof(RecordHeader)], data, length) == 0) {
                return true;
            }
            make_record(&buf[ofs], uint8_t(type), id, data, length);
            store(buf, ofs, ofs + old_size);
            return true;
        }
        // the length changed: remove the old record, then append
        memmove(&buf[ofs], &buf[ofs + old_size], used - (ofs + old_size));
        used -= old_size;
        memset(&buf[used], 0, old_size);
        first_changed = ofs;
    }
    const bool fits = used + RECORD_OVERHEAD + length <= area_size();
    if (fits) {
        used += make_record(&buf[used], uint8_t(type), id, data, length);
    }
    store(buf, first_changed, MAX(used, old_used));
    return fits;
}
