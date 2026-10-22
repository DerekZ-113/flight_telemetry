#include <gtest/gtest.h>

#include <cerrno>
#include <cstdint>
#include <fstream>
#include <iterator>
#include <optional>
#include <string>
#include <vector>

#include "drivers/nmea_gps.h"
#include "fake_serial_port.h"
#include "nmea_examples.h"

// TC-018 steps 10 through 17: the driver over the fake port. Framing,
// pairing, the fix rule and the counters, with the byte stream cut
// wherever a test wants.

namespace ex = nmea_examples;

namespace {

std::string line(const std::string& sentence) {
    return sentence + "\r\n";
}

// The manual's GGA and the RMC of the same epoch (the same UTC time field).
std::string first_epoch() {
    return line(ex::kGga) + line(ex::kRmcSameEpoch);
}

std::string second_epoch() {
    return line(ex::kGgaSecondEpoch) + line(ex::kRmcSecondEpoch);
}

// Polls until a fix arrives or the port runs dry; the fix, if any.
std::optional<GpsFix> poll_until_fix(NmeaGps& gps, const FakeSerialPort& port) {
    for (int i = 0; i < 10000; ++i) {
        const std::optional<GpsFix> fix = gps.poll();
        if (fix.has_value()) {
            return fix;
        }
        if (port.empty()) {
            return std::nullopt;
        }
    }
    return std::nullopt;
}

void expect_manual_fix(const GpsFix& fix) {
    EXPECT_NEAR(fix.latitude_deg, ex::kGgaLatitudeDeg, 1e-7);
    EXPECT_NEAR(fix.longitude_deg, ex::kGgaLongitudeDeg, 1e-7);
    EXPECT_FLOAT_EQ(fix.altitude_m, ex::kGgaAltitudeM);
    EXPECT_NEAR(fix.ground_speed_mps, ex::kRmcSpeedMps, 1e-6);
    EXPECT_FLOAT_EQ(fix.course_deg, ex::kRmcCourseDeg);
    EXPECT_EQ(fix.fix_quality, ex::kGgaQuality);
    EXPECT_EQ(fix.satellites, ex::kGgaSatellites);
    EXPECT_EQ(fix.utc_time, "092725.00");
}

void expect_same_counters(const GpsCounters& a, const GpsCounters& b) {
    EXPECT_EQ(a.bytes, b.bytes);
    EXPECT_EQ(a.sentences, b.sentences);
    EXPECT_EQ(a.used, b.used);
    EXPECT_EQ(a.bad_checksum, b.bad_checksum);
    EXPECT_EQ(a.malformed, b.malformed);
    EXPECT_EQ(a.overlong, b.overlong);
    EXPECT_EQ(a.resyncs, b.resyncs);
    EXPECT_EQ(a.incomplete_epochs, b.incomplete_epochs);
    EXPECT_EQ(a.no_fix_epochs, b.no_fix_epochs);
    EXPECT_EQ(a.fixes, b.fixes);
    EXPECT_EQ(a.fixes_skipped, b.fixes_skipped);
    EXPECT_EQ(a.read_errors, b.read_errors);
}

// Queues `text` in pieces of `cut` bytes.
void queue_cut(FakeSerialPort& port, const std::string& text, size_t cut) {
    for (size_t at = 0; at < text.size(); at += cut) {
        port.queue(text.substr(at, cut));
    }
}

std::string read_file(const char* path) {
    std::ifstream in(path, std::ios::binary);
    return std::string(std::istreambuf_iterator<char>(in), std::istreambuf_iterator<char>());
}

struct CaptureRun {
    std::vector<GpsFix> fixes;
    GpsCounters counters;
};

// Streams `capture` in pieces of `cut` bytes, polling after each, then
// polls until the port is empty: a poll reads at most kMaxBytesPerPoll.
CaptureRun stream_capture(const std::string& capture, size_t cut) {
    FakeSerialPort port;
    NmeaGps gps(port);
    CaptureRun run;
    const auto poll = [&] {
        if (const std::optional<GpsFix> fix = gps.poll()) {
            run.fixes.push_back(*fix);
        }
    };
    for (size_t at = 0; at < capture.size(); at += cut) {
        port.queue(capture.substr(at, cut));
        poll();
    }
    while (!port.empty()) {
        poll();
    }
    run.counters = gps.counters();
    return run;
}

void expect_same_fix(const GpsFix& a, const GpsFix& b) {
    EXPECT_EQ(a.latitude_deg, b.latitude_deg);
    EXPECT_EQ(a.longitude_deg, b.longitude_deg);
    EXPECT_EQ(a.altitude_m, b.altitude_m);
    EXPECT_EQ(a.ground_speed_mps, b.ground_speed_mps);
    EXPECT_EQ(a.course_deg, b.course_deg);
    EXPECT_EQ(a.fix_quality, b.fix_quality);
    EXPECT_EQ(a.satellites, b.satellites);
    EXPECT_EQ(a.utc_time, b.utc_time);
}

}  // namespace

