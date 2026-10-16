#include "drivers/opensky_client.h"

#include <cstdio>
#include <cstdlib>

namespace {

// The box's edges as the service reads them: four decimals is a tenth of
// a metre, more than the file carries.
std::string decimal(double value) {
    char buffer[32];
    std::snprintf(buffer, sizeof(buffer), "%.4f", value);
    return buffer;
}

bool unreserved(unsigned char c) {
    return (c >= 'A' && c <= 'Z') || (c >= 'a' && c <= 'z') || (c >= '0' && c <= '9') || c == '-' || c == '.' ||
           c == '_' || c == '~';
}

// A poll that yielded nothing: the status, the HTTP status when there was
// one, and libcurl's text when there was none.
FetchResult failure(FetchStatus status, const HttpResponse& response) {
    FetchResult result;
    result.status = status;
    result.http_status = response.transport_ok ? response.status : 0;
    result.detail = response.transport_ok ? "" : response.error;
    return result;
}

}  // namespace

const char* fetch_status_name(FetchStatus status) {
    switch (status) {  // LCOV_EXCL_BR_LINE: exhaustive over enum class, no-match branch unreachable
        case FetchStatus::OK:                return "ok";
        case FetchStatus::TOKEN_REFUSED:     return "token refused";
        case FetchStatus::TOKEN_UNAVAILABLE: return "token unavailable";
        case FetchStatus::UNAUTHORIZED:      return "unauthorized";
        case FetchStatus::RATE_LIMITED:      return "rate limited";
        case FetchStatus::HTTP_ERROR:        return "http error";
        case FetchStatus::TRANSPORT_ERROR:   return "transport error";
        case FetchStatus::MALFORMED:         return "malformed";
    }
    return "UNKNOWN";  // LCOV_EXCL_LINE (unreachable: the switch covers every enumerator)
}

OpenSkyClient::OpenSkyClient(HttpClient& http, Clock& clock, opensky_json::Credentials credentials, const AdsbBox& box,
                             const std::string& origin_override)
    : http_(http), clock_(clock), credentials_(std::move(credentials)) {
    token_url_ = with_origin(kTokenUrl, origin_override);
    states_url_ = with_origin(kStatesUrl, origin_override) + "?lamin=" + decimal(box.lat_min) +
                  "&lomin=" + decimal(box.lon_min) + "&lamax=" + decimal(box.lat_max) + "&lomax=" + decimal(box.lon_max);
}

std::string OpenSkyClient::form_encode(const std::string& value) {
    std::string out;
    for (const char ch : value) {
        const unsigned char c = static_cast<unsigned char>(ch);
        if (unreserved(c)) {
            out += ch;
        } else {
            char escaped[4];
            std::snprintf(escaped, sizeof(escaped), "%%%02X", c);
            out += escaped;
        }
    }
    return out;
}

std::string OpenSkyClient::with_origin(const std::string& url, const std::string& origin) {
    if (origin.empty()) {
        return url;
    }
    const size_t scheme_end = url.find("://");
    const size_t path = scheme_end == std::string::npos ? std::string::npos : url.find('/', scheme_end + 3);
    std::string base = origin;
    if (base.back() == '/') {
        base.pop_back();
    }
    if (path == std::string::npos) {
        return base;
    }
    return base + url.substr(path);
}

bool OpenSkyClient::has_token() const { return has_token_; }
const OpenSkyCounters& OpenSkyClient::counters() const { return counters_; }
const std::string& OpenSkyClient::token_url() const { return token_url_; }
const std::string& OpenSkyClient::states_url() const { return states_url_; }

bool OpenSkyClient::token_valid_now() const {
    return has_token_ && clock_.now_ns() < token_expires_at_ns_;
}

