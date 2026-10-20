#pragma once

#include <deque>
#include <mutex>
#include <string>
#include <utility>
#include <vector>

#include "drivers/http_client.h"

// The HTTP seam's fake (TC-027, TC-029): scripted responses served in
// order, one per request, and a record of every request, so a test can
// assert what the client sent (the token request's body, the bearer, the
// box in the URL) and what it did with each answer. A request past the
// script is a transport error naming the gap, so a test that scripts too
// little fails loudly rather than hanging. Locked, because the source's
// poll thread calls it while a test reads the record after the join.
struct FakeHttpRequest {
    std::string method;        // "GET" or "POST"
    std::string url;
    std::string bearer_token;  // GET only
    std::string form_body;     // POST only
};

class FakeHttpClient : public HttpClient {
public:
    void queue(HttpResponse response);

    // A completed exchange: the status and body, with headers whose names
    // are lower-cased as the real client lower-cases them.
    static HttpResponse response(long status, std::string body,
                                 std::vector<std::pair<std::string, std::string>> headers = {});
    // An exchange that never completed, with libcurl's code and text.
    static HttpResponse transport_error(int code, std::string text);

    HttpResponse get(const std::string& url, const std::string& bearer_token) override;
    HttpResponse post_form(const std::string& url, const std::string& form_body) override;

    std::vector<FakeHttpRequest> requests() const;
    size_t pending() const;

private:
    HttpResponse next();

    mutable std::mutex mutex_;
    std::deque<HttpResponse> responses_;
    std::vector<FakeHttpRequest> requests_;
};
