#include "fake_http_client.h"

#include <cctype>

void FakeHttpClient::queue(HttpResponse response) {
    std::lock_guard<std::mutex> lock(mutex_);
    responses_.push_back(std::move(response));
}

HttpResponse FakeHttpClient::response(long status, std::string body,
                                      std::vector<std::pair<std::string, std::string>> headers) {
    HttpResponse r;
    r.transport_ok = true;
    r.status = status;
    r.body = std::move(body);
    for (auto& entry : headers) {
        for (char& c : entry.first) {
            c = static_cast<char>(std::tolower(static_cast<unsigned char>(c)));
        }
    }
    r.headers = std::move(headers);
    return r;
}

HttpResponse FakeHttpClient::transport_error(int code, std::string text) {
    HttpResponse r;
    r.transport_ok = false;
    r.error_code = code;
    r.error = std::move(text);
    return r;
}

HttpResponse FakeHttpClient::next() {
    if (responses_.empty()) {
        return transport_error(7, "FakeHttpClient: no scripted response for this request");
    }
    HttpResponse r = std::move(responses_.front());
    responses_.pop_front();
    return r;
}

HttpResponse FakeHttpClient::get(const std::string& url, const std::string& bearer_token) {
    std::lock_guard<std::mutex> lock(mutex_);
    requests_.push_back({"GET", url, bearer_token, ""});
    return next();
}

HttpResponse FakeHttpClient::post_form(const std::string& url, const std::string& form_body) {
    std::lock_guard<std::mutex> lock(mutex_);
    requests_.push_back({"POST", url, "", form_body});
    return next();
}

std::vector<FakeHttpRequest> FakeHttpClient::requests() const {
    std::lock_guard<std::mutex> lock(mutex_);
    return requests_;
}

size_t FakeHttpClient::pending() const {
    std::lock_guard<std::mutex> lock(mutex_);
    return responses_.size();
}