// Step 10: one epoch, one fix.
TEST(NmeaGpsTest, OneEpochGivesOneFix) {
    FakeSerialPort port;
    NmeaGps gps(port);
    port.queue(first_epoch());
    const std::optional<GpsFix> fix = gps.poll();
    ASSERT_TRUE(fix.has_value());
    expect_manual_fix(*fix);
    const GpsCounters& c = gps.counters();
    EXPECT_EQ(c.bytes, first_epoch().size());
    EXPECT_EQ(c.sentences, 2u);
    EXPECT_EQ(c.used, 2u);
    EXPECT_EQ(c.fixes, 1u);
    EXPECT_EQ(c.bad_checksum, 0u);
    EXPECT_EQ(c.malformed, 0u);
    EXPECT_EQ(c.overlong, 0u);
    EXPECT_EQ(c.resyncs, 0u);
    EXPECT_EQ(c.incomplete_epochs, 0u);
    EXPECT_EQ(c.no_fix_epochs, 0u);
    EXPECT_EQ(c.fixes_skipped, 0u);
    EXPECT_EQ(c.read_errors, 0u);
    // A reading is delivered once: the next poll, with nothing new, is empty.
    EXPECT_FALSE(gps.poll().has_value());
    EXPECT_EQ(gps.counters().fixes, 1u);
}

TEST(NmeaGpsTest, RmcBeforeGgaPairsToo) {
    FakeSerialPort port;
    NmeaGps gps(port);
    port.queue(line(ex::kRmcSameEpoch) + line(ex::kGga));
    const std::optional<GpsFix> fix = gps.poll();
    ASSERT_TRUE(fix.has_value());
    expect_manual_fix(*fix);
}

TEST(NmeaGpsTest, OtherSentencesCountButAreNotUsed) {
    FakeSerialPort port;
    NmeaGps gps(port);
    port.queue(line(ex::kVtg) + line(ex::kGsa) + line(ex::kGsv) + line(ex::kTxt) + line(ex::kGll) + line(ex::kPubx));
    EXPECT_FALSE(gps.poll().has_value());
    EXPECT_EQ(gps.counters().sentences, 6u);
    EXPECT_EQ(gps.counters().used, 0u);
    EXPECT_EQ(gps.counters().bad_checksum, 0u);
}

TEST(NmeaGpsTest, LineFeedAloneEndsASentence) {
    FakeSerialPort port;
    NmeaGps gps(port);
    port.queue(std::string(ex::kGga) + "\n" + ex::kRmcSameEpoch + "\n");
    ASSERT_TRUE(gps.poll().has_value());
}

// Step 11: the same bytes, cut anywhere.
TEST(NmeaGpsTest, TheSameBytesCutAnywhereGiveTheSameFix) {
    FakeSerialPort whole_port;
    NmeaGps whole(whole_port);
    whole_port.queue(first_epoch());
    const std::optional<GpsFix> reference = whole.poll();
    ASSERT_TRUE(reference.has_value());

    for (size_t cut : {size_t{1}, size_t{7}}) {
        FakeSerialPort port;
        NmeaGps gps(port);
        queue_cut(port, first_epoch(), cut);
        const std::optional<GpsFix> fix = poll_until_fix(gps, port);
        ASSERT_TRUE(fix.has_value()) << "cut " << cut;
        expect_manual_fix(*fix);
        expect_same_counters(gps.counters(), whole.counters());
    }

    // A cut at the star: the checksum digits arrive in the next read.
    FakeSerialPort port;
    NmeaGps gps(port);
    const std::string bytes = first_epoch();
    const size_t star = bytes.find('*');
    port.queue(bytes.substr(0, star + 1));
    port.queue(bytes.substr(star + 1));
    const std::optional<GpsFix> fix = poll_until_fix(gps, port);
    ASSERT_TRUE(fix.has_value());
    expect_manual_fix(*fix);
    expect_same_counters(gps.counters(), whole.counters());
}

