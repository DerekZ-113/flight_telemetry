#include <gtest/gtest.h>

#include <string>
#include <vector>

#include "drivers/nmea.h"
#include "nmea_examples.h"

// TC-018 steps 1 through 5: the parser alone, no port, no driver. The
// manual's sentences are the oracle (nmea_examples.h).

namespace ex = nmea_examples;

// Step 1: the checksum rule.

TEST(NmeaTest, ChecksumAcceptsTheManualsExamples) {
    for (const char* sentence : {ex::kGga, ex::kRmc, ex::kVtg, ex::kGll, ex::kGsa, ex::kGsv, ex::kTxt,
                                 ex::kNoFixGga, ex::kNoFixRmc}) {
        EXPECT_TRUE(nmea::checksum_ok(sentence)) << sentence;
    }
}

TEST(NmeaTest, ChecksumRejectsOneAlteredCharacter) {
    std::string digit = ex::kGga;
    digit.back() = 'A';   // *5B becomes *5A
    EXPECT_FALSE(nmea::checksum_ok(digit));
    std::string body = ex::kGga;
    body[17] = '5';       // 4717.11399 becomes 5717.11399 under the printed checksum
    EXPECT_FALSE(nmea::checksum_ok(body));
}

TEST(NmeaTest, ChecksumRequiresTheStarAndTwoDigitsAndNothingAfter) {
    const std::string whole = ex::kGga;
    EXPECT_FALSE(nmea::checksum_ok(whole.substr(0, whole.size() - 3)));   // no *5B
    EXPECT_FALSE(nmea::checksum_ok(whole.substr(0, whole.size() - 1)));   // *5
    EXPECT_FALSE(nmea::checksum_ok(whole + "\r\n"));                      // the assembler strips these first
    EXPECT_FALSE(nmea::checksum_ok(whole.substr(1)));                     // no $
    EXPECT_FALSE(nmea::checksum_ok(""));
    EXPECT_FALSE(nmea::checksum_ok("$GPGGA*ZZ"));
    EXPECT_FALSE(nmea::checksum_ok("$GPGGA*!!"));   // below '0'
    EXPECT_FALSE(nmea::checksum_ok("$GPGGA*??"));   // between '9' and 'A'
    EXPECT_FALSE(nmea::checksum_ok("$GPGGA*zz"));   // above 'f'
    EXPECT_FALSE(nmea::checksum_ok("$GPGGA*5Z"));   // one good digit, one bad
}

TEST(NmeaTest, ChecksumAcceptsLowerCaseHex) {
    std::string lower = ex::kGga;
    lower.back() = 'b';
    EXPECT_TRUE(nmea::checksum_ok(lower));
}

TEST(NmeaTest, FieldsSplitTheBodyAndKeepEmptyOnes) {
    const std::vector<std::string> f = nmea::fields(ex::kNoFixGga);
    ASSERT_EQ(f.size(), 15u);
    EXPECT_EQ(f[0], "GPGGA");
    EXPECT_EQ(f[1], "");
    EXPECT_EQ(f[6], "0");
    EXPECT_EQ(f[7], "00");
    EXPECT_EQ(f[8], "99.99");
    EXPECT_EQ(f[14], "");
    EXPECT_TRUE(nmea::fields("GPGGA,1,2*00").empty());    // no $
    EXPECT_TRUE(nmea::fields("$GPGGA,1,2").empty());      // no *
    EXPECT_TRUE(nmea::fields("").empty());
    const std::vector<std::string> one = nmea::fields("$GPTXT*00");
    ASSERT_EQ(one.size(), 1u);   // an address alone
    EXPECT_EQ(one[0], "GPTXT");
}

TEST(NmeaTest, FormatterIsTheLastThreeLettersOfAnyTalker) {
    EXPECT_EQ(nmea::formatter("GPGGA"), nmea::Formatter::GGA);
    EXPECT_EQ(nmea::formatter("GNGGA"), nmea::Formatter::GGA);
    EXPECT_EQ(nmea::formatter("GLRMC"), nmea::Formatter::RMC);
    EXPECT_EQ(nmea::formatter("GPVTG"), nmea::Formatter::OTHER);
    EXPECT_EQ(nmea::formatter("PUBX"), nmea::Formatter::OTHER);
    EXPECT_EQ(nmea::formatter("GPGGAX"), nmea::Formatter::OTHER);
    EXPECT_EQ(nmea::formatter(""), nmea::Formatter::OTHER);
}

// Step 2: GGA.

