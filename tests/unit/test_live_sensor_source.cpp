// Tests for src/drivers/live_sensor_source (REQ-SENS-001 through
// REQ-SENS-004, REQ-LOG-004). Test card TC-014, steps 5 through 10. Both
// chips sit on one fake bus, as they do on the real one: the barometer
// with the datasheet's worked example, the MPU6050 with one g on its z
// axis.

#include <gtest/gtest.h>

#include <memory>

#include "bmp280_example.h"
#include "drivers/live_sensor_source.h"
#include "drivers/mpu6050.h"
#include "fake_clock.h"
#include "fake_i2c_bus.h"

namespace {

constexpr uint8_t kBaro = 0x76;
constexpr uint8_t kImu = 0x68;
constexpr float kG = 9.81f;

void load_imu(FakeI2cBus& bus, uint8_t who_am_i = Mpu6050::kWhoAmI) {
    bus.registers(kImu)[Mpu6050::kRegWhoAmI] = who_am_i;
    // accel x, y, z; temperature; gyro x, y, z: one g on z, everything else zero.
    bus.load(kImu, Mpu6050::kRegAccelXoutH,
             {0x00, 0x00,  0x00, 0x00,  0x40, 0x00,  0x12, 0x34,  0x00, 0x00,  0x00, 0x00,  0x00, 0x00});
}

// A fake with both chips loaded, handed to the source; the raw pointer
// stays with the test so it can fail transactions after the move.
struct Rig {
    std::unique_ptr<FakeI2cBus> owned = std::make_unique<FakeI2cBus>();
    FakeI2cBus* bus = owned.get();
    FakeClock clock;

    Rig() {
        bmp280_example::load_chip(*bus, kBaro);
        load_imu(*bus);
    }

    LiveSensorSource make(int imu_sample_rate_hz = Mpu6050::kDefaultSampleRateHz) {
        return LiveSensorSource(std::move(owned), kBaro, kImu, clock, imu_sample_rate_hz);
    }
};

}  // namespace

// TC-014 step 5. One frame from both chips: raw fields in the frame's
// units, both I2C channels fresh, GPS absent and never fresh, every
// status NOMINAL, every computed field untouched (the raw-only contract
// of design.md section 3), and the first timestamp zero.
TEST(LiveSensorSourceTest, FrameCarriesBothChipsRawOnly) {
    Rig rig;
    LiveSensorSource source = rig.make();
    ASSERT_TRUE(source.ok()) << source.error();

    const TelemetryFrame f = source.read_frame();
    EXPECT_EQ(f.timestamp_ms, 0u);
    EXPECT_NEAR(f.pressure_hpa, bmp280_example::kPressureHpa, 0.001f);
    EXPECT_NEAR(f.temperature_c, bmp280_example::kTemperatureC, 0.001f);
    EXPECT_NEAR(f.accel_x, 0.0f, 1e-6f);
    EXPECT_NEAR(f.accel_y, 0.0f, 1e-6f);
    EXPECT_NEAR(f.accel_z, kG, 1e-4f);
    EXPECT_NEAR(f.gyro_x, 0.0f, 1e-6f);
    EXPECT_NEAR(f.gyro_y, 0.0f, 1e-6f);
    EXPECT_NEAR(f.gyro_z, 0.0f, 1e-6f);
    EXPECT_TRUE(f.baro_read_ok);
    EXPECT_TRUE(f.imu_read_ok);
    EXPECT_FALSE(f.gps_read_ok);
    EXPECT_EQ(f.latitude, 0.0);
    EXPECT_EQ(f.longitude, 0.0);
    EXPECT_EQ(f.gps_altitude_m, 0.0f);
    EXPECT_EQ(f.ground_speed_mps, 0.0f);
    EXPECT_EQ(f.heading_deg, 0.0f);
    EXPECT_EQ(f.baro_status, ChannelStatus::NOMINAL);
    EXPECT_EQ(f.imu_status, ChannelStatus::NOMINAL);
    EXPECT_EQ(f.gps_status, ChannelStatus::NOMINAL);
    EXPECT_EQ(f.baro_altitude_m, 0.0f);
    EXPECT_EQ(f.pitch_deg, 0.0f);
    EXPECT_EQ(f.roll_deg, 0.0f);
    EXPECT_EQ(f.fused_altitude_m, 0.0f);
    EXPECT_EQ(f.vertical_speed_mps, 0.0f);
}