// Step 12: garbage between sentences.
TEST(NmeaGpsTest, GarbageBetweenSentencesIsDiscardedAndCounted) {
    FakeSerialPort port;
    NmeaGps gps(port);
    std::string corrupt = ex::kGga;
    corrupt.back() = 'A';   // *5B becomes *5A
    port.queue_bytes({0x00, 0xFF, 0x13, 0x0A});                 // junk before any sentence
    port.queue(std::string(ex::kGga).substr(0, 20));              // half a sentence, then a new one starts
    port.queue(line(corrupt) + first_epoch());
    port.queue_pause();   // the receiver's second between two epochs
    port.queue(second_epoch());
    const std::optional<GpsFix> first = gps.poll();
    ASSERT_TRUE(first.has_value());
    expect_manual_fix(*first);
    const std::optional<GpsFix> second = gps.poll();
    ASSERT_TRUE(second.has_value());
    EXPECT_EQ(second->utc_time, "092726.00");
    EXPECT_FLOAT_EQ(second->altitude_m, 500.6f);
    const GpsCounters& c = gps.counters();
    EXPECT_EQ(c.fixes, 2u);
    EXPECT_EQ(c.bad_checksum, 1u);
    EXPECT_GE(c.resyncs, 2u);   // the junk run, and the half sentence
    EXPECT_EQ(c.malformed, 0u);
    EXPECT_EQ(c.sentences, 4u);
}

// Step 13: no fix.
TEST(NmeaGpsTest, NoFixEpochIsNotAReading) {
    FakeSerialPort port;
    NmeaGps gps(port);
    port.queue(line(ex::kNoFixGga) + line(ex::kNoFixRmc));
    EXPECT_FALSE(gps.poll().has_value());
    EXPECT_EQ(gps.counters().no_fix_epochs, 1u);
    EXPECT_EQ(gps.counters().sentences, 2u);
    EXPECT_EQ(gps.counters().fixes, 0u);
    port.queue(line(ex::kNoFixGgaWithTime) + line(ex::kNoFixRmcWithTime));
    EXPECT_FALSE(gps.poll().has_value());
    EXPECT_EQ(gps.counters().no_fix_epochs, 2u);
    // Then the sky clears.
    port.queue(first_epoch());
    EXPECT_TRUE(gps.poll().has_value());
    EXPECT_EQ(gps.counters().fixes, 1u);
}

// Both halves of the receiver's verdict: a GGA with quality 1 paired with
// an RMC that says V is no fix, counted and dropped, and the next epoch
// pairs on its own. The three no-fix epochs above have quality 0, so the
// record showed the RMC's operand of the fix rule never false (FTS-VR-001
// Entry 13 §13.7).
TEST(NmeaGpsTest, FixQualityGgaWithStatusVRmcIsNoFix) {
    FakeSerialPort port;
    NmeaGps gps(port);
    port.queue(line(ex::kGga) + line(ex::kRmcStatusVSameEpoch));
    EXPECT_FALSE(gps.poll().has_value());
    EXPECT_EQ(gps.counters().sentences, 2u);
    EXPECT_EQ(gps.counters().used, 2u);
    EXPECT_EQ(gps.counters().malformed, 0u);
    EXPECT_EQ(gps.counters().no_fix_epochs, 1u);
    EXPECT_EQ(gps.counters().incomplete_epochs, 0u);
    EXPECT_EQ(gps.counters().fixes, 0u);
    // The pair was consumed: the next epoch pairs on its own.
    port.queue(first_epoch());
    const std::optional<GpsFix> fix = gps.poll();
    ASSERT_TRUE(fix.has_value());
    expect_manual_fix(*fix);
    EXPECT_EQ(gps.counters().incomplete_epochs, 0u);
    EXPECT_EQ(gps.counters().fixes, 1u);
}

// Step 14: an overlong line.
TEST(NmeaGpsTest, OverlongLineIsDroppedAndTheNextSentenceStillParses) {
    FakeSerialPort port;
    NmeaGps gps(port);
    port.queue("$" + std::string(200, 'A') + first_epoch());
    const std::optional<GpsFix> fix = gps.poll();
    ASSERT_TRUE(fix.has_value());
    expect_manual_fix(*fix);
    EXPECT_EQ(gps.counters().overlong, 1u);
    EXPECT_EQ(gps.counters().bad_checksum, 0u);
}

// Step 15: GGA without RMC.
TEST(NmeaGpsTest, GgaWithoutRmcCompletesNothing) {
    FakeSerialPort port;
    NmeaGps gps(port);
    port.queue(line(ex::kGga) + line(ex::kGgaThirdEpoch) + line(ex::kGgaFourthEpoch));
    EXPECT_FALSE(gps.poll().has_value());
    EXPECT_EQ(gps.counters().sentences, 3u);
    EXPECT_EQ(gps.counters().incomplete_epochs, 2u);
    EXPECT_EQ(gps.counters().no_fix_epochs, 0u);
    EXPECT_EQ(gps.counters().fixes, 0u);
}