TEST(NmeaTest, GgaExampleParsesExactly) {
    const std::optional<nmea::Gga> gga = nmea::parse_gga(nmea::fields(ex::kGga));
    ASSERT_TRUE(gga.has_value());
    EXPECT_EQ(gga->utc_time, "092725.00");
    EXPECT_TRUE(gga->has_position);
    EXPECT_NEAR(gga->latitude_deg, ex::kGgaLatitudeDeg, 1e-7);
    EXPECT_NEAR(gga->longitude_deg, ex::kGgaLongitudeDeg, 1e-7);
    EXPECT_FLOAT_EQ(gga->altitude_m, ex::kGgaAltitudeM);
    EXPECT_EQ(gga->fix_quality, ex::kGgaQuality);
    EXPECT_EQ(gga->satellites, ex::kGgaSatellites);
}

TEST(NmeaTest, NoFixGgaHasQualityZeroAndNoPosition) {
    const std::optional<nmea::Gga> gga = nmea::parse_gga(nmea::fields(ex::kNoFixGga));
    ASSERT_TRUE(gga.has_value());
    EXPECT_EQ(gga->utc_time, "");
    EXPECT_FALSE(gga->has_position);
    EXPECT_EQ(gga->latitude_deg, 0.0);
    EXPECT_EQ(gga->longitude_deg, 0.0);
    EXPECT_EQ(gga->altitude_m, 0.0f);
    EXPECT_EQ(gga->fix_quality, 0);
    EXPECT_EQ(gga->satellites, 0);
}

TEST(NmeaTest, GgaEmptyQualityAndSatellitesReadZero) {
    const std::optional<nmea::Gga> gga = nmea::parse_gga(nmea::fields("$GPGGA,,,,,,,,99.99,,,,,,*00"));
    ASSERT_TRUE(gga.has_value());
    EXPECT_EQ(gga->fix_quality, 0);
    EXPECT_EQ(gga->satellites, 0);
    EXPECT_FALSE(gga->has_position);
}

TEST(NmeaTest, GgaQualityTwoIsAFixToo) {
    const std::optional<nmea::Gga> gga = nmea::parse_gga(nmea::fields(ex::kGgaQuality2));
    ASSERT_TRUE(gga.has_value());
    EXPECT_EQ(gga->fix_quality, 2);
    EXPECT_TRUE(gga->has_position);
}

TEST(NmeaTest, AnotherTalkerParsesTheSame) {
    EXPECT_TRUE(nmea::checksum_ok(ex::kGnGga));
    const std::optional<nmea::Gga> gp = nmea::parse_gga(nmea::fields(ex::kGga));
    const std::optional<nmea::Gga> gn = nmea::parse_gga(nmea::fields(ex::kGnGga));
    ASSERT_TRUE(gp.has_value());
    ASSERT_TRUE(gn.has_value());
    EXPECT_EQ(gn->latitude_deg, gp->latitude_deg);
    EXPECT_EQ(gn->longitude_deg, gp->longitude_deg);
    EXPECT_EQ(gn->altitude_m, gp->altitude_m);
    EXPECT_EQ(gn->fix_quality, gp->fix_quality);
}

// A position is latitude, longitude and altitude, because the Kalman
// filter takes the GGA's altitude: the two angles with an empty altitude
// field are no position. At quality 0 the sentence parses with
// has_position false; at quality 1 it is a fix without a position, which
// is malformed. The GCC record showed the altitude operand of
// has_position never false with a position present (FTS-VR-001 Entry 13
// §13.7): the no-fix fixture has neither.
TEST(NmeaTest, PositionWithoutAltitudeIsNoPosition) {
    EXPECT_TRUE(nmea::checksum_ok(ex::kGgaNoAltitudeQuality0));
    const std::optional<nmea::Gga> gga = nmea::parse_gga(nmea::fields(ex::kGgaNoAltitudeQuality0));
    ASSERT_TRUE(gga.has_value());
    EXPECT_FALSE(gga->has_position);
    EXPECT_EQ(gga->altitude_m, 0.0f);
    EXPECT_EQ(gga->fix_quality, 0);
    EXPECT_EQ(gga->satellites, 8);
    EXPECT_TRUE(nmea::checksum_ok(ex::kGgaNoAltitude));
    EXPECT_FALSE(nmea::parse_gga(nmea::fields(ex::kGgaNoAltitude)).has_value());
}

// Step 3: RMC.

