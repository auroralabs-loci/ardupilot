#include <AP_gtest.h>
#include <AP_Math/crc.h>
#include <StorageManager/PersistentStorage.h>

const AP_HAL::HAL &hal = AP_HAL::get_HAL();

class RamStorage : public AP_HAL::Storage
{
public:
    void init() override {}
    void read_block(void *dst, uint16_t src, size_t size) override
    {
        memcpy(dst, &bytes[src], size);
    }
    void write_block(uint16_t dst, const void *src, size_t size) override
    {
        memcpy(&bytes[dst], src, size);
    }
    uint8_t bytes[HAL_STORAGE_SIZE] {};
};

// physical offset of the StoragePersistent area in the layout
static constexpr uint16_t AREA_OFFSET = 16256;
static constexpr auto BARO = PersistentStorage::Type::BARO_PROM;

class PersistentStorageTest : public ::testing::Test
{
protected:
    void SetUp() override
    {
        old_storage = hal.storage;
        AP_HAL::get_HAL_mutable().storage = &storage;
    }
    void TearDown() override
    {
        AP_HAL::get_HAL_mutable().storage = old_storage;
    }

    // a record in the documented layout, written straight to storage
    void plant(uint16_t ofs, uint8_t type, uint32_t id, const uint8_t *data, uint8_t length)
    {
        uint8_t rec[64];
        const uint16_t magic = 0x5250;
        memcpy(rec, &magic, 2);
        rec[2] = type;
        rec[3] = length;
        memcpy(&rec[4], &id, 4);
        memcpy(&rec[8], data, length);
        const uint32_t crc = crc_crc32(0, &rec[2], 6 + length);
        memcpy(&rec[8 + length], &crc, 4);
        memcpy(&storage.bytes[ofs], rec, 12 + length);
    }

    RamStorage storage;
    AP_HAL::Storage *old_storage;
    const uint16_t prom[8] { 0xFFFF, 40127, 36924, 23317, 23282, 33464, 28312, 0x0008 };
};

TEST_F(PersistentStorageTest, WriteRead)
{
    uint16_t out[8] {};
    EXPECT_FALSE(PersistentStorage::read(BARO, 7, out, sizeof(out)));
    EXPECT_TRUE(PersistentStorage::write(BARO, 7, prom, sizeof(prom)));
    EXPECT_TRUE(PersistentStorage::read(BARO, 7, out, sizeof(out)));
    EXPECT_EQ(0, memcmp(out, prom, sizeof(out)));
    // other ids and lengths don't match
    EXPECT_FALSE(PersistentStorage::read(BARO, 8, out, sizeof(out)));
    EXPECT_FALSE(PersistentStorage::read(BARO, 7, out, 4));
}

TEST_F(PersistentStorageTest, UpdateInPlace)
{
    uint16_t changed[8];
    memcpy(changed, prom, sizeof(changed));
    changed[3] ^= 0x0c00;
    EXPECT_TRUE(PersistentStorage::write(BARO, 1, prom, sizeof(prom)));
    EXPECT_TRUE(PersistentStorage::write(BARO, 2, prom, sizeof(prom)));
    EXPECT_TRUE(PersistentStorage::write(BARO, 1, changed, sizeof(changed)));
    uint16_t out[8];
    EXPECT_TRUE(PersistentStorage::read(BARO, 1, out, sizeof(out)));
    EXPECT_EQ(0, memcmp(out, changed, sizeof(out)));
    EXPECT_TRUE(PersistentStorage::read(BARO, 2, out, sizeof(out)));
    EXPECT_EQ(0, memcmp(out, prom, sizeof(out)));
    // still the first record in the area
    uint32_t id;
    memcpy(&id, &storage.bytes[AREA_OFFSET + 4], sizeof(id));
    EXPECT_EQ(1U, id);
}

