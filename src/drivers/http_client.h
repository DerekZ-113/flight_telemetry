#pragma once

#include <string>
#include <utility>
#include <vector>

// The HTTP seam (REQ-SENS-008, REQ-CFG-004): one exchange with a server,
// bounded in time, reported as data. The same pattern as SerialPort and
// I2cBus: an abstract class the ADS-B source's client calls, one real
// implementation (CurlHttpClient, over libcurl) and a fake in the tests
// that scripts responses and records requests, so every unit test of the
// token client and the source runs without a network and CI never calls
// the service. Errors are values, never exceptions, and nothing here
// prints: a verbose HTTP log would carry the bearer token and, on the
// token request, the secret.
struct HttpResponse {
    // A status line came back. False means the exchange never completed
    // (no route, a refused connection, a timeout, a body past the cap);
    // `error` says why in libcurl's words and `error_code` is its code.
    bool transport_ok = false;
    long status = 0;
    std::string body;
    // Response headers, names lower-cased and values trimmed, in order.
    std::vector<std::pair<std::string, std::string>> headers;
    std::string error;    // never a URL, a header or a body
    int error_code = 0;

    // The first header named `name` (lower case), or null.
    const std::string* header(const std::string& name) const;
};

class HttpClient {
public:
    virtual ~HttpClient() = default;

    // GET `url`, with "Authorization: Bearer <bearer_token>" when the token
    // is non-empty. Never throws; never blocks past the implementation's
    // timeout.
    virtual HttpResponse get(const std::string& url, const std::string& bearer_token) = 0;

    // POST `form_body`, already form-encoded, as
    // application/x-www-form-urlencoded. The body is the one place a
    // credential travels (REQ-CFG-004).
    virtual HttpResponse post_form(const std::string& url, const std::string& form_body) = 0;
};
