// Unit tests for src/logging (REQ-LOG-001, REQ-LOG-002). Test card TC-008.
// Every test writes under its own temporary directory, created in SetUp
// and removed in TearDown, so tests neither see each other's files nor
// leave anything behind.

#include <gtest/gtest.h>

#include <array>
#include <cstring>
#include <filesystem>
#include <fstream>

#include "drivers/simulated_source.h"
#include "logging/binary_logger.h"
#include "logging/log_format.h"
#include "logging/log_reader.h"

namespace fs = std::filesystem;

namespace {

constexpr uint64_t kFrameRecord = 1 + sizeof(TelemetryFrame);
constexpr uint64_t kHeader = sizeof(LogFileHeader);

class LoggingTest : public ::testing::Test {
protected:
    void SetUp() override {
        static int counter = 0;
        dir_ = fs::temp_directory_path() / ("ftlg_test_" + std::to_string(counter++));
        fs::remove_all(dir_);
        fs::create_directories(dir_);
    }
    void TearDown() override { fs::remove_all(dir_); }

    fs::path dir_;
};

bool frames_equal(const TelemetryFrame& a, const TelemetryFrame& b) {
    return std::memcmp(&a, &b, sizeof(TelemetryFrame)) == 0;
}

}  // namespace

// The header round-trips and validates against this build.
TEST_F(LoggingTest, HeaderRoundTrip) {
    {
        BinaryLogger logger(dir_, "t", 1 << 20);
        ASSERT_TRUE(logger.ok());
        logger.log_frame(TelemetryFrame{});
    }   // logger destroyed here: file flushed and closed by RAII

    LogReader reader(dir_ / "t_000.bin");
    ASSERT_TRUE(reader.ok()) << reader.error();
    EXPECT_EQ(reader.header().version, kLogFormatVersion);
    EXPECT_EQ(kLogFormatVersion, 2);
    EXPECT_EQ(kOldestReadableLogFormatVersion, 1);
    EXPECT_EQ(reader.header().frame_size, sizeof(TelemetryFrame));
    EXPECT_EQ(reader.header().event_size, sizeof(FaultEvent));
    EXPECT_EQ(reader.header().endian_marker, kEndianMarker);
}

// Bytes in, same bytes out. memcmp, not field-by-field with tolerance:
// a log that changes one bit is not a log.
TEST_F(LoggingTest, FrameRoundTripIsBitExact) {
    SimulatedSource source(20, 42);
    std::vector<TelemetryFrame> written;
    {
        BinaryLogger logger(dir_, "t", 1 << 20);
        for (int i = 0; i < 10; i++) {
            written.push_back(source.read_frame());
            logger.log_frame(written.back());
        }
    }

    LogReader reader(dir_ / "t_000.bin");
    ASSERT_TRUE(reader.ok()) << reader.error();
    for (size_t i = 0; i < written.size(); i++) {
        std::optional<TelemetryFrame> f = reader.next_frame();
        ASSERT_TRUE(f.has_value()) << "frame " << i;
        EXPECT_TRUE(frames_equal(*f, written[i])) << "frame " << i;
    }
    EXPECT_FALSE(reader.next_frame().has_value());
}

// Frames and events share one stream, in order, told apart by the tag.
TEST_F(LoggingTest, EventRecordRoundTrip) {
    const FaultEvent event{1234, Channel::IMU, FaultType::STUCK, 0, 0.5f};
    {
        BinaryLogger logger(dir_, "t", 1 << 20);
        logger.log_frame(TelemetryFrame{});
        logger.log_event(event);
        logger.log_frame(TelemetryFrame{});
    }

    LogReader reader(dir_ / "t_000.bin");
    ASSERT_TRUE(reader.ok());
    LogRecord r;
    ASSERT_TRUE(reader.next(r)); EXPECT_EQ(r.type, RecordType::FRAME);
    ASSERT_TRUE(reader.next(r)); EXPECT_EQ(r.type, RecordType::FAULT_EVENT);
    EXPECT_EQ(std::memcmp(&r.event, &event, sizeof(event)), 0);
    ASSERT_TRUE(reader.next(r)); EXPECT_EQ(r.type, RecordType::FRAME);
    EXPECT_FALSE(reader.next(r));
}