TEST(NmeaGpsTest, StaleGgaIsReplacedByTheNextEpochsRmc) {
    FakeSerialPort port;
    NmeaGps gps(port);
    port.queue(line(ex::kGga) + line(ex::kRmcSecondEpoch) + line(ex::kGgaSecondEpoch));
    const std::optional<GpsFix> fix = gps.poll();
    ASSERT_TRUE(fix.has_value());
    EXPECT_EQ(fix->utc_time, "092726.00");
    EXPECT_EQ(gps.counters().incomplete_epochs, 1u);
}

TEST(NmeaGpsTest, ASentenceArrivingTwiceReplacesItsFirstCopy) {
    FakeSerialPort port;
    NmeaGps gps(port);
    port.queue(line(ex::kRmcSameEpoch) + line(ex::kRmcSameEpoch) + line(ex::kGga));
    const std::optional<GpsFix> fix = gps.poll();
    ASSERT_TRUE(fix.has_value());
    EXPECT_EQ(gps.counters().incomplete_epochs, 1u);
    EXPECT_EQ(gps.counters().fixes, 1u);
}

TEST(NmeaGpsTest, MalformedSentencesAreCountedAndDoNotPair) {
    FakeSerialPort port;
    NmeaGps gps(port);
    // Both pass the checksum; the parser refuses both.
    port.queue(line(ex::kGgaLatitude91) + line(ex::kRmcStatusX));
    EXPECT_FALSE(gps.poll().has_value());
    EXPECT_EQ(gps.counters().sentences, 2u);
    EXPECT_EQ(gps.counters().used, 2u);
    EXPECT_EQ(gps.counters().malformed, 2u);
    EXPECT_EQ(gps.counters().bad_checksum, 0u);
    EXPECT_EQ(gps.counters().no_fix_epochs, 0u);
}

TEST(NmeaGpsTest, StaleRmcIsReplacedByTheNextEpochsGga) {
    FakeSerialPort port;
    NmeaGps gps(port);
    port.queue(line(ex::kRmcSameEpoch) + second_epoch());
    const std::optional<GpsFix> fix = gps.poll();
    ASSERT_TRUE(fix.has_value());
    EXPECT_EQ(fix->utc_time, "092726.00");
    EXPECT_EQ(gps.counters().incomplete_epochs, 1u);
}

// Step 16: two epochs in one poll; a read error.
TEST(NmeaGpsTest, TwoEpochsInOnePollDeliverTheLatest) {
    FakeSerialPort port;
    NmeaGps gps(port);
    port.queue(first_epoch() + second_epoch());
    const std::optional<GpsFix> fix = gps.poll();
    ASSERT_TRUE(fix.has_value());
    EXPECT_EQ(fix->utc_time, "092726.00");
    EXPECT_FLOAT_EQ(fix->altitude_m, 500.6f);
    EXPECT_EQ(gps.counters().fixes, 1u);
    EXPECT_EQ(gps.counters().fixes_skipped, 1u);
}

TEST(NmeaGpsTest, ReadErrorIsCountedAndEndsThePoll) {
    FakeSerialPort port;
    NmeaGps gps(port);
    port.queue(first_epoch());
    port.queue_error(EIO);
    port.queue(second_epoch());
    const std::optional<GpsFix> first = gps.poll();   // the epoch, then the error stops this poll
    ASSERT_TRUE(first.has_value());
    EXPECT_EQ(first->utc_time, "092725.00");
    EXPECT_EQ(gps.counters().read_errors, 1u);
    EXPECT_FALSE(port.empty());
    const std::optional<GpsFix> second = gps.poll();
    ASSERT_TRUE(second.has_value());
    EXPECT_EQ(second->utc_time, "092726.00");

    port.fail_every_read();
    EXPECT_FALSE(gps.poll().has_value());
    EXPECT_FALSE(gps.poll().has_value());
    EXPECT_EQ(gps.counters().read_errors, 3u);
}

