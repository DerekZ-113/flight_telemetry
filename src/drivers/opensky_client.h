#pragma once

#include <cstddef>
#include <cstdint>
#include <optional>
#include <string>

#include "drivers/adsb_config.h"
#include "drivers/http_client.h"
#include "drivers/opensky_json.h"
#include "timing/clock.h"

// The OpenSky Network client (REQ-SENS-008, REQ-CFG-004; FTS-FM-001
// FAULT-012 to FAULT-014, FAULT-017): the token's life and one states
// fetch with its failure vocabulary and counters. OAuth2 client
// credentials: the client proves itself once by posting its id and secret
// to the token endpoint and receives a bearer token good for about thirty
// minutes; every states request carries the token and never the secret.
// The token is refreshed kTokenRefreshMarginS before it expires, on the
// Clock seam, and a 401 from the states endpoint is repaired once with a
// refresh and a retry of the same poll. Every failure is a value and a
// count; nothing here prints or writes, and no message carries a token,
// a credential, a header or a body. The two endpoints are facts of the
// service, constants, whose origin `origin_override` replaces for the
// end-to-end test's local server (`--adsb-origin`); CI never calls the
// service.
enum class FetchStatus : uint8_t {
    OK,                 // a states document, parsed
    TOKEN_REFUSED,      // 400, 401 or 403 from the token endpoint: the credentials are wrong (FAULT-013)
    TOKEN_UNAVAILABLE,  // the token endpoint unreachable, in error, or answering nonsense (FAULT-012 at the token endpoint)
    UNAUTHORIZED,       // 401 from the states endpoint twice, the refresh between (FAULT-013)
    RATE_LIMITED,       // 429: the credits are spent (FAULT-014)
    HTTP_ERROR,         // any other status outside 2xx (FAULT-012)
    TRANSPORT_ERROR,    // no status line: no route, refused, timed out, the body past its cap (FAULT-012)
    MALFORMED,          // 2xx with a body that is not a states document (FAULT-017)
};

const char* fetch_status_name(FetchStatus status);

struct FetchResult {
    FetchStatus status = FetchStatus::TRANSPORT_ERROR;
    opensky_json::StatesDocument document;   // valid when OK
    std::optional<int64_t> retry_after_s;    // RATE_LIMITED, when the header was there and parsed
    long http_status = 0;                    // the last status seen, 0 without one
    std::string detail;                      // libcurl's text or the parser's problem; never a body, a token or a credential
};

// Every poll falls in exactly one of ok, token_refused, token_unavailable,
// unauthorized, rate_limited, http_errors, transport_errors and malformed;
// `auth` counts every 401 the states endpoint returned, repaired or not,
// so a poll repaired by a refresh and a retry counts both an `auth` and
// an `ok`.
struct OpenSkyCounters {
    size_t polls = 0;
    size_t ok = 0;
    size_t auth = 0;
    size_t token_refused = 0;
    size_t token_unavailable = 0;
    size_t unauthorized = 0;
    size_t rate_limited = 0;
    size_t http_errors = 0;
    size_t transport_errors = 0;
    size_t malformed = 0;
    size_t token_requests = 0;
    size_t rows_skipped = 0;
    std::optional<int64_t> last_retry_after_s;
    long last_http_status = 0;
    int last_error_code = 0;
};

class OpenSkyClient {
public:
    static constexpr const char* kTokenUrl =
        "https://auth.opensky-network.org/auth/realms/opensky-network/protocol/openid-connect/token";
    static constexpr const char* kStatesUrl = "https://opensky-network.org/api/states/all";
    // Refreshed this long before expiry: longer than a poll interval plus
    // a request, so a session never meets the expiry in the ordinary course.
    static constexpr int64_t kTokenRefreshMarginS = 60;

    // `http` and `clock` must outlive this object. `origin_override`, when
    // not empty, replaces the scheme and host of both endpoints
    // ("http://127.0.0.1:8080"), keeping the service's paths.
    OpenSkyClient(HttpClient& http, Clock& clock, opensky_json::Credentials credentials, const AdsbBox& box,
                  const std::string& origin_override = "");

    // One poll: a token when none is valid, the GET with the box, on 401
    // one refresh and one retry, the parse.
    FetchResult fetch_states();

    bool has_token() const;   // the token's state, for the tests; never its value
    const OpenSkyCounters& counters() const;
    const std::string& token_url() const;
    const std::string& states_url() const;   // with the box's four parameters

    // Percent-encoding for a form field: unreserved characters kept, every
    // other byte as %XX, so a secret with '&' or '=' in it cannot be read
    // as two fields.
    static std::string form_encode(const std::string& value);

    // `url` with its scheme and host replaced by `origin`, or `url` itself
    // when `origin` is empty.
    static std::string with_origin(const std::string& url, const std::string& origin);

private:
    enum class TokenOutcome { OK, REFUSED, UNAVAILABLE };
    TokenOutcome request_token();
    bool token_valid_now() const;

    HttpClient& http_;
    Clock& clock_;
    opensky_json::Credentials credentials_;
    std::string token_url_;
    std::string states_url_;
    std::string token_;
    bool has_token_ = false;
    uint64_t token_expires_at_ns_ = 0;   // the clock reading at which the margin is reached
    OpenSkyCounters counters_;
};
