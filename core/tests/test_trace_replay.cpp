#include "doctest/doctest.h"
#include "wininspect/core.hpp"
#include "wininspect/fake_backend.hpp"
#include <fstream>
#include <sstream>

using namespace wininspect;

static std::string read_file(const char* path) {
  std::ifstream f(path, std::ios::binary);
  std::ostringstream ss;
  ss << f.rdbuf();
  return ss.str();
}

DOCTEST_TEST_CASE("trace replay: two_clients_non_interference") {
  // Minimal replay: validate key expectations using fake backend
  auto trace = wininspect::json::parse(read_file("formal/traces/two_clients_non_interference.json")).as_obj();
  auto windows = trace.at("initial_world").as_obj().at("windows").as_arr();

  std::vector<FakeWindow> fw;
  for (const auto& w : windows) {
    auto& o = w.as_obj();
    auto hwnd_s = o.at("hwnd").as_str();
    // parse as hex without 0x for simplicity
    std::uint64_t hwnd = std::stoull(hwnd_s.substr(2), nullptr, 16);
    fw.push_back({(hwnd_u64)hwnd,0,0,o.at("title").as_str(),o.at("class").as_str(), o.at("visible").as_bool()});
  }
  FakeBackend fb(std::move(fw));
  Snapshot s = fb.capture_snapshot();
  CoreEngine core(&fb);

  // c2 ensureVisible twice -> second changed=false
  CoreRequest req;
  req.id="c2-1";
  req.method="window.ensureVisible";
  req.params["hwnd"]=std::string("0x2");
  req.params["visible"]=true;

  auto r1 = core.handle(req, s);
  DOCTEST_REQUIRE(r1.ok);

  req.id="c2-2";
  auto r2 = core.handle(req, s);
  DOCTEST_REQUIRE(r2.ok);
  DOCTEST_REQUIRE_EQ(r2.result.as_obj().at("changed").as_bool(), false);
}