// Step 17: the per-poll cap.
TEST(NmeaGpsTest, PollReadsAtMostTheCapInChunks) {
    FakeSerialPort port;
    NmeaGps gps(port);
    std::string flood;
    for (int i = 0; i < 32; ++i) {
        flood += first_epoch();
    }
    ASSERT_GT(flood.size(), NmeaGps::kMaxBytesPerPoll);
    port.queue(flood);
    gps.poll();
    ASSERT_EQ(port.reads().size(), NmeaGps::kMaxBytesPerPoll / NmeaGps::kChunkBytes);
    for (const FakeSerialPort::Read& r : port.reads()) {
        EXPECT_EQ(r.requested, NmeaGps::kChunkBytes);
        ASSERT_TRUE(r.returned.has_value());
        EXPECT_EQ(*r.returned, NmeaGps::kChunkBytes);
    }
    EXPECT_EQ(gps.counters().bytes, NmeaGps::kMaxBytesPerPoll);
    gps.poll();
    EXPECT_TRUE(port.empty());
    EXPECT_EQ(gps.counters().bytes, flood.size());
    EXPECT_EQ(gps.counters().fixes, 2u);   // one per poll, the rest skipped
    EXPECT_EQ(gps.counters().fixes + gps.counters().fixes_skipped, 32u);
}

// Step 17: the GT-U7's own 30 seconds at 9600 baud, recorded on the Pi on
// 2026-10-21 (FTS-VR-001 Entry 13 §13.12). Every position is shifted by one
// constant pair, and the altitude and the geoid separation by one constant
// each, so that the first fix lands on the manual's example; the checksums
// are recomputed and every other byte is the board's. Cut at 19 and at 7
// bytes the runs agree on everything. Queued whole, a poll reads 4096 bytes
// and delivers only the latest fix, so that run skips what the others
// deliver, and every fix it does deliver is theirs.
TEST(NmeaGpsTest, TheBoardsCaptureGivesTheSameFixesHoweverItIsCut) {
    const std::string capture = read_file(TELEMETRY_GPS_CAPTURE);
    ASSERT_FALSE(capture.empty()) << TELEMETRY_GPS_CAPTURE;

    const CaptureRun by19 = stream_capture(capture, 19);
    EXPECT_EQ(by19.counters.bytes, capture.size());
    EXPECT_EQ(by19.counters.sentences, 277u);
    EXPECT_EQ(by19.counters.used, 60u);
    EXPECT_EQ(by19.counters.bad_checksum, 0u);
    EXPECT_EQ(by19.counters.malformed, 0u);
    EXPECT_EQ(by19.counters.resyncs, 0u);
    EXPECT_EQ(by19.counters.incomplete_epochs, 0u);
    EXPECT_EQ(by19.counters.no_fix_epochs, 0u);
    EXPECT_EQ(by19.counters.fixes, 30u);
    EXPECT_EQ(by19.counters.fixes_skipped, 0u);
    ASSERT_EQ(by19.fixes.size(), 30u);
    EXPECT_NEAR(by19.fixes.front().latitude_deg, ex::kGgaLatitudeDeg, 1e-9);
    EXPECT_NEAR(by19.fixes.front().longitude_deg, ex::kGgaLongitudeDeg, 1e-9);
    EXPECT_FLOAT_EQ(by19.fixes.front().altitude_m, ex::kGgaAltitudeM);
    EXPECT_EQ(by19.fixes.front().utc_time, "191330.00");
    EXPECT_EQ(by19.fixes.back().utc_time, "191359.00");

    const CaptureRun by7 = stream_capture(capture, 7);
    expect_same_counters(by7.counters, by19.counters);
    ASSERT_EQ(by7.fixes.size(), by19.fixes.size());
    for (size_t i = 0; i < by19.fixes.size(); ++i) {
        expect_same_fix(by7.fixes[i], by19.fixes[i]);
    }

    const CaptureRun whole = stream_capture(capture, capture.size());
    EXPECT_EQ(whole.counters.bytes, by19.counters.bytes);
    EXPECT_EQ(whole.counters.sentences, by19.counters.sentences);
    EXPECT_EQ(whole.counters.used, by19.counters.used);
    EXPECT_EQ(whole.counters.bad_checksum, 0u);
    EXPECT_EQ(whole.counters.malformed, 0u);
    EXPECT_EQ(whole.counters.incomplete_epochs, 0u);
    EXPECT_EQ(whole.counters.no_fix_epochs, 0u);
    EXPECT_EQ(whole.counters.fixes + whole.counters.fixes_skipped, by19.counters.fixes);
    EXPECT_EQ(whole.fixes.size(), whole.counters.fixes);
    for (const GpsFix& fix : whole.fixes) {
        size_t match = 0;
        while (match < by19.fixes.size() && by19.fixes[match].utc_time != fix.utc_time) {
            ++match;
        }
        ASSERT_LT(match, by19.fixes.size()) << fix.utc_time;
        expect_same_fix(fix, by19.fixes[match]);
    }
}