TEST(NmeaTest, RmcExampleParsesExactly) {
    const std::optional<nmea::Rmc> rmc = nmea::parse_rmc(nmea::fields(ex::kRmc));
    ASSERT_TRUE(rmc.has_value());
    EXPECT_EQ(rmc->utc_time, "083559.00");
    EXPECT_TRUE(rmc->valid);
    EXPECT_TRUE(rmc->has_position);
    EXPECT_NEAR(rmc->latitude_deg, 47.0 + 17.11437 / 60.0, 1e-7);
    EXPECT_NEAR(rmc->longitude_deg, 8.0 + 33.91522 / 60.0, 1e-7);
    EXPECT_NEAR(rmc->speed_mps, ex::kRmcSpeedMps, 1e-6);
    EXPECT_FLOAT_EQ(rmc->course_deg, ex::kRmcCourseDeg);
}

TEST(NmeaTest, NoFixRmcIsInvalidWithNoPosition) {
    const std::optional<nmea::Rmc> rmc = nmea::parse_rmc(nmea::fields(ex::kNoFixRmc));
    ASSERT_TRUE(rmc.has_value());
    EXPECT_EQ(rmc->utc_time, "");
    EXPECT_FALSE(rmc->valid);
    EXPECT_FALSE(rmc->has_position);
    EXPECT_EQ(rmc->speed_mps, 0.0f);
    EXPECT_EQ(rmc->course_deg, 0.0f);
}

TEST(NmeaTest, RmcEmptySpeedAndCourseReadZero) {
    EXPECT_TRUE(nmea::checksum_ok(ex::kRmcEmptySpeedCourse));
    const std::optional<nmea::Rmc> rmc = nmea::parse_rmc(nmea::fields(ex::kRmcEmptySpeedCourse));
    ASSERT_TRUE(rmc.has_value());
    EXPECT_TRUE(rmc->valid);
    EXPECT_TRUE(rmc->has_position);
    EXPECT_EQ(rmc->speed_mps, 0.0f);
    EXPECT_EQ(rmc->course_deg, 0.0f);
}

TEST(NmeaTest, RmcStatusMustBeAOrV) {
    EXPECT_TRUE(nmea::checksum_ok(ex::kRmcStatusX));
    EXPECT_FALSE(nmea::parse_rmc(nmea::fields(ex::kRmcStatusX)).has_value());
}

// An empty status field is NMEA 0183's null field, "data not available":
// the sentence is well formed and the receiver gave no verdict. No verdict
// is not a fix (valid is true for an explicit A only), and nothing in the
// sentence is corrupt, so it is not malformed either, the rule nmea.h
// states for every field; it parses as the no-fix RMC does and pairs into
// an epoch the driver counts as no fix. The record showed the empty
// outcome of the status rule never taken: the no-fix fixture says V
// (Entry 13 §13.7).
TEST(NmeaTest, RmcEmptyStatusIsNotAFix) {
    EXPECT_TRUE(nmea::checksum_ok(ex::kRmcEmptyStatus));
    const std::optional<nmea::Rmc> rmc = nmea::parse_rmc(nmea::fields(ex::kRmcEmptyStatus));
    ASSERT_TRUE(rmc.has_value());
    EXPECT_FALSE(rmc->valid);
    EXPECT_TRUE(rmc->has_position);
    EXPECT_NEAR(rmc->speed_mps, ex::kRmcSpeedMps, 1e-6);
    EXPECT_FLOAT_EQ(rmc->course_deg, ex::kRmcCourseDeg);
    EXPECT_TRUE(nmea::checksum_ok(ex::kNoFixRmcEmptyStatus));
    const std::optional<nmea::Rmc> no_fix = nmea::parse_rmc(nmea::fields(ex::kNoFixRmcEmptyStatus));
    ASSERT_TRUE(no_fix.has_value());
    EXPECT_FALSE(no_fix->valid);
    EXPECT_FALSE(no_fix->has_position);
}

// Step 4: degrees and minutes to decimal degrees.

TEST(NmeaTest, DegreesFromDegreesAndMinutes) {
    const std::optional<double> north = nmea::to_degrees("4717.11399", "N");
    const std::optional<double> east = nmea::to_degrees("00833.91590", "E");
    const std::optional<double> south = nmea::to_degrees("4717.11399", "S");
    const std::optional<double> west = nmea::to_degrees("00833.91590", "W");
    ASSERT_TRUE(north.has_value() && east.has_value() && south.has_value() && west.has_value());
    EXPECT_NEAR(*north, ex::kGgaLatitudeDeg, 1e-7);
    EXPECT_NEAR(*east, ex::kGgaLongitudeDeg, 1e-7);
    EXPECT_NEAR(*south, -ex::kGgaLatitudeDeg, 1e-7);
    EXPECT_NEAR(*west, -ex::kGgaLongitudeDeg, 1e-7);
}

