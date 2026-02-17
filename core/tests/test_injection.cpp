#include "doctest/doctest.h"
#include "wininspect/core.hpp"
#include "wininspect/fake_backend.hpp"

using namespace wininspect;

DOCTEST_TEST_CASE("Injection methods work") {
  FakeBackend backend({});
  CoreEngine core(&backend);

  // Test window.postMessage
  {
    // Need a fake window to target
    CoreRequest req;
    req.id = "1";
    req.method = "window.postMessage";

    // We can't really verify postMessage side effects on FakeBackend easily
    // without extending FakeBackend to record them, but we can verify the call
    // succeeds. Let's modify FakeBackend to be observable or just check success
    // response.

    json::Object params;
    params["hwnd"] = "0x1234";
    params["msg"] = 100.0; // WM_...
    req.params = params;

    CoreResponse resp = core.handle(req, {});
    DOCTEST_REQUIRE(resp.ok);

    // Check missing params
    req.params.clear();
    resp = core.handle(req, {});
    DOCTEST_REQUIRE(!resp.ok);
  }

  // Test input.send
  {
    CoreRequest req;
    req.id = "2";
    req.method = "input.send";

    // Valid base64
    json::Object params;
    params["data_b64"] = "AAAA"; // Valid base64 (3 bytes 0x000000)
    req.params = params;

    CoreResponse resp = core.handle(req, {});
    DOCTEST_REQUIRE(resp.ok);

    // Missing param
    req.params.clear();
    resp = core.handle(req, {});
    DOCTEST_REQUIRE(!resp.ok);
  }

  // Test input.mouseClick
  {
    CoreRequest req;
    req.id = "3";
    req.method = "input.mouseClick";
    json::Object params;
    params["x"] = 100.0;
    params["y"] = 200.0;
    params["button"] = 0.0;
    req.params = params;

    CoreResponse resp = core.handle(req, {});
    DOCTEST_REQUIRE(resp.ok);

    auto events = backend.get_injected_events();
    DOCTEST_REQUIRE(events.size() > 0);
    DOCTEST_REQUIRE(events.back() == "mouse_click:100,200,0");
  }

  // Test input.keyPress
  {
    CoreRequest req;
    req.id = "4";
    req.method = "input.keyPress";
    json::Object params;
    params["vk"] = 65.0; // 'A'
    req.params = params;

    CoreResponse resp = core.handle(req, {});
    DOCTEST_REQUIRE(resp.ok);

    auto events = backend.get_injected_events();
    DOCTEST_REQUIRE(events.size() > 0);
    DOCTEST_REQUIRE(events.back() == "key_press:65");
  }

  // Test input.text
  {
    CoreRequest req;
    req.id = "5";
    req.method = "input.text";
    json::Object params;
    params["text"] = "hello";
    req.params = params;

    CoreResponse resp = core.handle(req, {});
    DOCTEST_REQUIRE(resp.ok);

    auto events = backend.get_injected_events();
    DOCTEST_REQUIRE(events.size() > 0);
    DOCTEST_REQUIRE(events.back() == "text:hello");
  }
}