TEST_F(LoggingTest, NextFrameSkipsEvents) {
    {
        BinaryLogger logger(dir_, "t", 1 << 20);
        logger.log_frame(TelemetryFrame{});
        logger.log_event(FaultEvent{1, Channel::BARO, FaultType::COMM_TIMEOUT, 0, 0.0f});
        logger.log_event(FaultEvent{2, Channel::BARO, FaultType::RECOVERY, 0, 0.0f});
        logger.log_frame(TelemetryFrame{});
    }
    LogReader reader(dir_ / "t_000.bin");
    ASSERT_TRUE(reader.ok());
    EXPECT_TRUE(reader.next_frame().has_value());
    EXPECT_TRUE(reader.next_frame().has_value());
    EXPECT_FALSE(reader.next_frame().has_value());
}

// REQ-LOG-002, TC-008. A limit that fits three frame records produces
// files of three, each with its own header, no record split, none over
// the limit.
TEST_F(LoggingTest, RotationSplitsAtRecordBoundary) {
    const uint64_t limit = kHeader + 3 * kFrameRecord;
    {
        BinaryLogger logger(dir_, "t", limit);
        for (int i = 0; i < 10; i++) {
            TelemetryFrame f{};
            f.timestamp_ms = static_cast<uint64_t>(i);
            logger.log_frame(f);
        }
        EXPECT_EQ(logger.file_index(), 3);   // files 0..3
    }

    const auto files = list_log_files(dir_, "t");
    ASSERT_EQ(files.size(), 4u);
    uint64_t total_frames = 0;
    uint64_t expected_ts = 0;
    for (const auto& file : files) {
        EXPECT_LE(fs::file_size(file), limit) << file;
        LogReader reader(file);
        ASSERT_TRUE(reader.ok()) << file << ": " << reader.error();
        while (auto f = reader.next_frame()) {
            EXPECT_EQ(f->timestamp_ms, expected_ts++);
            total_frames++;
        }
    }
    EXPECT_EQ(total_frames, 10u);
}

// A limit smaller than one record still yields one record per file
// rather than an infinite rotation loop.
TEST_F(LoggingTest, TinyLimitStillWritesOneRecordPerFile) {
    {
        BinaryLogger logger(dir_, "t", 1);
        logger.log_frame(TelemetryFrame{});
        logger.log_frame(TelemetryFrame{});
        EXPECT_EQ(logger.file_index(), 1);
    }
    EXPECT_EQ(list_log_files(dir_, "t").size(), 2u);
}

// Guards: a file that is not ours is refused before any record is read.
TEST_F(LoggingTest, ReaderRejectsBadMagic) {
    LogFileHeader h = make_log_header();
    std::memcpy(h.magic, "NOPE", 4);
    {
        std::ofstream out(dir_ / "bad.bin", std::ios::binary);
        out.write(reinterpret_cast<const char*>(&h), sizeof(h));
    }
    LogReader reader(dir_ / "bad.bin");
    EXPECT_FALSE(reader.ok());
    EXPECT_FALSE(reader.error().empty());
}

TEST_F(LoggingTest, ReaderRejectsWrongFrameSize) {
    LogFileHeader h = make_log_header();
    h.frame_size = static_cast<uint16_t>(sizeof(TelemetryFrame) + 8);   // a frame layout change
    {
        std::ofstream out(dir_ / "bad.bin", std::ios::binary);
        out.write(reinterpret_cast<const char*>(&h), sizeof(h));
    }
    LogReader reader(dir_ / "bad.bin");
    EXPECT_FALSE(reader.ok());
    EXPECT_NE(reader.error().find("frame size 112 in the header, 104 in this build"), std::string::npos)
        << reader.error();
}

