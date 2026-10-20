#pragma once

#include <cstddef>
#include <string>

#include "drivers/http_client.h"

// The HTTP seam over libcurl's easy interface (REQ-SENS-008). One easy
// handle, reused across exchanges so a connection can be kept, used by one
// thread at a time (the poll thread, after the first poll on the main
// thread). Each exchange is bounded: a connect timeout, a total timeout,
// and a cap on the body, so a misconfigured box or a hostile server cannot
// hold the thread or eat memory. CURLOPT_NOSIGNAL is set because libcurl
// otherwise uses SIGALRM for resolver timeouts, which is unsafe once a
// second thread exists; redirects are not followed; only http and https
// are allowed; and CURLOPT_VERBOSE is never set, since it would print the
// Authorization header and the token request's body (REQ-CFG-004).
// curl_global_init runs once in the constructor and its cleanup in the
// destructor: main owns one client for the process.
class CurlHttpClient : public HttpClient {
public:
    static constexpr long kConnectTimeoutMs = 5000;    // a connection that takes longer is not coming
    static constexpr long kTotalTimeoutMs = 10000;     // twice the shipped poll interval: a stuck request never stalls two polls
    static constexpr size_t kMaxBodyBytes = 4u << 20;  // a city-sized box answers in tens of kilobytes
    static constexpr const char* kUserAgent = "flight-telemetry/0.1.0 (+https://github.com/DerekZ-113/flight_telemetry)";

    explicit CurlHttpClient(long total_timeout_ms = kTotalTimeoutMs,
                            long connect_timeout_ms = kConnectTimeoutMs,
                            size_t max_body_bytes = kMaxBodyBytes);
    ~CurlHttpClient() override;
    CurlHttpClient(const CurlHttpClient&) = delete;
    CurlHttpClient& operator=(const CurlHttpClient&) = delete;

    bool ok() const;                   // libcurl initialized and a handle obtained
    const std::string& error() const;

    HttpResponse get(const std::string& url, const std::string& bearer_token) override;
    HttpResponse post_form(const std::string& url, const std::string& form_body) override;

private:
    // The one exchange both calls share: a GET with an optional bearer, or
    // a POST of a form body when `form_body` is not null.
    HttpResponse perform(const std::string& url, const std::string& bearer_token, const std::string* form_body);

    void* handle_ = nullptr;   // CURL*, kept as void* so this header stays free of curl/curl.h
    long total_timeout_ms_;
    long connect_timeout_ms_;
    size_t max_body_bytes_;
    bool ok_ = false;
    std::string error_;
};