// The one place the secret travels (REQ-CFG-004): the token request's
// body, form-encoded. The response names the token and its lifetime; the
// margin is taken off here so the expiry test is one comparison.
OpenSkyClient::TokenOutcome OpenSkyClient::request_token() {
    counters_.token_requests++;
    has_token_ = false;
    token_.clear();
    const std::string body = "grant_type=client_credentials&client_id=" + form_encode(credentials_.client_id) +
                             "&client_secret=" + form_encode(credentials_.client_secret);
    const HttpResponse response = http_.post_form(token_url_, body);
    if (!response.transport_ok) {
        counters_.token_unavailable++;
        counters_.last_error_code = response.error_code;
        return TokenOutcome::UNAVAILABLE;
    }
    counters_.last_http_status = response.status;
    const bool refused = response.status == 400 || response.status == 401 || response.status == 403;
    if (refused) {
        counters_.token_refused++;
        return TokenOutcome::REFUSED;
    }
    const bool success = response.status >= 200 && response.status < 300;
    if (!success) {
        counters_.token_unavailable++;
        return TokenOutcome::UNAVAILABLE;
    }
    const opensky_json::TokenParse parsed = opensky_json::parse_token(response.body);
    if (!parsed.ok) {
        counters_.token_unavailable++;
        return TokenOutcome::UNAVAILABLE;
    }
    token_ = parsed.access_token;
    has_token_ = true;
    const int64_t usable_s = parsed.expires_in_s > kTokenRefreshMarginS ? parsed.expires_in_s - kTokenRefreshMarginS : 0;
    token_expires_at_ns_ = clock_.now_ns() + static_cast<uint64_t>(usable_s) * 1'000'000'000ull;
    return TokenOutcome::OK;
}

FetchResult OpenSkyClient::fetch_states() {
    counters_.polls++;
    const bool valid = token_valid_now();
    if (!valid) {
        const TokenOutcome outcome = request_token();
        if (outcome == TokenOutcome::REFUSED) {
            FetchResult result;
            result.status = FetchStatus::TOKEN_REFUSED;
            result.http_status = counters_.last_http_status;
            return result;
        }
        if (outcome == TokenOutcome::UNAVAILABLE) {
            FetchResult result;
            result.status = FetchStatus::TOKEN_UNAVAILABLE;
            result.http_status = counters_.last_http_status;
            return result;
        }
    }

    HttpResponse response = http_.get(states_url_, token_);
    if (response.transport_ok && response.status == 401) {
        // The token the server rejected is gone, whatever this client's
        // clock said: one refresh, one retry of the same poll (FAULT-013).
        counters_.auth++;
        const TokenOutcome outcome = request_token();
        if (outcome == TokenOutcome::REFUSED) {
            FetchResult result;
            result.status = FetchStatus::TOKEN_REFUSED;
            result.http_status = counters_.last_http_status;
            return result;
        }
        if (outcome == TokenOutcome::UNAVAILABLE) {
            FetchResult result;
            result.status = FetchStatus::TOKEN_UNAVAILABLE;
            result.http_status = counters_.last_http_status;
            return result;
        }
        response = http_.get(states_url_, token_);
        if (response.transport_ok && response.status == 401) {
            counters_.auth++;
            counters_.unauthorized++;
            counters_.last_http_status = 401;
            return failure(FetchStatus::UNAUTHORIZED, response);
        }
    }
    if (!response.transport_ok) {
        counters_.transport_errors++;
        counters_.last_error_code = response.error_code;
        return failure(FetchStatus::TRANSPORT_ERROR, response);
    }
    counters_.last_http_status = response.status;
    if (response.status == 429) {
        counters_.rate_limited++;
        FetchResult result = failure(FetchStatus::RATE_LIMITED, response);
        const std::string* retry_after = response.header("x-rate-limit-retry-after-seconds");
        if (retry_after != nullptr) {
            char* end = nullptr;
            const long long seconds = std::strtoll(retry_after->c_str(), &end, 10);
            const bool parsed = end != retry_after->c_str() && *end == '\0' && seconds >= 0;
            if (parsed) {
                result.retry_after_s = seconds;
                counters_.last_retry_after_s = seconds;
            }
        }
        return result;
    }
    const bool success = response.status >= 200 && response.status < 300;
    if (!success) {
        counters_.http_errors++;
        return failure(FetchStatus::HTTP_ERROR, response);
    }
    opensky_json::StatesParse parsed = opensky_json::parse_states(response.body);
    if (!parsed.ok) {
        counters_.malformed++;
        FetchResult result = failure(FetchStatus::MALFORMED, response);
        result.detail = parsed.problem;
        return result;
    }
    counters_.ok++;
    counters_.rows_skipped += parsed.document.rows_skipped;
    FetchResult result;
    result.status = FetchStatus::OK;
    result.http_status = response.status;
    result.document = std::move(parsed.document);
    return result;
}