TEST_F(LoggingTest, ReaderRejectsMissingFile) {
    LogReader reader(dir_ / "does_not_exist.bin");
    EXPECT_FALSE(reader.ok());
    EXPECT_NE(reader.error().find("cannot open"), std::string::npos);
}

TEST_F(LoggingTest, ReaderRejectsTruncatedHeader) {
    {
        std::ofstream out(dir_ / "short.bin", std::ios::binary);
        out.write("FTLG", 4);
    }
    LogReader reader(dir_ / "short.bin");
    EXPECT_FALSE(reader.ok());
}

// A file cut off mid-write (power loss, crash) yields every complete
// record and then stops. It must not crash and must not invent a frame.
TEST_F(LoggingTest, TornRecordStopsCleanly) {
    {
        BinaryLogger logger(dir_, "t", 1 << 20);
        logger.log_frame(TelemetryFrame{});
        logger.log_frame(TelemetryFrame{});
    }
    const fs::path file = dir_ / "t_000.bin";
    fs::resize_file(file, kHeader + kFrameRecord + 1 + 40);   // second frame half written

    LogReader reader(file);
    ASSERT_TRUE(reader.ok());
    LogRecord r;
    EXPECT_TRUE(reader.next(r));
    EXPECT_FALSE(reader.next(r));
    EXPECT_NE(reader.error().find("torn"), std::string::npos);
}

TEST_F(LoggingTest, UnknownTagStopsReader) {
    {
        BinaryLogger logger(dir_, "t", 1 << 20);
        logger.log_frame(TelemetryFrame{});
    }
    const fs::path file = dir_ / "t_000.bin";
    {
        std::ofstream out(file, std::ios::binary | std::ios::app);
        const uint8_t bogus = 0x7F;
        out.write(reinterpret_cast<const char*>(&bogus), 1);
    }
    LogReader reader(file);
    LogRecord r;
    EXPECT_TRUE(reader.next(r));
    EXPECT_FALSE(reader.next(r));
    EXPECT_FALSE(reader.ok());
}

TEST_F(LoggingTest, ListLogFilesIsSortedAndFiltered) {
    for (const char* name : {"t_002.bin", "t_000.bin", "t_001.bin", "other_000.bin", "t_003.txt"}) {
        std::ofstream(dir_ / name).put('x');
    }
    const auto files = list_log_files(dir_, "t");
    ASSERT_EQ(files.size(), 3u);
    EXPECT_EQ(files[0].filename(), "t_000.bin");
    EXPECT_EQ(files[1].filename(), "t_001.bin");
    EXPECT_EQ(files[2].filename(), "t_002.bin");
}

TEST_F(LoggingTest, LogFilePathIsZeroPadded) {
    EXPECT_EQ(log_file_path("d", "telemetry", 7).filename(), "telemetry_007.bin");
    EXPECT_EQ(log_file_path("d", "telemetry", 123).filename(), "telemetry_123.bin");
}

// ---------------------------------------------------------------------------
// Failure paths (REQ-LOG-001): the logger must never crash the pipeline.
// A logger that cannot write reports ok() false and drops records quietly;
// the processing loop keeps running (FTS-FM-001: degrade, never crash).
// ---------------------------------------------------------------------------

TEST_F(LoggingTest, UnwritableDirectoryReportsNotOk) {
    const fs::path blocked = dir_ / "blocked";
    std::ofstream(blocked).put('x');          // a file where the directory should be

    BinaryLogger logger(blocked, "t", 1 << 20);
    EXPECT_FALSE(logger.ok());
    logger.log_frame(TelemetryFrame{});       // must not throw or crash
    logger.log_event(FaultEvent{});
    EXPECT_FALSE(logger.ok());
}

