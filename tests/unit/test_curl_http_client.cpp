// Tests for src/drivers/curl_http_client (REQ-SENS-008, REQ-CFG-004). Test
// card TC-027 step 11: the libcurl client against a one-connection HTTP
// responder on a loopback socket inside the test, so the success path, the
// headers, the form body and every failure are exercised without a
// network; CI never calls the service. The responder records the request
// it received, which is how the bearer and the content type are checked.

#include <gtest/gtest.h>

#include <arpa/inet.h>
#include <netinet/in.h>
#include <poll.h>
#include <sys/socket.h>
#include <unistd.h>

#include <chrono>
#include <cstring>
#include <string>
#include <thread>

#include "drivers/curl_http_client.h"

namespace {

// Serves one connection: reads the request (the headers and, for a POST,
// the body named by Content-Length), stores it, writes `reply` and closes.
// With an empty reply it stays silent for `silent_ms` and then closes,
// which is how a timeout is provoked. The accept waits at most three
// seconds, so a test that never connects still ends.
class Responder {
public:
    explicit Responder(std::string reply, int silent_ms = 0) : reply_(std::move(reply)), silent_ms_(silent_ms) {
        listening_ = ::socket(AF_INET, SOCK_STREAM, 0);
        int one = 1;
        ::setsockopt(listening_, SOL_SOCKET, SO_REUSEADDR, &one, sizeof(one));
        sockaddr_in address{};
        address.sin_family = AF_INET;
        address.sin_addr.s_addr = htonl(INADDR_LOOPBACK);
        address.sin_port = 0;
        ::bind(listening_, reinterpret_cast<sockaddr*>(&address), sizeof(address));
        ::listen(listening_, 1);
        socklen_t length = sizeof(address);
        ::getsockname(listening_, reinterpret_cast<sockaddr*>(&address), &length);
        port_ = ntohs(address.sin_port);
        thread_ = std::thread([this] { serve(); });
    }
    ~Responder() {
        if (thread_.joinable()) {
            thread_.join();
        }
        ::close(listening_);
    }
    int port() const { return port_; }
    std::string url(const std::string& path = "/") const { return "http://127.0.0.1:" + std::to_string(port_) + path; }
    std::string request() {
        if (thread_.joinable()) {
            thread_.join();
        }
        return request_;
    }

private:
    void serve() {
        pollfd waiting{};
        waiting.fd = listening_;
        waiting.events = POLLIN;
        if (::poll(&waiting, 1, 3000) <= 0) {
            return;
        }
        const int connection = ::accept(listening_, nullptr, nullptr);
        if (connection < 0) {
            return;
        }
        timeval limit{};
        limit.tv_sec = 2;
        ::setsockopt(connection, SOL_SOCKET, SO_RCVTIMEO, &limit, sizeof(limit));
        std::string received;
        char chunk[1024];
        size_t body_needed = 0;
        bool headers_done = false;
        while (true) {
            const ssize_t n = ::recv(connection, chunk, sizeof(chunk), 0);
            if (n <= 0) {
                break;
            }
            received.append(chunk, static_cast<size_t>(n));
            if (!headers_done) {
                const size_t end = received.find("\r\n\r\n");
                if (end == std::string::npos) {
                    continue;
                }
                headers_done = true;
                const size_t at = received.find("Content-Length: ");
                if (at != std::string::npos && at < end) {
                    body_needed = static_cast<size_t>(std::stoul(received.substr(at + 16)));
                }
                if (received.size() - (end + 4) >= body_needed) {
                    break;
                }
            } else {
                const size_t end = received.find("\r\n\r\n");
                if (received.size() - (end + 4) >= body_needed) {
                    break;
                }
            }
        }
        request_ = received;
        if (reply_.empty()) {
            std::this_thread::sleep_for(std::chrono::milliseconds(silent_ms_));
        } else {
            ::send(connection, reply_.data(), reply_.size(), 0);
        }
        ::close(connection);
    }

    std::string reply_;
    int silent_ms_;
    int listening_ = -1;
    int port_ = 0;
    std::string request_;
    std::thread thread_;
};

std::string reply(int status, const std::string& reason, const std::string& body, const std::string& extra_headers = "") {
    return "HTTP/1.1 " + std::to_string(status) + " " + reason + "\r\n" + extra_headers +
           "Content-Length: " + std::to_string(body.size()) + "\r\nConnection: close\r\n\r\n" + body;
}

}  // namespace

TEST(CurlHttpClientTest, ClientReportsOk) {
    CurlHttpClient client;
    EXPECT_TRUE(client.ok()) << client.error();
    EXPECT_TRUE(client.error().empty());
}