TEST(NmeaTest, DegreesRefuseImpossibleValues) {
    EXPECT_FALSE(nmea::to_degrees("4760.00000", "N").has_value());    // sixty minutes
    EXPECT_FALSE(nmea::to_degrees("9117.11399", "N").has_value());    // 91 degrees of latitude
    EXPECT_FALSE(nmea::to_degrees("18100.00000", "E").has_value());   // 181 degrees of longitude
    EXPECT_FALSE(nmea::to_degrees("4717.11399", "X").has_value());    // no such hemisphere
    EXPECT_FALSE(nmea::to_degrees("4717.11399", "").has_value());
    EXPECT_FALSE(nmea::to_degrees("abc", "N").has_value());
    EXPECT_FALSE(nmea::to_degrees("", "N").has_value());
    EXPECT_FALSE(nmea::to_degrees("-4717.11399", "N").has_value());   // the sign is the hemisphere's job
    EXPECT_TRUE(nmea::to_degrees("9000.00000", "N").has_value());     // the pole itself is a place
}

// Step 5: malformed and foreign sentences.

TEST(NmeaTest, UnparsableFieldIsMalformed) {
    EXPECT_TRUE(nmea::checksum_ok(ex::kGgaBadSatellites));
    EXPECT_FALSE(nmea::parse_gga(nmea::fields(ex::kGgaBadSatellites)).has_value());
    EXPECT_FALSE(nmea::parse_gga(nmea::fields(ex::kGgaLatitude91)).has_value());
    EXPECT_FALSE(nmea::parse_gga(nmea::fields(ex::kGgaMinutes60)).has_value());
    EXPECT_FALSE(nmea::parse_gga(nmea::fields("$GPGGA,092725.00,4717.11399,N,00833.91590,E,1,08,1.01,49x.6,M,48.0,M,,*00")).has_value());
    EXPECT_FALSE(nmea::parse_rmc(nmea::fields("$GPRMC,083559.00,A,4717.11437,N,00833.91522,E,fast,77.52,091202,,,A*00")).has_value());
    // Numbers strtod reads that no receiver means: not a number, infinite.
    EXPECT_FALSE(nmea::parse_gga(nmea::fields("$GPGGA,092725.00,4717.11399,N,00833.91590,E,1,08,1.01,nan,M,48.0,M,,*00")).has_value());
    EXPECT_FALSE(nmea::parse_gga(nmea::fields("$GPGGA,092725.00,4717.11399,N,00833.91590,E,1,08,1.01,inf,M,48.0,M,,*00")).has_value());
    // Counts outside their ranges.
    EXPECT_FALSE(nmea::parse_gga(nmea::fields("$GPGGA,092725.00,4717.11399,N,00833.91590,E,10,08,1.01,499.6,M,48.0,M,,*00")).has_value());
    EXPECT_FALSE(nmea::parse_gga(nmea::fields("$GPGGA,092725.00,4717.11399,N,00833.91590,E,1,-1,1.01,499.6,M,48.0,M,,*00")).has_value());
    // A position with one half missing, either half.
    EXPECT_FALSE(nmea::parse_gga(nmea::fields("$GPGGA,092725.00,4717.11399,N,,,1,08,1.01,499.6,M,48.0,M,,*00")).has_value());
    EXPECT_FALSE(nmea::parse_gga(nmea::fields("$GPGGA,092725.00,,,00833.91590,E,1,08,1.01,499.6,M,48.0,M,,*00")).has_value());
    // An RMC whose position does not convert.
    EXPECT_FALSE(nmea::parse_rmc(nmea::fields("$GPRMC,083559.00,A,9117.11437,N,00833.91522,E,0.004,77.52,091202,,,A*00")).has_value());
    // Speed and course outside what the receiver can mean; 360.0 itself is allowed.
    EXPECT_FALSE(nmea::parse_rmc(nmea::fields("$GPRMC,083559.00,A,4717.11437,N,00833.91522,E,-0.004,77.52,091202,,,A*00")).has_value());
    EXPECT_FALSE(nmea::parse_rmc(nmea::fields("$GPRMC,083559.00,A,4717.11437,N,00833.91522,E,0.004,-1.0,091202,,,A*00")).has_value());
    EXPECT_FALSE(nmea::parse_rmc(nmea::fields("$GPRMC,083559.00,A,4717.11437,N,00833.91522,E,0.004,361.0,091202,,,A*00")).has_value());
    EXPECT_TRUE(nmea::parse_rmc(nmea::fields("$GPRMC,083559.00,A,4717.11437,N,00833.91522,E,0.004,360.0,091202,,,A*00")).has_value());
}

