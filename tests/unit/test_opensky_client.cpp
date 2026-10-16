// Tests for src/drivers/opensky_client (REQ-SENS-008, REQ-CFG-004;
// FTS-FM-001 FAULT-012 to FAULT-014, FAULT-017). Test card TC-027 steps 5
// through 10: the secret in the token request's body and nowhere else, the
// token reused until the margin before its expiry, one 401 repaired with
// one refresh and one retry, and every other failure a value and a count.
// Every exchange is scripted on FakeHttpClient; the clock is FakeClock; CI
// never calls the service.

#include <gtest/gtest.h>

#include <string>

#include "drivers/adsb_config.h"
#include "drivers/opensky_client.h"
#include "fake_clock.h"
#include "fake_http_client.h"
#include "opensky_examples.h"

namespace ex = opensky_examples;

namespace {

constexpr uint64_t kSecondNs = 1'000'000'000ull;

opensky_json::Credentials credentials() {
    return {"test-client", "test-secret"};
}

struct Rig {
    FakeHttpClient http;
    FakeClock clock;
    OpenSkyClient client{http, clock, credentials(), AdsbBox{}};

    void script_token_and_states() {
        http.queue(FakeHttpClient::response(200, ex::kTokenResponse));
        http.queue(FakeHttpClient::response(200, ex::kStatesResponse));
    }
};

const char* const kExpectedTokenBody = "grant_type=client_credentials&client_id=test-client&client_secret=test-secret";

}  // namespace

// TC-027 step 5. The first fetch proves the client once, by a form POST to
// the token endpoint with the three fields and nothing else, then asks for
// the box with the bearer and nothing else.
TEST(OpenSkyClientTest, FirstFetchRequestsATokenThenTheStates) {
    Rig rig;
    rig.script_token_and_states();
    const FetchResult result = rig.client.fetch_states();
    ASSERT_EQ(result.status, FetchStatus::OK) << result.detail;
    EXPECT_EQ(result.document.states.size(), 6u);
    EXPECT_EQ(result.http_status, 200);

    const auto requests = rig.http.requests();
    ASSERT_EQ(requests.size(), 2u);
    EXPECT_EQ(requests[0].method, "POST");
    EXPECT_EQ(requests[0].url, OpenSkyClient::kTokenUrl);
    EXPECT_EQ(requests[0].form_body, kExpectedTokenBody);
    EXPECT_EQ(requests[0].bearer_token, "");
    EXPECT_EQ(requests[1].method, "GET");
    EXPECT_EQ(requests[1].url,
              std::string(OpenSkyClient::kStatesUrl) + "?lamin=37.4500&lomin=-122.5500&lamax=37.7500&lomax=-122.1000");
    EXPECT_EQ(requests[1].bearer_token, "test-token");
    EXPECT_TRUE(rig.client.has_token());
    EXPECT_EQ(rig.client.counters().polls, 1u);
    EXPECT_EQ(rig.client.counters().ok, 1u);
    EXPECT_EQ(rig.client.counters().token_requests, 1u);
}

// TC-027 step 5. The origin override rewrites the scheme and host of both
// endpoints and nothing else, for the end-to-end test's local server.
TEST(OpenSkyClientTest, OriginOverrideRewritesBothUrls) {
    FakeHttpClient http;
    FakeClock clock;
    OpenSkyClient local(http, clock, credentials(), AdsbBox{}, "http://127.0.0.1:8080/");
    EXPECT_EQ(local.token_url(), "http://127.0.0.1:8080/auth/realms/opensky-network/protocol/openid-connect/token");
    EXPECT_EQ(local.states_url(), "http://127.0.0.1:8080/api/states/all?lamin=37.4500&lomin=-122.5500&lamax=37.7500&lomax=-122.1000");
    EXPECT_EQ(OpenSkyClient::with_origin("https://a.example/x/y", "http://b:1"), "http://b:1/x/y");
    EXPECT_EQ(OpenSkyClient::with_origin("https://a.example/x/y", ""), "https://a.example/x/y");
}