TEST_F(PersistentStorageTest, LengthChange)
{
    const uint8_t small[4] { 1, 2, 3, 4 };
    EXPECT_TRUE(PersistentStorage::write(BARO, 1, prom, sizeof(prom)));
    EXPECT_TRUE(PersistentStorage::write(BARO, 2, prom, sizeof(prom)));
    EXPECT_TRUE(PersistentStorage::write(BARO, 1, small, sizeof(small)));
    uint8_t out_small[4];
    uint16_t out[8];
    EXPECT_TRUE(PersistentStorage::read(BARO, 1, out_small, sizeof(out_small)));
    EXPECT_EQ(0, memcmp(out_small, small, sizeof(small)));
    EXPECT_TRUE(PersistentStorage::read(BARO, 2, out, sizeof(out)));
}

TEST_F(PersistentStorageTest, AreaFull)
{
    // 28 byte records, 4 fit in 128 bytes
    for (uint32_t id = 0; id < 4; id++) {
        EXPECT_TRUE(PersistentStorage::write(BARO, id, prom, sizeof(prom)));
    }
    EXPECT_FALSE(PersistentStorage::write(BARO, 4, prom, sizeof(prom)));
    uint16_t out[8];
    EXPECT_TRUE(PersistentStorage::read(BARO, 3, out, sizeof(out)));
}

TEST_F(PersistentStorageTest, SurvivesErase)
{
    EXPECT_TRUE(PersistentStorage::write(BARO, 7, prom, sizeof(prom)));
    memset(&storage.bytes[0], 0x55, 100);
    StorageManager::erase();
    EXPECT_EQ(0, storage.bytes[0]);
    uint16_t out[8];
    EXPECT_TRUE(PersistentStorage::read(BARO, 7, out, sizeof(out)));
    EXPECT_EQ(0, memcmp(out, prom, sizeof(out)));
}

TEST_F(PersistentStorageTest, RecoversMovedRecord)
{
    // left by a layout that had the area at offset 100
    plant(100, uint8_t(BARO), 7, (const uint8_t *)prom, sizeof(prom));
    uint16_t out[8];
    EXPECT_TRUE(PersistentStorage::read(BARO, 7, out, sizeof(out)));
    EXPECT_EQ(0, memcmp(out, prom, sizeof(out)));
    // copied into the current area
    uint16_t magic;
    memcpy(&magic, &storage.bytes[AREA_OFFSET], sizeof(magic));
    EXPECT_EQ(0x5250, magic);
}

TEST_F(PersistentStorageTest, RecoversMovedRecordOnErase)
{
    uint16_t other[8] {};
    EXPECT_TRUE(PersistentStorage::write(BARO, 1, prom, sizeof(prom)));
    plant(100, uint8_t(BARO), 2, (const uint8_t *)prom, sizeof(prom));
    // a stale copy of id 1 must not replace the one in the area
    plant(300, uint8_t(BARO), 1, (const uint8_t *)other, sizeof(other));
    StorageManager::erase();
    uint16_t out[8];
    EXPECT_TRUE(PersistentStorage::read(BARO, 2, out, sizeof(out)));
    EXPECT_TRUE(PersistentStorage::read(BARO, 1, out, sizeof(out)));
    EXPECT_EQ(0, memcmp(out, prom, sizeof(out)));
}

TEST_F(PersistentStorageTest, CorruptRecordIgnored)
{
    EXPECT_TRUE(PersistentStorage::write(BARO, 1, prom, sizeof(prom)));
    EXPECT_TRUE(PersistentStorage::write(BARO, 2, prom, sizeof(prom)));
    // flip a data bit in the first record
    storage.bytes[AREA_OFFSET + 10] ^= 1;
    uint16_t out[8];
    EXPECT_FALSE(PersistentStorage::read(BARO, 1, out, sizeof(out)));
    // the record after it is still found
    EXPECT_TRUE(PersistentStorage::read(BARO, 2, out, sizeof(out)));
    // and the corrupted one can be written again
    EXPECT_TRUE(PersistentStorage::write(BARO, 1, prom, sizeof(prom)));
    EXPECT_TRUE(PersistentStorage::read(BARO, 1, out, sizeof(out)));
}

AP_GTEST_MAIN()
