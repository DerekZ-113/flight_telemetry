// Tests for src/drivers/live_sensor_source (REQ-SENS-001 through
// REQ-SENS-005, REQ-LOG-004). Test card TC-014, steps 5 through 10, and
// TC-018, steps 18 and 19. Both chips sit on one fake bus, as they do on
// the real one: the barometer with the datasheet's worked example, the
// MPU6050 with one g on its z axis; the receiver, when a test wants one,
// on a fake serial port fed the manual's sentences.

#include <gtest/gtest.h>

#include <memory>

#include "bmp280_example.h"
#include "drivers/live_sensor_source.h"
#include "drivers/mpu6050.h"
#include "fake_clock.h"
#include "fake_i2c_bus.h"
#include "fake_serial_port.h"
#include "nmea_examples.h"

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
// stays with the test so it can fail transactions after the move. The
// same for the receiver's port, handed over only by make_with_gps().
struct Rig {
    std::unique_ptr<FakeI2cBus> owned = std::make_unique<FakeI2cBus>();
    FakeI2cBus* bus = owned.get();
    std::unique_ptr<FakeSerialPort> gps_owned = std::make_unique<FakeSerialPort>();
    FakeSerialPort* gps = gps_owned.get();
    FakeClock clock;

    Rig() {
        bmp280_example::load_chip(*bus, kBaro);
        load_imu(*bus);
    }

    LiveSensorSource make(int imu_sample_rate_hz = Mpu6050::kDefaultSampleRateHz) {
        return LiveSensorSource(std::move(owned), kBaro, kImu, clock, imu_sample_rate_hz);
    }

    LiveSensorSource make_with_gps() {
        return LiveSensorSource(std::move(owned), kBaro, kImu, clock, Mpu6050::kDefaultSampleRateHz,
                                std::move(gps_owned));
    }
};

std::string epoch() {
    return std::string(nmea_examples::kGga) + "\r\n" + nmea_examples::kRmcSameEpoch + "\r\n";
}

// The first second after start (frames at 0 to 980 ms), with nothing on
// the port: the window in which the source discards a fix as the device's
// backlog. Leaves the clock at 1000 ms for the next frame.
void run_startup(LiveSensorSource& source, Rig& rig) {
    for (int i = 0; i < 50; ++i) {
        const TelemetryFrame f = source.read_frame();
        EXPECT_FALSE(f.gps_read_ok) << i;
        rig.clock.advance(20'000'000);
    }
}

}  // namespace

// TC-014 step 5. One frame from both chips: raw fields in the frame's
// units, both I2C channels fresh, GPS absent and never fresh, every
// status NOMINAL, every computed field untouched and unjudged (the raw-only
// contract of design.md section 3), and the first timestamp zero.
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
    EXPECT_EQ(f.gps_status, ChannelStatus::NOT_AVAILABLE);   // no receiver on this rig (REQ-FAULT-007)
    EXPECT_EQ(f.baro_altitude_m, 0.0f);
    EXPECT_EQ(f.pitch_deg, 0.0f);
    EXPECT_EQ(f.roll_deg, 0.0f);
    EXPECT_EQ(f.fused_altitude_m, 0.0f);
    EXPECT_EQ(f.vertical_speed_mps, 0.0f);
    EXPECT_EQ(f.baro_altitude_validity, Validity::NOT_JUDGED);   // the processor judges (REQ-PROC-007)
    EXPECT_EQ(f.pitch_validity, Validity::NOT_JUDGED);
    EXPECT_EQ(f.roll_validity, Validity::NOT_JUDGED);
    EXPECT_EQ(f.fused_altitude_validity, Validity::NOT_JUDGED);
    EXPECT_EQ(f.vertical_speed_validity, Validity::NOT_JUDGED);
    EXPECT_EQ(f.reserved_a, 0u);
    EXPECT_EQ(f.reserved_b, 0u);
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