// TC-014 step 6. A failed barometer burst holds the barometer's fields at
// the previous values (exactly, EXPECT_EQ) and clears only baro_read_ok;
// the IMU is untouched. The next good burst is fresh again.
TEST(LiveSensorSourceTest, FailedBaroReadHoldsFieldsAndClearsReadOk) {
    Rig rig;
    LiveSensorSource source = rig.make();
    ASSERT_TRUE(source.ok()) << source.error();
    const TelemetryFrame first = source.read_frame();

    rig.bus->fail_reads_at(kBaro, Bmp280::kRegData);
    const TelemetryFrame second = source.read_frame();
    EXPECT_EQ(second.pressure_hpa, first.pressure_hpa);
    EXPECT_EQ(second.temperature_c, first.temperature_c);
    EXPECT_FALSE(second.baro_read_ok);
    EXPECT_TRUE(second.imu_read_ok);
    EXPECT_NEAR(second.accel_z, kG, 1e-4f);
    EXPECT_EQ(second.baro_status, ChannelStatus::NOMINAL);   // sources report; the detector judges

    rig.bus->fail_reads_at(kBaro, Bmp280::kRegData, false);
    EXPECT_TRUE(source.read_frame().baro_read_ok);
}

// TC-014 step 7. The same for the IMU: its six fields hold, imu_read_ok
// clears, the barometer is untouched.
TEST(LiveSensorSourceTest, FailedImuReadHoldsFieldsAndClearsReadOk) {
    Rig rig;
    LiveSensorSource source = rig.make();
    ASSERT_TRUE(source.ok()) << source.error();
    const TelemetryFrame first = source.read_frame();

    rig.bus->fail_reads_at(kImu, Mpu6050::kRegAccelXoutH);
    const TelemetryFrame second = source.read_frame();
    EXPECT_EQ(second.accel_x, first.accel_x);
    EXPECT_EQ(second.accel_y, first.accel_y);
    EXPECT_EQ(second.accel_z, first.accel_z);
    EXPECT_EQ(second.gyro_x, first.gyro_x);
    EXPECT_EQ(second.gyro_y, first.gyro_y);
    EXPECT_EQ(second.gyro_z, first.gyro_z);
    EXPECT_FALSE(second.imu_read_ok);
    EXPECT_TRUE(second.baro_read_ok);
    EXPECT_NEAR(second.pressure_hpa, bmp280_example::kPressureHpa, 0.001f);

    rig.bus->fail_reads_at(kImu, Mpu6050::kRegAccelXoutH, false);
    EXPECT_TRUE(source.read_frame().imu_read_ok);
}

// TC-014 step 8. Before the first good read of a channel its fields are
// the zeros of a value-initialized frame, never stack contents, and the
// channel is not fresh. A barometer whose data registers still hold the
// reset value (no conversion complete) counts as not read.
TEST(LiveSensorSourceTest, FieldsAreZeroBeforeFirstGoodRead) {
    Rig rig;
    rig.bus->load(kBaro, Bmp280::kRegData,
                  bmp280_example::data_bytes(Bmp280::kSkippedMeasurement, Bmp280::kSkippedMeasurement));
    rig.bus->fail_reads_at(kImu, Mpu6050::kRegAccelXoutH);
    LiveSensorSource source = rig.make();
    ASSERT_TRUE(source.ok()) << source.error();

    const TelemetryFrame f = source.read_frame();
    EXPECT_FALSE(f.baro_read_ok);
    EXPECT_FALSE(f.imu_read_ok);
    EXPECT_EQ(f.pressure_hpa, 0.0f);
    EXPECT_EQ(f.temperature_c, 0.0f);
    EXPECT_EQ(f.accel_x, 0.0f);
    EXPECT_EQ(f.accel_y, 0.0f);
    EXPECT_EQ(f.accel_z, 0.0f);
    EXPECT_EQ(f.gyro_x, 0.0f);
    EXPECT_EQ(f.gyro_y, 0.0f);
    EXPECT_EQ(f.gyro_z, 0.0f);
}