// Rotation into a directory that vanished mid-session fails the same way.
TEST_F(LoggingTest, RotationIntoRemovedDirectoryReportsNotOk) {
    BinaryLogger logger(dir_, "t", 1);        // one record per file
    logger.log_frame(TelemetryFrame{});
    ASSERT_TRUE(logger.ok());
    fs::remove_all(dir_);
    logger.log_frame(TelemetryFrame{});       // forces open_next_file, which fails
    EXPECT_FALSE(logger.ok());
    logger.log_frame(TelemetryFrame{});       // early return on !ok_
    fs::create_directories(dir_);             // so TearDown has something to remove
}

TEST_F(LoggingTest, CurrentPathTracksRotation) {
    BinaryLogger logger(dir_, "t", 1);
    EXPECT_EQ(logger.current_path().filename(), "t_000.bin");
    logger.log_frame(TelemetryFrame{});
    logger.log_frame(TelemetryFrame{});
    EXPECT_EQ(logger.current_path().filename(), "t_001.bin");
}

// Each header guard is checked independently, so a mismatch in any one
// field is enough to refuse the file. A version this build does not read
// is refused by name, with the range it does read (REQ-LOG-005, TC-017
// step 2).
TEST_F(LoggingTest, ReaderRejectsWrongVersion) {
    LogFileHeader h = make_log_header();
    h.version = kLogFormatVersion + 1;
    std::ofstream(dir_ / "bad.bin", std::ios::binary).write(reinterpret_cast<const char*>(&h), sizeof(h));
    LogReader reader(dir_ / "bad.bin");
    EXPECT_FALSE(reader.ok());
    EXPECT_NE(reader.error().find("format version 3; this build reads 1 to 2"), std::string::npos)
        << reader.error();
    h.version = 0;
    std::ofstream(dir_ / "bad0.bin", std::ios::binary).write(reinterpret_cast<const char*>(&h), sizeof(h));
    EXPECT_FALSE(LogReader(dir_ / "bad0.bin").ok());
}

// REQ-LOG-005, TC-017 step 2. A version-1 file has this build's layout with
// the compiler's padding where version 2 names fields, and an old file may
// carry anything in those bytes. The reader accepts the file and hands
// back what version 2 means by them: validity NOT_JUDGED, reserved zero,
// every other field as logged.
TEST_F(LoggingTest, Version1HeaderIsReadAndSanitized) {
    LogFileHeader h = make_log_header();
    h.version = 1;
    TelemetryFrame frame{};
    frame.timestamp_ms = 40;
    frame.pressure_hpa = 1000.0f;
    frame.reserved_a = 0xDEADBEEFu;                      // stack contents in a pre-9/9 log
    frame.baro_altitude_validity = Validity::NORMAL;
    frame.pitch_validity = Validity::FAILURE_WARNING;
    frame.roll_validity = static_cast<Validity>(0x7F);
    frame.fused_altitude_validity = Validity::NORMAL;
    frame.vertical_speed_validity = Validity::NO_COMPUTED_DATA;
    frame.reserved_b = 0x55;
    FaultEvent event{};
    event.timestamp_ms = 60;
    event.channel = Channel::GPS;
    event.type = FaultType::COMM_TIMEOUT;
    event.reserved = 0x1234;
    event.value = 2020.0f;
    {
        std::ofstream out(dir_ / "v1.bin", std::ios::binary);
        out.write(reinterpret_cast<const char*>(&h), sizeof(h));
        const uint8_t frame_tag = static_cast<uint8_t>(RecordType::FRAME);
        const uint8_t event_tag = static_cast<uint8_t>(RecordType::FAULT_EVENT);
        out.write(reinterpret_cast<const char*>(&frame_tag), 1);
        out.write(reinterpret_cast<const char*>(&frame), sizeof(frame));
        out.write(reinterpret_cast<const char*>(&event_tag), 1);
        out.write(reinterpret_cast<const char*>(&event), sizeof(event));
    }

    LogReader reader(dir_ / "v1.bin");
    ASSERT_TRUE(reader.ok()) << reader.error();
    EXPECT_EQ(reader.header().version, 1);
    LogRecord r;
    ASSERT_TRUE(reader.next(r));
    ASSERT_EQ(r.type, RecordType::FRAME);
    EXPECT_EQ(r.frame.timestamp_ms, 40u);
    EXPECT_EQ(r.frame.pressure_hpa, 1000.0f);
    EXPECT_EQ(r.frame.reserved_a, 0u);
    EXPECT_EQ(r.frame.baro_altitude_validity, Validity::NOT_JUDGED);
    EXPECT_EQ(r.frame.pitch_validity, Validity::NOT_JUDGED);
    EXPECT_EQ(r.frame.roll_validity, Validity::NOT_JUDGED);
    EXPECT_EQ(r.frame.fused_altitude_validity, Validity::NOT_JUDGED);
    EXPECT_EQ(r.frame.vertical_speed_validity, Validity::NOT_JUDGED);
    EXPECT_EQ(r.frame.reserved_b, 0u);
    ASSERT_TRUE(reader.next(r));
    ASSERT_EQ(r.type, RecordType::FAULT_EVENT);
    EXPECT_EQ(r.event.timestamp_ms, 60u);
    EXPECT_EQ(r.event.reserved, 0u);
    EXPECT_EQ(r.event.value, 2020.0f);
    EXPECT_FALSE(reader.next(r));
}