// TC-027 step 5 (REQ-CFG-004). After a poll with every kind of answer, the
// id and the secret appear in the token request's body and in no URL, no
// bearer and no error text.
TEST(OpenSkyClientTest, SecretTravelsOnlyInTheTokenPostBody) {
    Rig rig;
    rig.http.queue(FakeHttpClient::response(200, ex::kTokenResponse));
    rig.http.queue(FakeHttpClient::response(401, "expired"));
    rig.http.queue(FakeHttpClient::response(200, ex::kSecondTokenResponse));
    rig.http.queue(FakeHttpClient::transport_error(28, "Timeout was reached"));
    const FetchResult result = rig.client.fetch_states();
    EXPECT_EQ(result.status, FetchStatus::TRANSPORT_ERROR);
    EXPECT_EQ(result.detail.find("test-secret"), std::string::npos);
    size_t bodies_with_secret = 0;
    for (const FakeHttpRequest& r : rig.http.requests()) {
        EXPECT_EQ(r.url.find("test-secret"), std::string::npos) << r.url;
        EXPECT_EQ(r.url.find("test-client"), std::string::npos) << r.url;
        EXPECT_EQ(r.bearer_token.find("test-secret"), std::string::npos);
        if (r.form_body.find("test-secret") != std::string::npos) {
            EXPECT_EQ(r.method, "POST");
            bodies_with_secret++;
        }
    }
    EXPECT_EQ(bodies_with_secret, 2u);   // the first token and the refresh
}

// TC-027 step 6. The token is reused inside its lifetime and refreshed at
// the margin before expiry, on the fake clock.
TEST(OpenSkyClientTest, TokenIsReusedUntilTheMarginBeforeExpiry) {
    Rig rig;
    rig.script_token_and_states();
    ASSERT_EQ(rig.client.fetch_states().status, FetchStatus::OK);

    rig.clock.advance((1800 - OpenSkyClient::kTokenRefreshMarginS - 1) * kSecondNs);
    rig.http.queue(FakeHttpClient::response(200, ex::kStatesResponse));
    ASSERT_EQ(rig.client.fetch_states().status, FetchStatus::OK);
    EXPECT_EQ(rig.http.requests().size(), 3u);   // no second POST yet
    EXPECT_EQ(rig.client.counters().token_requests, 1u);

    rig.clock.advance(1 * kSecondNs);
    rig.http.queue(FakeHttpClient::response(200, ex::kSecondTokenResponse));
    rig.http.queue(FakeHttpClient::response(200, ex::kStatesResponse));
    ASSERT_EQ(rig.client.fetch_states().status, FetchStatus::OK);
    const auto requests = rig.http.requests();
    ASSERT_EQ(requests.size(), 5u);
    EXPECT_EQ(requests[3].method, "POST");
    EXPECT_EQ(requests[4].bearer_token, "test-token-2");
    EXPECT_EQ(rig.client.counters().token_requests, 2u);
}

// TC-027 step 6. A token response without expires_in lives the documented
// 1800 s, which the margin shortens as for any other.
TEST(OpenSkyClientTest, ExpiresInDefaultsToThirtyMinutes) {
    Rig rig;
    rig.http.queue(FakeHttpClient::response(200, ex::kTokenResponseWithoutExpiry));
    rig.http.queue(FakeHttpClient::response(200, ex::kStatesResponse));
    ASSERT_EQ(rig.client.fetch_states().status, FetchStatus::OK);
    rig.clock.advance((1800 - OpenSkyClient::kTokenRefreshMarginS - 1) * kSecondNs);
    rig.http.queue(FakeHttpClient::response(200, ex::kStatesResponse));
    ASSERT_EQ(rig.client.fetch_states().status, FetchStatus::OK);
    EXPECT_EQ(rig.client.counters().token_requests, 1u);
    rig.clock.advance(1 * kSecondNs);
    rig.http.queue(FakeHttpClient::response(200, ex::kSecondTokenResponse));
    rig.http.queue(FakeHttpClient::response(200, ex::kStatesResponse));
    ASSERT_EQ(rig.client.fetch_states().status, FetchStatus::OK);
    EXPECT_EQ(rig.client.counters().token_requests, 2u);
}

// TC-027 step 7 (FAULT-013). A 401 on the states request is repaired once:
// a refresh, a retry carrying the new token, the poll ok; counted as one
// auth and one ok.
TEST(OpenSkyClientTest, UnauthorizedGetRefreshesOnceAndRetries) {
    Rig rig;
    rig.http.queue(FakeHttpClient::response(200, ex::kTokenResponse));
    rig.http.queue(FakeHttpClient::response(401, "expired"));
    rig.http.queue(FakeHttpClient::response(200, ex::kSecondTokenResponse));
    rig.http.queue(FakeHttpClient::response(200, ex::kStatesResponse));
    const FetchResult result = rig.client.fetch_states();
    ASSERT_EQ(result.status, FetchStatus::OK) << result.detail;
    const auto requests = rig.http.requests();
    ASSERT_EQ(requests.size(), 4u);
    EXPECT_EQ(requests[1].bearer_token, "test-token");
    EXPECT_EQ(requests[2].method, "POST");
    EXPECT_EQ(requests[3].bearer_token, "test-token-2");
    EXPECT_EQ(rig.client.counters().auth, 1u);
    EXPECT_EQ(rig.client.counters().ok, 1u);
    EXPECT_EQ(rig.client.counters().unauthorized, 0u);
    EXPECT_EQ(rig.client.counters().token_requests, 2u);
}