// A NUL byte is invisible to the XOR checksum (x ^ 0 is x) and a BREAK on
// the header UART reads as one, so a NUL inside a numeric field must not
// silently truncate the number: a field is read in full, to its last byte.
TEST(NmeaTest, EmbeddedNulInANumberIsMalformed) {
    std::string altitude_cut = "$GPGGA,092725.00,4717.11399,N,00833.91590,E,1,08,1.01,499";
    altitude_cut.push_back('\0');
    altitude_cut += ".6,M,48.0,M,,*00";
    EXPECT_FALSE(nmea::parse_gga(nmea::fields(altitude_cut)).has_value());
    std::string satellites_cut = "$GPGGA,092725.00,4717.11399,N,00833.91590,E,1,0";
    satellites_cut.push_back('\0');
    satellites_cut += "8,1.01,499.6,M,48.0,M,,*00";
    EXPECT_FALSE(nmea::parse_gga(nmea::fields(satellites_cut)).has_value());
}

TEST(NmeaTest, TooFewFieldsIsMalformed) {
    EXPECT_FALSE(nmea::parse_gga(nmea::fields("$GPGGA,092725.00,4717.11399,N,00833.91590,E,1*00")).has_value());
    EXPECT_FALSE(nmea::parse_rmc(nmea::fields("$GPRMC,083559.00,A,4717.11437,N*00")).has_value());
    EXPECT_FALSE(nmea::parse_gga({}).has_value());
    EXPECT_FALSE(nmea::parse_rmc({}).has_value());
}

TEST(NmeaTest, FixWithoutPositionIsMalformed) {
    EXPECT_TRUE(nmea::checksum_ok(ex::kGgaFixWithoutPosition));
    EXPECT_FALSE(nmea::parse_gga(nmea::fields(ex::kGgaFixWithoutPosition)).has_value());
    EXPECT_FALSE(nmea::parse_rmc(nmea::fields("$GPRMC,092725.00,A,,,,,0.004,77.52,091202,,,A*00")).has_value());
}

// Half a position that converts is still half a position: a longitude
// that does not convert beside a latitude that does is malformed, in a
// GGA and in an RMC. The malformed-latitude sentences above take the
// latitude's refusal; the record showed the longitude's never taken
// (Entry 13 §13.7).
TEST(NmeaTest, LongitudeThatDoesNotConvertIsMalformed) {
    EXPECT_TRUE(nmea::checksum_ok(ex::kGgaLongitude181));
    EXPECT_FALSE(nmea::parse_gga(nmea::fields(ex::kGgaLongitude181)).has_value());
    EXPECT_TRUE(nmea::checksum_ok(ex::kRmcLongitude181));
    EXPECT_FALSE(nmea::parse_rmc(nmea::fields(ex::kRmcLongitude181)).has_value());
}

// A course that is not a number is malformed, as a speed that is not a
// number is: UnparsableFieldIsMalformed feeds `fast` as the speed, which
// the record showed never reaches the course's rule (Entry 13 §13.7).
TEST(NmeaTest, NonNumericCourseIsMalformed) {
    EXPECT_TRUE(nmea::checksum_ok(ex::kRmcBadCourse));
    EXPECT_FALSE(nmea::parse_rmc(nmea::fields(ex::kRmcBadCourse)).has_value());
}

TEST(NmeaTest, ForeignSentencesPassTheChecksumAndAreNotUsed) {
    for (const char* sentence : {ex::kVtg, ex::kGll, ex::kGsa, ex::kGsv, ex::kTxt, ex::kPubx}) {
        EXPECT_TRUE(nmea::checksum_ok(sentence)) << sentence;
        EXPECT_EQ(nmea::formatter(nmea::fields(sentence)[0]), nmea::Formatter::OTHER) << sentence;
    }
}

TEST(NmeaTest, KnotsToMetersPerSecond) {
    EXPECT_NEAR(nmea::kMetersPerSecondPerKnot, 0.514444, 1e-6);
    EXPECT_NEAR(nmea::knots_to_mps(1.0), 0.514444f, 1e-6);
    EXPECT_NEAR(nmea::knots_to_mps(0.004), ex::kRmcSpeedMps, 1e-6);
}
