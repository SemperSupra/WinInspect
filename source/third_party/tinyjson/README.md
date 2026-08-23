# TinyJSON

A zero-dependency, single-header JSON parser and serializer in C++17.

Approximately 400 lines, no external dependencies, works on any C++17 compiler.

## Features

- Parse JSON strings into `Object`, `Array`, `String`, `Number`, `Bool`, or `Null`
- Serialize back to JSON string
- No dynamic memory allocation (uses `std::vector`, `std::map`, `std::string`)
- No exceptions (returns sentinel values on parse error)
- Compliant with RFC 8259 subset (no duplicate key detection)

## Usage

```cpp
#include "tinyjson.hpp"
using namespace wininspect::json;

// Parse
auto v = parse(R"({"hello": "world", "count": 42})");
if (v.is_obj()) {
    auto& obj = v.as_obj();
    // Access fields
    std::string msg = obj.at("hello").as_str();  // "world"
    double count = obj.at("count").as_num();      // 42.0
}

// Construct
Object o;
o["name"] = "WinInspect";
o["version"] = 1.0;
o["active"] = true;
o["tags"] = {"json", "parser", "header-only"};

// Serialize
std::string json = dumps(o);
// {"active":true,"name":"WinInspect","tags":["json","parser","header-only"],"version":1}
```

## API Reference

### Types

| C++ Type | JSON Type | Methods |
|---|---|---|
| `Null` | `null` | — |
| `Bool` | `true`/`false` | `as_bool()` |
| `Number` | number | `as_num()` |
| `String` | string | `as_str()` |
| `Array` | array | `as_arr()`, `operator[]` |
| `Object` | object | `as_obj()`, `at(key)`, `operator[]` |

All types: `is_null()`, `is_bool()`, `is_num()`, `is_str()`, `is_arr()`, `is_obj()`

### Functions

- `parse(const std::string&)` → `Value` (sentry Null on parse error)
- `dumps(const Value&)` → `std::string` (pretty-print with indentation)
- `dumps(const Value&, int indent)` → `std::string` (custom indent level)

## Integration

```cmake
# In your CMakeLists.txt
add_library(tinyjson INTERFACE)
target_include_directories(tinyjson INTERFACE third_party/tinyjson)
target_link_libraries(your_target PRIVATE tinyjson)
```

## License

PolyForm Noncommercial 1.0.0. See [LICENSE](../../LICENSE) for details.
