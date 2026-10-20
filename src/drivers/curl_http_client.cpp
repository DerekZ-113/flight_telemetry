#include "drivers/curl_http_client.h"

#include <curl/curl.h>

#include <algorithm>
#include <cctype>

namespace {

// What the write callbacks fill while libcurl reads a response.
struct Receiving {
    std::string body;
    size_t max_body_bytes = 0;
    std::vector<std::pair<std::string, std::string>> headers;
};

// libcurl hands the body in chunks; returning fewer bytes than offered
// aborts the transfer with CURLE_WRITE_ERROR, which is how the cap refuses
// a body (the exchange then reports a transport error, never a partial
// body as if it were whole).
size_t write_body(char* data, size_t size, size_t count, void* user) {
    Receiving& receiving = *static_cast<Receiving*>(user);
    const size_t bytes = size * count;
    if (receiving.body.size() + bytes > receiving.max_body_bytes) {
        return 0;
    }
    receiving.body.append(data, bytes);
    return bytes;
}

std::string trimmed(const std::string& text) {
    size_t begin = 0;
    size_t end = text.size();
    while (begin < end && std::isspace(static_cast<unsigned char>(text[begin]))) {
        begin++;
    }
    while (end > begin && std::isspace(static_cast<unsigned char>(text[end - 1]))) {
        end--;
    }
    return text.substr(begin, end - begin);
}

// One header line per call: "Name: value\r\n". The status line and the
// blank line that ends the headers carry no colon and are skipped; names
// are lower-cased so a caller can ask for one spelling.
size_t write_header(char* data, size_t size, size_t count, void* user) {
    const size_t bytes = size * count;
    const std::string line(data, bytes);
    const size_t colon = line.find(':');
    if (colon != std::string::npos) {
        Receiving& receiving = *static_cast<Receiving*>(user);
        std::string name = line.substr(0, colon);
        std::transform(name.begin(), name.end(), name.begin(),
                       [](unsigned char c) { return static_cast<char>(std::tolower(c)); });
        receiving.headers.emplace_back(name, trimmed(line.substr(colon + 1)));
    }
    return bytes;
}

}  // namespace

CurlHttpClient::CurlHttpClient(long total_timeout_ms, long connect_timeout_ms, size_t max_body_bytes)
    : total_timeout_ms_(total_timeout_ms), connect_timeout_ms_(connect_timeout_ms), max_body_bytes_(max_body_bytes) {
    const CURLcode global = curl_global_init(CURL_GLOBAL_DEFAULT);
    if (global != CURLE_OK) {
        error_ = std::string("curl_global_init: ") + curl_easy_strerror(global);
        return;
    }
    handle_ = curl_easy_init();
    if (handle_ == nullptr) {
        error_ = "curl_easy_init failed";
        return;
    }
    ok_ = true;
}

CurlHttpClient::~CurlHttpClient() {
    if (handle_ != nullptr) {
        curl_easy_cleanup(static_cast<CURL*>(handle_));
    }
    curl_global_cleanup();
}

bool CurlHttpClient::ok() const { return ok_; }
const std::string& CurlHttpClient::error() const { return error_; }

HttpResponse CurlHttpClient::get(const std::string& url, const std::string& bearer_token) {
    return perform(url, bearer_token, nullptr);
}

HttpResponse CurlHttpClient::post_form(const std::string& url, const std::string& form_body) {
    return perform(url, "", &form_body);
}

HttpResponse CurlHttpClient::perform(const std::string& url, const std::string& bearer_token, const std::string* form_body) {
    HttpResponse response;
    if (!ok_) {
        response.error = error_;
        return response;
    }
    CURL* curl = static_cast<CURL*>(handle_);
    Receiving receiving;
    receiving.max_body_bytes = max_body_bytes_;

    // Every option set afresh on each exchange: the reset wipes the last
    // exchange's POST and headers, so a GET never carries the token
    // request's body.
    curl_easy_reset(curl);
    curl_easy_setopt(curl, CURLOPT_URL, url.c_str());
    curl_easy_setopt(curl, CURLOPT_NOSIGNAL, 1L);
    curl_easy_setopt(curl, CURLOPT_CONNECTTIMEOUT_MS, connect_timeout_ms_);
    curl_easy_setopt(curl, CURLOPT_TIMEOUT_MS, total_timeout_ms_);
    curl_easy_setopt(curl, CURLOPT_PROTOCOLS_STR, "http,https");
    curl_easy_setopt(curl, CURLOPT_FOLLOWLOCATION, 0L);
    curl_easy_setopt(curl, CURLOPT_USERAGENT, kUserAgent);
    curl_easy_setopt(curl, CURLOPT_WRITEFUNCTION, write_body);
    curl_easy_setopt(curl, CURLOPT_WRITEDATA, &receiving);
    curl_easy_setopt(curl, CURLOPT_HEADERFUNCTION, write_header);
    curl_easy_setopt(curl, CURLOPT_HEADERDATA, &receiving);

    curl_slist* request_headers = nullptr;
    if (form_body != nullptr) {
        curl_easy_setopt(curl, CURLOPT_POST, 1L);
        curl_easy_setopt(curl, CURLOPT_POSTFIELDSIZE, static_cast<long>(form_body->size()));
        curl_easy_setopt(curl, CURLOPT_COPYPOSTFIELDS, form_body->c_str());
        request_headers = curl_slist_append(request_headers, "Content-Type: application/x-www-form-urlencoded");
    }
    if (!bearer_token.empty()) {
        const std::string authorization = "Authorization: Bearer " + bearer_token;
        request_headers = curl_slist_append(request_headers, authorization.c_str());
    }
    if (request_headers != nullptr) {
        curl_easy_setopt(curl, CURLOPT_HTTPHEADER, request_headers);
    }

    const CURLcode code = curl_easy_perform(curl);
    curl_slist_free_all(request_headers);

    if (code != CURLE_OK) {
        response.error = curl_easy_strerror(code);
        response.error_code = static_cast<int>(code);
        return response;
    }
    long status = 0;
    curl_easy_getinfo(curl, CURLINFO_RESPONSE_CODE, &status);
    response.transport_ok = true;
    response.status = status;
    response.body = std::move(receiving.body);
    response.headers = std::move(receiving.headers);
    return response;
}