// TC-027 step 7 (FAULT-013). A second 401 is reported, not looped: exactly
// two GETs and the one refresh between them, the poll yielding nothing.
TEST(OpenSkyClientTest, SecondUnauthorizedIsReportedNotLooped) {
    Rig rig;
    rig.http.queue(FakeHttpClient::response(200, ex::kTokenResponse));
    rig.http.queue(FakeHttpClient::response(401, "expired"));
    rig.http.queue(FakeHttpClient::response(200, ex::kSecondTokenResponse));
    rig.http.queue(FakeHttpClient::response(401, "still no"));
    const FetchResult result = rig.client.fetch_states();
    EXPECT_EQ(result.status, FetchStatus::UNAUTHORIZED);
    EXPECT_EQ(result.http_status, 401);
    EXPECT_EQ(rig.http.requests().size(), 4u);
    EXPECT_EQ(rig.http.pending(), 0u);
    EXPECT_EQ(rig.client.counters().auth, 2u);
    EXPECT_EQ(rig.client.counters().unauthorized, 1u);
    EXPECT_EQ(rig.client.counters().ok, 0u);
}

// TC-027 step 10 (FAULT-013). A token request refused with 400, 401 or 403
// leaves no token and makes no states request: the credentials are wrong.
TEST(OpenSkyClientTest, RefusedTokenIsTokenRefused) {
    for (const long status : {400L, 401L, 403L}) {
        Rig rig;
        rig.http.queue(FakeHttpClient::response(status, "{\"error\":\"invalid_client\"}"));
        const FetchResult result = rig.client.fetch_states();
        EXPECT_EQ(result.status, FetchStatus::TOKEN_REFUSED) << status;
        EXPECT_EQ(result.http_status, status);
        EXPECT_FALSE(rig.client.has_token());
        EXPECT_EQ(rig.http.requests().size(), 1u) << status;
        EXPECT_EQ(rig.client.counters().token_refused, 1u);
        EXPECT_EQ(rig.client.counters().token_requests, 1u);
    }
}

// TC-027 step 10 (FAULT-012 at the token endpoint). A token endpoint in
// error, unreachable, or answering nonsense is counted as unavailable and
// tried again at the next poll, which then succeeds.
TEST(OpenSkyClientTest, TokenEndpointFailureIsUnavailableAndRetriedNextPoll) {
    const HttpResponse failures[] = {
        FakeHttpClient::response(503, "later"),
        FakeHttpClient::transport_error(6, "Could not resolve host"),
        FakeHttpClient::response(200, "nope"),
    };
    for (const HttpResponse& failure : failures) {
        Rig rig;
        rig.http.queue(failure);
        const FetchResult result = rig.client.fetch_states();
        EXPECT_EQ(result.status, FetchStatus::TOKEN_UNAVAILABLE);
        EXPECT_FALSE(rig.client.has_token());
        EXPECT_EQ(rig.http.requests().size(), 1u);
        EXPECT_EQ(rig.client.counters().token_unavailable, 1u);

        rig.script_token_and_states();
        EXPECT_EQ(rig.client.fetch_states().status, FetchStatus::OK);
        EXPECT_EQ(rig.client.counters().token_requests, 2u);
        EXPECT_EQ(rig.client.counters().polls, 2u);
    }
}

// TC-027 step 8 (FAULT-014). 429 reads the Retry-After header when it is
// there, in any case, and leaves it empty when it is not or is not a
// number; the token is kept.
TEST(OpenSkyClientTest, RateLimitedReadsRetryAfterHeader) {
    Rig rig;
    rig.http.queue(FakeHttpClient::response(200, ex::kTokenResponse));
    rig.http.queue(FakeHttpClient::response(429, "", {{"X-Rate-Limit-Retry-After-Seconds", "7"}}));
    FetchResult result = rig.client.fetch_states();
    EXPECT_EQ(result.status, FetchStatus::RATE_LIMITED);
    EXPECT_EQ(result.retry_after_s, std::optional<int64_t>(7));
    EXPECT_EQ(rig.client.counters().rate_limited, 1u);
    EXPECT_EQ(rig.client.counters().last_retry_after_s, std::optional<int64_t>(7));
    EXPECT_TRUE(rig.client.has_token());

    rig.http.queue(FakeHttpClient::response(429, ""));
    result = rig.client.fetch_states();
    EXPECT_EQ(result.status, FetchStatus::RATE_LIMITED);
    EXPECT_FALSE(result.retry_after_s.has_value());

    rig.http.queue(FakeHttpClient::response(429, "", {{"x-rate-limit-retry-after-seconds", "soon"}}));
    result = rig.client.fetch_states();
    EXPECT_EQ(result.status, FetchStatus::RATE_LIMITED);
    EXPECT_FALSE(result.retry_after_s.has_value());
    EXPECT_EQ(rig.client.counters().rate_limited, 3u);
    EXPECT_EQ(rig.client.counters().token_requests, 1u);
}