TEST_F(LoggingTest, ReaderRejectsWrongEventSize) {
    LogFileHeader h = make_log_header();
    h.event_size = static_cast<uint16_t>(sizeof(FaultEvent) + 4);
    std::ofstream(dir_ / "bad.bin", std::ios::binary).write(reinterpret_cast<const char*>(&h), sizeof(h));
    EXPECT_FALSE(LogReader(dir_ / "bad.bin").ok());
}

TEST_F(LoggingTest, ReaderRejectsWrongByteOrder) {
    LogFileHeader h = make_log_header();
    h.endian_marker = 0x04030201u;            // what the other byte order would have written
    std::ofstream(dir_ / "bad.bin", std::ios::binary).write(reinterpret_cast<const char*>(&h), sizeof(h));
    EXPECT_FALSE(LogReader(dir_ / "bad.bin").ok());
}

TEST_F(LoggingTest, NextOnRejectedReaderReturnsFalse) {
    LogReader reader(dir_ / "does_not_exist.bin");
    ASSERT_FALSE(reader.ok());
    LogRecord r;
    EXPECT_FALSE(reader.next(r));
    EXPECT_FALSE(reader.next_frame().has_value());
}

TEST_F(LoggingTest, ListLogFilesOnMissingDirectoryIsEmpty) {
    EXPECT_TRUE(list_log_files(dir_ / "nope", "t").empty());
}

// A frame message is a complete one-record log file: the header, the FRAME
// tag, then the frame's bytes verbatim. Both transports send this
// (REQ-TRANS-001, REQ-TRANS-002); TC-005 step 4 at unit level.
TEST_F(LoggingTest, FrameMessageIsHeaderTagAndFrame) {
    SimulatedSource source(20, 42);
    const TelemetryFrame frame = source.read_frame();

    const std::array<uint8_t, kFrameMessageBytes> message = frame_message(frame);
    ASSERT_EQ(message.size(), kHeader + kFrameRecord);

    LogFileHeader header{};
    std::memcpy(&header, message.data(), sizeof(header));
    EXPECT_TRUE(header_is_valid(header));
    EXPECT_EQ(header.version, 2);
    const LogFileHeader expected = make_log_header();
    EXPECT_EQ(std::memcmp(&header, &expected, sizeof(header)), 0);
    EXPECT_EQ(message[kHeader], static_cast<uint8_t>(RecordType::FRAME));
    EXPECT_EQ(std::memcmp(message.data() + kHeader + 1, &frame, sizeof(frame)), 0);
}