// TC-027 step 11. A GET: the status, the body, a header lower-cased, and
// on the responder's side the bearer and the user agent.
TEST(CurlHttpClientTest, GetReturnsStatusHeadersAndBody) {
    Responder responder(reply(200, "OK", "ok", "Content-Type: text/plain\r\nX-Rate-Limit-Retry-After-Seconds: 7\r\n"));
    CurlHttpClient client;
    const HttpResponse response = client.get(responder.url("/api/states/all?lamin=1"), "abc");
    ASSERT_TRUE(response.transport_ok) << response.error;
    EXPECT_EQ(response.status, 200);
    EXPECT_EQ(response.body, "ok");
    ASSERT_NE(response.header("x-rate-limit-retry-after-seconds"), nullptr);
    EXPECT_EQ(*response.header("x-rate-limit-retry-after-seconds"), "7");
    EXPECT_EQ(*response.header("content-type"), "text/plain");
    EXPECT_EQ(response.header("x-missing"), nullptr);
    const std::string request = responder.request();
    EXPECT_NE(request.find("GET /api/states/all?lamin=1 HTTP/1.1"), std::string::npos) << request;
    EXPECT_NE(request.find("Authorization: Bearer abc\r\n"), std::string::npos) << request;
    EXPECT_NE(request.find("User-Agent: flight-telemetry/0.1.0"), std::string::npos) << request;
}

// TC-027 step 11. A form POST carries the body as given under the form
// content type, and no bearer.
TEST(CurlHttpClientTest, PostFormSendsTheBodyWithTheFormContentType) {
    Responder responder(reply(200, "OK", "{\"access_token\":\"t\"}"));
    CurlHttpClient client;
    const HttpResponse response = client.post_form(responder.url("/token"), "a=1&b=2");
    ASSERT_TRUE(response.transport_ok) << response.error;
    EXPECT_EQ(response.status, 200);
    const std::string request = responder.request();
    EXPECT_NE(request.find("POST /token HTTP/1.1"), std::string::npos) << request;
    EXPECT_NE(request.find("Content-Type: application/x-www-form-urlencoded\r\n"), std::string::npos) << request;
    EXPECT_NE(request.find("Content-Length: 7\r\n"), std::string::npos) << request;
    EXPECT_EQ(request.substr(request.size() - 7), "a=1&b=2");
    EXPECT_EQ(request.find("Authorization"), std::string::npos) << request;
}

// TC-027 step 11. A 404 is a status, not an error: the exchange completed.
TEST(CurlHttpClientTest, NotFoundIsAStatusNotAnError) {
    Responder responder(reply(404, "Not Found", "gone"));
    CurlHttpClient client;
    const HttpResponse response = client.get(responder.url("/nothing"), "");
    ASSERT_TRUE(response.transport_ok) << response.error;
    EXPECT_EQ(response.status, 404);
    EXPECT_EQ(response.body, "gone");
    EXPECT_EQ(responder.request().find("Authorization"), std::string::npos);
}

// TC-027 step 11 (FAULT-012). A closed port is a transport error with
// libcurl's code for a failed connection.
TEST(CurlHttpClientTest, ConnectionRefusedIsATransportError) {
    int port = 0;
    {
        Responder probe("");   // bound and listening for a moment, so the port is free once it is gone
        port = probe.port();
    }
    CurlHttpClient client;
    const HttpResponse response = client.get("http://127.0.0.1:" + std::to_string(port) + "/", "");
    EXPECT_FALSE(response.transport_ok);
    EXPECT_EQ(response.error_code, 7);   // CURLE_COULDNT_CONNECT
    EXPECT_FALSE(response.error.empty());
    EXPECT_EQ(response.status, 0);
}

// TC-027 step 11 (FAULT-012). A server that accepts and never answers is a
// timeout within the client's bound: code 28, well inside a second here.
TEST(CurlHttpClientTest, TimeoutIsATransportError) {
    Responder responder("", 1500);
    CurlHttpClient client(200, 200);
    const auto started = std::chrono::steady_clock::now();
    const HttpResponse response = client.get(responder.url(), "");
    const auto elapsed = std::chrono::steady_clock::now() - started;
    EXPECT_FALSE(response.transport_ok);
    EXPECT_EQ(response.error_code, 28);   // CURLE_OPERATION_TIMEDOUT
    EXPECT_LT(elapsed, std::chrono::seconds(1));
}

// TC-027 step 11. A body past the cap is refused as a transport error, not
// delivered in part.
TEST(CurlHttpClientTest, BodyPastTheCapIsRefused) {
    Responder responder(reply(200, "OK", std::string(100, 'x')));
    CurlHttpClient client(CurlHttpClient::kTotalTimeoutMs, CurlHttpClient::kConnectTimeoutMs, 64);
    const HttpResponse response = client.get(responder.url(), "");
    EXPECT_FALSE(response.transport_ok);
    EXPECT_EQ(response.error_code, 23);   // CURLE_WRITE_ERROR: the callback refused the bytes
    EXPECT_TRUE(response.body.empty());
}

// TC-027 step 11. A URL that is not one, and a scheme the client does not
// allow, are refused without a connection.
TEST(CurlHttpClientTest, MalformedAndForbiddenUrlsAreTransportErrors) {
    CurlHttpClient client;
    const HttpResponse malformed = client.get("not a url", "");
    EXPECT_FALSE(malformed.transport_ok);
    EXPECT_NE(malformed.error_code, 0);
    const HttpResponse ftp = client.get("ftp://127.0.0.1:1/x", "");
    EXPECT_FALSE(ftp.transport_ok);
    EXPECT_EQ(ftp.error_code, 1);   // CURLE_UNSUPPORTED_PROTOCOL
}
