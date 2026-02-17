#pragma once
#include <cctype>
#include <cstdint>
#include <map>
#include <stdexcept>
#include <string>
#include <string_view>
#include <variant>
#include <vector>

namespace wininspect::json {

struct Value;
using Object = std::map<std::string, Value>;
using Array = std::vector<Value>;
using Null = std::monostate;

struct Value : std::variant<Null, bool, double, std::string, Array, Object> {
  using variant::variant;

  bool is_null() const { return std::holds_alternative<Null>(*this); }
  bool is_bool() const { return std::holds_alternative<bool>(*this); }
  bool is_num() const { return std::holds_alternative<double>(*this); }
  bool is_str() const { return std::holds_alternative<std::string>(*this); }
  bool is_arr() const { return std::holds_alternative<Array>(*this); }
  bool is_obj() const { return std::holds_alternative<Object>(*this); }

  const Object &as_obj() const { return std::get<Object>(*this); }
  const Array &as_arr() const { return std::get<Array>(*this); }
  const std::string &as_str() const { return std::get<std::string>(*this); }
  double as_num() const { return std::get<double>(*this); }
  bool as_bool() const { return std::get<bool>(*this); }

  Object &obj() { return std::get<Object>(*this); }
  Array &arr() { return std::get<Array>(*this); }
  std::string &str() { return std::get<std::string>(*this); }
};

class ParseError : public std::runtime_error {
public:
  using std::runtime_error::runtime_error;
};

class Parser {
public:
  explicit Parser(std::string_view s) : s_(s) {}

  Value parse() {
    skip_ws();
    Value v = parse_value();
    skip_ws();
    if (i_ != s_.size())
      throw ParseError("trailing characters");
    return v;
  }

private:
  std::string_view s_;
  size_t i_ = 0;

  void skip_ws() {
    while (i_ < s_.size() && std::isspace((unsigned char)s_[i_]))
      i_++;
  }

  char peek() const {
    if (i_ >= s_.size())
      return '\0';
    return s_[i_];
  }

  char get() {
    if (i_ >= s_.size())
      throw ParseError("unexpected end");
    return s_[i_++];
  }

  Value parse_value() {
    char c = peek();
    if (c == '{')
      return parse_object();
    if (c == '[')
      return parse_array();
    if (c == '"')
      return parse_string();
    if (c == 't')
      return parse_true();
    if (c == 'f')
      return parse_false();
    if (c == 'n')
      return parse_null();
    if (c == '-' || std::isdigit((unsigned char)c))
      return parse_number();
    throw ParseError("invalid value");
  }

  Value parse_object() {
    Object obj;
    get(); // {
    skip_ws();
    if (peek() == '}') {
      get();
      return obj;
    }
    while (true) {
      skip_ws();
      if (peek() != '"')
        throw ParseError("expected string key");
      std::string key = std::get<std::string>(parse_string());
      skip_ws();
      if (get() != ':')
        throw ParseError("expected ':'");
      skip_ws();
      obj.emplace(std::move(key), parse_value());
      skip_ws();
      char c = get();
      if (c == '}')
        break;
      if (c != ',')
        throw ParseError("expected ',' or '}'");
      skip_ws();
    }
    return obj;
  }

  Value parse_array() {
    Array arr;
    get(); // [
    skip_ws();
    if (peek() == ']') {
      get();
      return arr;
    }
    while (true) {
      skip_ws();
      arr.push_back(parse_value());
      skip_ws();
      char c = get();
      if (c == ']')
        break;
      if (c != ',')
        throw ParseError("expected ',' or ']'");
      skip_ws();
    }
    return arr;
  }

  static int hexval(char c) {
    if (c >= '0' && c <= '9')
      return c - '0';
    if (c >= 'a' && c <= 'f')
      return 10 + (c - 'a');
    if (c >= 'A' && c <= 'F')
      return 10 + (c - 'A');
    return -1;
  }

  Value parse_string() {
    std::string out;
    if (get() != '"')
      throw ParseError("expected '\"'");
    while (true) {
      char c = get();
      if (c == '"')
        break;
      if (c == '\\') {
        char e = get();
        switch (e) {
        case '"':
          out.push_back('"');
          break;
        case '\\':
          out.push_back('\\');
          break;
        case '/':
          out.push_back('/');
          break;
        case 'b':
          out.push_back('\b');
          break;
        case 'f':
          out.push_back('\f');
          break;
        case 'n':
          out.push_back('\n');
          break;
        case 'r':
          out.push_back('\r');
          break;
        case 't':
          out.push_back('\t');
          break;
        case 'u': {
          int h1 = hexval(get()), h2 = hexval(get()), h3 = hexval(get()),
              h4 = hexval(get());
          if (h1 < 0 || h2 < 0 || h3 < 0 || h4 < 0)
            throw ParseError("bad unicode escape");
          uint16_t code = (uint16_t)((h1 << 12) | (h2 << 8) | (h3 << 4) | h4);
          // Minimal UTF-8 encoding for BMP
          if (code < 0x80)
            out.push_back((char)code);
          else if (code < 0x800) {
            out.push_back((char)(0xC0 | (code >> 6)));
            out.push_back((char)(0x80 | (code & 0x3F)));
          } else {
            out.push_back((char)(0xE0 | (code >> 12)));
            out.push_back((char)(0x80 | ((code >> 6) & 0x3F)));
            out.push_back((char)(0x80 | (code & 0x3F)));
          }
          break;
        }
        default:
          throw ParseError("bad escape");
        }
      } else {
        out.push_back(c);
      }
    }
    return out;
  }