// TC-018 step 18. A fix is fresh on the one cycle its epoch completed and
// on no other: the receiver's 1 Hz measurement, applied once by the
// processor (REQ-PROC-006); the fields held between fixes and zero before
// the first; the raw-only contract untouched.
TEST(LiveSensorSourceTest, FixIsFreshOnlyOnTheCycleItCompleted) {
    Rig rig;
    LiveSensorSource source = rig.make_with_gps();
    ASSERT_TRUE(source.ok()) << source.error();

    // The first second with nothing on the port, and no fix to report yet.
    EXPECT_FALSE(source.last_gps_fix().has_value());
    run_startup(source, rig);
    EXPECT_FALSE(source.last_gps_fix().has_value());
    EXPECT_EQ(source.gps_startup_discarded(), 0u);
    // The epoch arrives in time for the frame at 1000 ms.
    rig.gps->queue(epoch());
    const TelemetryFrame fix = source.read_frame();
    EXPECT_TRUE(fix.gps_read_ok);
    EXPECT_NEAR(fix.latitude, nmea_examples::kGgaLatitudeDeg, 1e-7);
    EXPECT_NEAR(fix.longitude, nmea_examples::kGgaLongitudeDeg, 1e-7);
    EXPECT_FLOAT_EQ(fix.gps_altitude_m, nmea_examples::kGgaAltitudeM);
    EXPECT_NEAR(fix.ground_speed_mps, nmea_examples::kRmcSpeedMps, 1e-6);
    EXPECT_FLOAT_EQ(fix.heading_deg, nmea_examples::kRmcCourseDeg);
    EXPECT_TRUE(fix.baro_read_ok);   // the chips are read on the same cycle as before
    EXPECT_TRUE(fix.imu_read_ok);
    // The raw-only contract: every status NOMINAL, every validity unjudged.
    EXPECT_EQ(fix.baro_status, ChannelStatus::NOMINAL);
    EXPECT_EQ(fix.imu_status, ChannelStatus::NOMINAL);
    EXPECT_EQ(fix.gps_status, ChannelStatus::NOMINAL);
    EXPECT_EQ(fix.baro_altitude_validity, Validity::NOT_JUDGED);
    EXPECT_EQ(fix.pitch_validity, Validity::NOT_JUDGED);
    EXPECT_EQ(fix.roll_validity, Validity::NOT_JUDGED);
    EXPECT_EQ(fix.fused_altitude_validity, Validity::NOT_JUDGED);
    EXPECT_EQ(fix.vertical_speed_validity, Validity::NOT_JUDGED);
    EXPECT_EQ(fix.timestamp_ms, 1000u);
    // What the exit line reports about the last fix.
    const std::optional<GpsFix> last = source.last_gps_fix();
    ASSERT_TRUE(last.has_value());
    EXPECT_EQ(last->fix_quality, nmea_examples::kGgaQuality);
    EXPECT_EQ(last->satellites, nmea_examples::kGgaSatellites);
    EXPECT_EQ(last->utc_time, "092725.00");

    // The forty-nine after: the fix held, not fresh.
    for (int i = 0; i < 49; ++i) {
        rig.clock.advance(20'000'000);
        const TelemetryFrame f = source.read_frame();
        EXPECT_FALSE(f.gps_read_ok) << i;
        EXPECT_EQ(f.latitude, fix.latitude) << i;
        EXPECT_EQ(f.longitude, fix.longitude) << i;
        EXPECT_EQ(f.gps_altitude_m, fix.gps_altitude_m) << i;
        EXPECT_EQ(f.ground_speed_mps, fix.ground_speed_mps) << i;
        EXPECT_EQ(f.heading_deg, fix.heading_deg) << i;
    }
    const std::optional<GpsCounters> counters = source.gps_counters();
    ASSERT_TRUE(counters.has_value());
    EXPECT_EQ(counters->fixes, 1u);
    EXPECT_EQ(counters->sentences, 2u);
}

// A sentence cut across two cycles completes on the second, and the fix is
// fresh on that cycle alone.
TEST(LiveSensorSourceTest, EpochSplitAcrossCyclesIsFreshWhenItCompletes) {
    Rig rig;
    LiveSensorSource source = rig.make_with_gps();
    ASSERT_TRUE(source.ok()) << source.error();
    run_startup(source, rig);
    const std::string bytes = epoch();
    rig.gps->queue(bytes.substr(0, 100));
    EXPECT_FALSE(source.read_frame().gps_read_ok);
    rig.gps->queue(bytes.substr(100));
    EXPECT_TRUE(source.read_frame().gps_read_ok);
    EXPECT_FALSE(source.read_frame().gps_read_ok);
}

// The first second after start belongs to the device's backlog: a USB
// receiver hands over what it buffered while the port was closed, after
// the flush at open, and those epochs are old. A fix that completes in that
// second is counted and discarded, never fresh; the first fix delivered is
// a live one.
TEST(LiveSensorSourceTest, FixesInTheFirstSecondAreDiscarded) {
    Rig rig;
    LiveSensorSource source = rig.make_with_gps();
    ASSERT_TRUE(source.ok()) << source.error();
    rig.gps->queue(epoch());             // the backlog, waiting when the loop starts
    const TelemetryFrame first = source.read_frame();
    EXPECT_FALSE(first.gps_read_ok);
    EXPECT_EQ(first.latitude, 0.0);
    EXPECT_EQ(source.gps_startup_discarded(), 1u);
    EXPECT_FALSE(source.last_gps_fix().has_value());
    for (int i = 1; i < 50; ++i) {
        rig.clock.advance(20'000'000);
        rig.gps->queue(epoch());         // a backlog that keeps coming
        const TelemetryFrame f = source.read_frame();
        EXPECT_FALSE(f.gps_read_ok) << i;
        EXPECT_EQ(f.latitude, 0.0) << i;
    }
    EXPECT_EQ(source.gps_startup_discarded(), 50u);
    rig.clock.advance(20'000'000);       // 1000 ms: the grace is over
    rig.gps->queue(epoch());
    const TelemetryFrame live = source.read_frame();
    EXPECT_TRUE(live.gps_read_ok);
    EXPECT_NEAR(live.latitude, nmea_examples::kGgaLatitudeDeg, 1e-7);
    EXPECT_EQ(source.gps_startup_discarded(), 50u);
    ASSERT_TRUE(source.last_gps_fix().has_value());
}