// TC-014 step 9. Timestamps are milliseconds since the first read,
// rounded to the nearest millisecond: a 19.6 ms step reads as 20, and an
// overrun of a whole period shows up as a 40 ms step rather than being
// hidden behind the schedule.
TEST(LiveSensorSourceTest, TimestampsAreMillisecondsSinceFirstRead) {
    Rig rig;
    LiveSensorSource source = rig.make();
    ASSERT_TRUE(source.ok()) << source.error();

    EXPECT_EQ(source.read_frame().timestamp_ms, 0u);
    rig.clock.advance(20'000'000);
    EXPECT_EQ(source.read_frame().timestamp_ms, 20u);
    rig.clock.advance(19'600'000);
    EXPECT_EQ(source.read_frame().timestamp_ms, 40u);    // 39.6 rounds up
    rig.clock.advance(40'000'000);
    EXPECT_EQ(source.read_frame().timestamp_ms, 80u);    // 79.6 rounds up: a missed cycle is visible
    rig.clock.advance(400'000);
    EXPECT_EQ(source.read_frame().timestamp_ms, 80u);    // 80.0
}

// TC-014 step 10. A chip that answers with the wrong identity at startup
// leaves the source not ok, with the chip and the byte named, and both
// chips named when both are wrong; the source still produces frames,
// every channel stale, rather than crashing.
TEST(LiveSensorSourceTest, InitFailureNamesTheChip) {
    {
        Rig rig;
        rig.bus->registers(kBaro)[Bmp280::kRegId] = 0x55;
        LiveSensorSource source = rig.make();
        EXPECT_FALSE(source.ok());
        EXPECT_NE(source.error().find("barometer"), std::string::npos) << source.error();
        EXPECT_NE(source.error().find("0x55"), std::string::npos) << source.error();
        const TelemetryFrame f = source.read_frame();
        EXPECT_FALSE(f.baro_read_ok);
        EXPECT_TRUE(f.imu_read_ok);   // the chip that did initialize still reads; main refuses to start without both
    }
    {
        Rig rig;
        load_imu(*rig.bus, 0x70);
        LiveSensorSource source = rig.make();
        EXPECT_FALSE(source.ok());
        EXPECT_NE(source.error().find("IMU"), std::string::npos) << source.error();
        EXPECT_NE(source.error().find("0x70"), std::string::npos) << source.error();
        EXPECT_EQ(source.error().find("barometer"), std::string::npos) << source.error();
    }
    {
        Rig rig;
        rig.bus->registers(kBaro)[Bmp280::kRegId] = 0x55;
        load_imu(*rig.bus, 0x70);
        LiveSensorSource source = rig.make();
        EXPECT_FALSE(source.ok());
        EXPECT_NE(source.error().find("barometer: "), std::string::npos) << source.error();
        EXPECT_NE(source.error().find("; IMU: "), std::string::npos) << source.error();
    }
}

// TC-016 step 10. The configured IMU rate reaches the driver: 50 Hz is
// SMPLRT_DIV 19 (1 kHz / (1 + 19)), written during init; the default
// stays 100 Hz, divider 9 (TC-013 step 1). (REQ-SENS-003, REQ-CFG-001)
TEST(LiveSensorSourceTest, ImuSampleRateReachesTheDriver) {
    Rig fifty;
    LiveSensorSource at_fifty = fifty.make(50);
    ASSERT_TRUE(at_fifty.ok()) << at_fifty.error();
    EXPECT_EQ(fifty.bus->registers(kImu)[Mpu6050::kRegSampleRateDivider], 19);

    Rig hundred;
    LiveSensorSource at_default = hundred.make();
    ASSERT_TRUE(at_default.ok()) << at_default.error();
    EXPECT_EQ(hundred.bus->registers(kImu)[Mpu6050::kRegSampleRateDivider], 9);
}

// A rate the chip's divider cannot produce refuses startup with the rate
// named, the driver's own check (TC-013 step 5) surfacing through the
// source's error. The configuration's validation refuses it earlier
// still; this is the second line. (REQ-SENS-003, REQ-CFG-002)
TEST(LiveSensorSourceTest, UnreachableImuRateRefusesStartup) {
    Rig rig;
    LiveSensorSource source = rig.make(30);
    EXPECT_FALSE(source.ok());
    EXPECT_NE(source.error().find("IMU: sample rate 30 Hz"), std::string::npos) << source.error();
}