  Value parse_true() {
    if (s_.substr(i_, 4) != "true")
      throw ParseError("expected true");
    i_ += 4;
    return true;
  }

  Value parse_false() {
    if (s_.substr(i_, 5) != "false")
      throw ParseError("expected false");
    i_ += 5;
    return false;
  }

  Value parse_null() {
    if (s_.substr(i_, 4) != "null")
      throw ParseError("expected null");
    i_ += 4;
    return Null{};
  }

  Value parse_number() {
    size_t start = i_;
    if (peek() == '-')
      get();
    if (peek() == '0')
      get();
    else {
      if (!std::isdigit((unsigned char)peek()))
        throw ParseError("bad number");
      while (std::isdigit((unsigned char)peek()))
        get();
    }
    if (peek() == '.') {
      get();
      if (!std::isdigit((unsigned char)peek()))
        throw ParseError("bad number");
      while (std::isdigit((unsigned char)peek()))
        get();
    }
    if (peek() == 'e' || peek() == 'E') {
      get();
      if (peek() == '+' || peek() == '-')
        get();
      if (!std::isdigit((unsigned char)peek()))
        throw ParseError("bad number");
      while (std::isdigit((unsigned char)peek()))
        get();
    }
    double v = std::stod(std::string(s_.substr(start, i_ - start)));
    return v;
  }
};

inline Value parse(std::string_view s) { return Parser(s).parse(); }

// Stable serializer: object keys sorted (std::map does that), minimal
// formatting.
inline void dump_string(std::string &out, const std::string &s) {
  out.push_back('"');
  for (unsigned char c : s) {
    switch (c) {
    case '"':
      out += "\\\"";
      break;
    case '\\':
      out += "\\\\";
      break;
    case '\b':
      out += "\\b";
      break;
    case '\f':
      out += "\\f";
      break;
    case '\n':
      out += "\\n";
      break;
    case '\r':
      out += "\\r";
      break;
    case '\t':
      out += "\\t";
      break;
    default:
      if (c < 0x20) {
        char buf[7];
        snprintf(buf, sizeof(buf), "\\u%04x", c);
        out += buf;
      } else
        out.push_back((char)c);
    }
  }
  out.push_back('"');
}

inline void dump(std::string &out, const Value &v);

inline void dump_obj(std::string &out, const Object &o) {
  out.push_back('{');
  bool first = true;
  for (const auto &[k, val] : o) {
    if (!first)
      out.push_back(',');
    first = false;
    dump_string(out, k);
    out.push_back(':');
    dump(out, val);
  }
  out.push_back('}');
}

inline void dump_arr(std::string &out, const Array &a) {
  out.push_back('[');
  bool first = true;
  for (const auto &v : a) {
    if (!first)
      out.push_back(',');
    first = false;
    dump(out, v);
  }
  out.push_back(']');
}

inline void dump(std::string &out, const Value &v) {
  if (std::holds_alternative<Null>(v)) {
    out += "null";
    return;
  }
  if (std::holds_alternative<bool>(v)) {
    out += (std::get<bool>(v) ? "true" : "false");
    return;
  }
  if (std::holds_alternative<double>(v)) {
    // deterministic-ish formatting: use std::to_string then trim
    std::string s = std::to_string(std::get<double>(v));
    // trim trailing zeros
    while (s.size() > 1 && s.find('.') != std::string::npos && s.back() == '0')
      s.pop_back();
    if (!s.empty() && s.back() == '.')
      s.pop_back();
    out += s;
    return;
  }
  if (std::holds_alternative<std::string>(v)) {
    dump_string(out, std::get<std::string>(v));
    return;
  }
  if (std::holds_alternative<Array>(v)) {
    dump_arr(out, std::get<Array>(v));
    return;
  }
  dump_obj(out, std::get<Object>(v));
}

inline std::string dumps(const Value &v) {
  std::string out;
  dump(out, v);
  return out;
}

} // namespace wininspect::json