// TC-018 step 19. Without a port the receiver is absent: never fresh, the
// fields zero, no counters; the eight cases above hold unchanged.
TEST(LiveSensorSourceTest, WithoutAPortTheGpsIsAbsent) {
    Rig rig;
    LiveSensorSource source = rig.make();
    ASSERT_TRUE(source.ok()) << source.error();
    for (int i = 0; i < 3; ++i) {
        const TelemetryFrame f = source.read_frame();
        EXPECT_FALSE(f.gps_read_ok);
        EXPECT_EQ(f.latitude, 0.0);
        EXPECT_EQ(f.heading_deg, 0.0f);
        EXPECT_EQ(f.gps_status, ChannelStatus::NOT_AVAILABLE);
    }
    EXPECT_FALSE(source.gps_counters().has_value());
    EXPECT_FALSE(source.last_gps_fix().has_value());
    EXPECT_EQ(source.gps_startup_discarded(), 0u);
}

// TC-017 step 18 (REQ-FAULT-007, FAULT-018). Without a receiver the GPS
// channel is not provided: the source reports it NOT_AVAILABLE on every
// frame, the fields zero and never fresh, so the detector leaves it alone
// and no timeout and no event ever follow. Until 0.12.0 the source
// reported NOMINAL and the detector declared a failure at 2 s on every
// bench run without a receiver. The two chips stay NOMINAL, for the
// detector to judge.
TEST(LiveSensorSourceTest, GpsWithoutAReceiverIsNotAvailable) {
    Rig rig;
    LiveSensorSource source = rig.make();
    ASSERT_TRUE(source.ok()) << source.error();
    for (int i = 0; i < 100; ++i) {
        rig.clock.advance(20'000'000);
        const TelemetryFrame f = source.read_frame();
        EXPECT_EQ(f.gps_status, ChannelStatus::NOT_AVAILABLE) << i;
        EXPECT_FALSE(f.gps_read_ok) << i;
        EXPECT_EQ(f.latitude, 0.0) << i;
        EXPECT_EQ(f.gps_altitude_m, 0.0f) << i;
        EXPECT_EQ(f.baro_status, ChannelStatus::NOMINAL) << i;
        EXPECT_EQ(f.imu_status, ChannelStatus::NOMINAL) << i;
    }
}

// A port that fails leaves the channel not fresh and counts the error; the
// detector's timeout is the report (FAULT-003). (An unplugged USB receiver
// is not this case: Linux hangs the descriptor up and reads return 0, so
// the channel times out with no error counted.)
TEST(LiveSensorSourceTest, PortErrorLeavesTheGpsNotFresh) {
    Rig rig;
    rig.gps->fail_every_read();
    LiveSensorSource source = rig.make_with_gps();
    ASSERT_TRUE(source.ok()) << source.error();
    const TelemetryFrame f = source.read_frame();
    EXPECT_FALSE(f.gps_read_ok);
    EXPECT_EQ(f.latitude, 0.0);
    const std::optional<GpsCounters> counters = source.gps_counters();
    ASSERT_TRUE(counters.has_value());
    EXPECT_EQ(counters->read_errors, 1u);
}

// A receiver without a fix delivers nothing to the frame: FAULT-003 (b)
// applied at the source, so the channel times out like a silent one.
TEST(LiveSensorSourceTest, NoFixEpochLeavesTheGpsNotFresh) {
    Rig rig;
    LiveSensorSource source = rig.make_with_gps();
    ASSERT_TRUE(source.ok()) << source.error();
    rig.gps->queue(std::string(nmea_examples::kNoFixGga) + "\r\n" + nmea_examples::kNoFixRmc + "\r\n");
    const TelemetryFrame f = source.read_frame();
    EXPECT_FALSE(f.gps_read_ok);
    EXPECT_EQ(f.latitude, 0.0);
    EXPECT_EQ(f.gps_altitude_m, 0.0f);
    EXPECT_EQ(source.gps_counters()->no_fix_epochs, 1u);
}
