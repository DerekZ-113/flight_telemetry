#pragma once

// The example sentences printed in the u-blox 6 Receiver Description
// (GPS.G6-SW-10018), with the checksums as printed there. Every one was
// recomputed by an independent XOR before any test was written (TC-018
// preconditions), so they are the parser's oracle the way the BMP280
// datasheet example is the barometer's (bmp280_example.h). The variants
// carry checksums recomputed by the same XOR. Nothing here was produced
// by the code under test.
namespace nmea_examples {

// Printed in the manual.
constexpr const char* kGga = "$GPGGA,092725.00,4717.11399,N,00833.91590,E,1,08,1.01,499.6,M,48.0,M,,*5B";
constexpr const char* kRmc = "$GPRMC,083559.00,A,4717.11437,N,00833.91522,E,0.004,77.52,091202,,,A*57";
constexpr const char* kVtg = "$GPVTG,77.52,T,,M,0.004,N,0.008,K,A*06";
constexpr const char* kGll = "$GPGLL,4717.11364,N,00833.91565,E,092321.00,A,A*60";
constexpr const char* kGsa = "$GPGSA,A,3,23,29,07,08,09,18,26,28,,,,,1.94,1.18,1.54*0D";
constexpr const char* kGsv = "$GPGSV,3,1,10,23,38,230,44,29,71,156,47,07,29,116,41,08,09,081,36*7F";
constexpr const char* kTxt = "$GPTXT,01,01,02,u-blox ag - www.u-blox.com*50";

// What a u-blox receiver sends before it has a fix: the time, position
// and altitude fields empty, quality 0, status V.
constexpr const char* kNoFixGga = "$GPGGA,,,,,,0,00,99.99,,,,,,*48";
constexpr const char* kNoFixRmc = "$GPRMC,,V,,,,,,,,,,N*53";

// The example's values, from the manual's text, not from the parser:
// degrees plus minutes over sixty; knots times 1852 m over 3600 s.
constexpr double kGgaLatitudeDeg = 47.0 + 17.11399 / 60.0;    // 47.2852332 N
constexpr double kGgaLongitudeDeg = 8.0 + 33.91590 / 60.0;    // 8.5652650 E
constexpr float kGgaAltitudeM = 499.6f;
constexpr int kGgaQuality = 1;
constexpr int kGgaSatellites = 8;
constexpr double kRmcSpeedMps = 0.004 * 1852.0 / 3600.0;      // 0.0020578
constexpr float kRmcCourseDeg = 77.52f;

// Variants, checksums recomputed.
constexpr const char* kGnGga = "$GNGGA,092725.00,4717.11399,N,00833.91590,E,1,08,1.01,499.6,M,48.0,M,,*45";
constexpr const char* kGgaQuality2 = "$GPGGA,092725.00,4717.11399,N,00833.91590,E,2,08,1.01,499.6,M,48.0,M,,*58";
// The RMC of the example GGA's epoch: the same UTC time field.
constexpr const char* kRmcSameEpoch = "$GPRMC,092725.00,A,4717.11437,N,00833.91522,E,0.004,77.52,091202,,,A*5E";
constexpr const char* kRmcEmptySpeedCourse = "$GPRMC,092725.00,A,4717.11437,N,00833.91522,E,,,091202,,,A*5D";
// A second epoch, one second later and one metre higher.
constexpr const char* kGgaSecondEpoch = "$GPGGA,092726.00,4717.11399,N,00833.91590,E,1,08,1.01,500.6,M,48.0,M,,*59";
constexpr const char* kRmcSecondEpoch = "$GPRMC,092726.00,A,4717.11437,N,00833.91522,E,0.004,77.52,091202,,,A*5D";
// Two more GGA epochs with no RMC.
constexpr const char* kGgaThirdEpoch = "$GPGGA,092728.00,4717.11399,N,00833.91590,E,1,08,1.01,499.6,M,48.0,M,,*56";
constexpr const char* kGgaFourthEpoch = "$GPGGA,092729.00,4717.11399,N,00833.91590,E,1,08,1.01,499.6,M,48.0,M,,*57";
// A no-fix epoch from a receiver that already has the time.
constexpr const char* kNoFixGgaWithTime = "$GPGGA,092727.00,,,,,0,00,99.99,,,,,,*6F";
constexpr const char* kNoFixRmcWithTime = "$GPRMC,092727.00,V,,,,,,,091202,,,N*7C";
// Malformed: a latitude of 91 degrees, sixty minutes, a satellite count
// that is not a number, a fix with no position, a status that is neither
// A nor V. Each passes the checksum, so the parser is what refuses it.
constexpr const char* kGgaLatitude91 = "$GPGGA,092725.00,9117.11399,N,00833.91590,E,1,08,1.01,499.6,M,48.0,M,,*50";
constexpr const char* kGgaMinutes60 = "$GPGGA,092725.00,4760.00000,N,00833.91590,E,1,08,1.01,499.6,M,48.0,M,,*58";
constexpr const char* kGgaBadSatellites = "$GPGGA,092725.00,4717.11399,N,00833.91590,E,1,8x,1.01,499.6,M,48.0,M,,*13";
constexpr const char* kGgaFixWithoutPosition = "$GPGGA,092725.00,,,,,1,08,1.01,499.6,M,48.0,M,,*6A";
constexpr const char* kRmcStatusX = "$GPRMC,083559.00,X,4717.11437,N,00833.91522,E,0.004,77.52,091202,,,A*4E";
// A u-blox proprietary sentence: a four-letter address.
constexpr const char* kPubx = "$PUBX,00,081350.00,4717.113210,N,00833.915187,E,546.589,G3,2.1,2.0,0.007,77.52,0.007,,0.92,1.19,0.77,9,0,0*5F";

}  // namespace nmea_examples