// TC-027 step 9 (FAULT-012). Server errors and other statuses are counted
// by kind with the token kept; a transport error carries libcurl's text
// and code.
TEST(OpenSkyClientTest, ServerAndClientErrorsAreCountedAndTheTokenKept) {
    Rig rig;
    rig.http.queue(FakeHttpClient::response(200, ex::kTokenResponse));
    for (const long status : {500L, 502L, 503L, 403L, 404L}) {
        rig.http.queue(FakeHttpClient::response(status, "no"));
        const FetchResult result = rig.client.fetch_states();
        EXPECT_EQ(result.status, FetchStatus::HTTP_ERROR) << status;
        EXPECT_EQ(result.http_status, status);
    }
    EXPECT_EQ(rig.client.counters().http_errors, 5u);
    EXPECT_EQ(rig.client.counters().last_http_status, 404);
    EXPECT_EQ(rig.client.counters().token_requests, 1u);
    EXPECT_TRUE(rig.client.has_token());

    rig.http.queue(FakeHttpClient::transport_error(28, "Timeout was reached"));
    const FetchResult timeout = rig.client.fetch_states();
    EXPECT_EQ(timeout.status, FetchStatus::TRANSPORT_ERROR);
    EXPECT_EQ(timeout.detail, "Timeout was reached");
    EXPECT_EQ(timeout.http_status, 0);
    EXPECT_EQ(rig.client.counters().transport_errors, 1u);
    EXPECT_EQ(rig.client.counters().last_error_code, 28);
    EXPECT_EQ(rig.client.counters().polls, 6u);   // five statuses and the timeout; a token request is not a poll
}

// TC-027 step 9 (FAULT-017). A 200 whose body is not a states document is
// counted as malformed, with the parser's problem as the detail.
TEST(OpenSkyClientTest, MalformedStatesBodyIsCounted) {
    Rig rig;
    rig.http.queue(FakeHttpClient::response(200, ex::kTokenResponse));
    rig.http.queue(FakeHttpClient::response(200, "<html>maintenance</html>"));
    const FetchResult result = rig.client.fetch_states();
    EXPECT_EQ(result.status, FetchStatus::MALFORMED);
    EXPECT_EQ(result.detail, "the body is not JSON");
    EXPECT_EQ(rig.client.counters().malformed, 1u);
    EXPECT_EQ(rig.client.counters().ok, 0u);
}

// TC-027 step 5. Form encoding keeps the unreserved characters and escapes
// every other byte, so a secret with '&' or '=' cannot be read as two
// fields.
TEST(OpenSkyClientTest, FormEncodingEscapesReservedCharacters) {
    EXPECT_EQ(OpenSkyClient::form_encode("a b&c=d%"), "a%20b%26c%3Dd%25");
    EXPECT_EQ(OpenSkyClient::form_encode("Az09-._~"), "Az09-._~");
    EXPECT_EQ(OpenSkyClient::form_encode(""), "");
}

TEST(OpenSkyClientTest, EveryFetchStatusHasAName) {
    EXPECT_STREQ(fetch_status_name(FetchStatus::OK), "ok");
    EXPECT_STREQ(fetch_status_name(FetchStatus::TOKEN_REFUSED), "token refused");
    EXPECT_STREQ(fetch_status_name(FetchStatus::TOKEN_UNAVAILABLE), "token unavailable");
    EXPECT_STREQ(fetch_status_name(FetchStatus::UNAUTHORIZED), "unauthorized");
    EXPECT_STREQ(fetch_status_name(FetchStatus::RATE_LIMITED), "rate limited");
    EXPECT_STREQ(fetch_status_name(FetchStatus::HTTP_ERROR), "http error");
    EXPECT_STREQ(fetch_status_name(FetchStatus::TRANSPORT_ERROR), "transport error");
    EXPECT_STREQ(fetch_status_name(FetchStatus::MALFORMED), "malformed");
}
