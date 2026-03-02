#pragma once
#include "Result.h"
#include <string>
#include <string_view>
#include <vector>
#include <map>
#include <variant>
#include <cctype>
#include <cstdlib>
#include <fstream>
#include <type_traits>

namespace util {

struct Json;
using JsonNull = std::monostate;
using JsonBool = bool;
using JsonNum  = double;
using JsonStr  = std::string;
using JsonArr  = std::vector<Json>;
using JsonObj  = std::map<std::string, Json>;

struct Json {
  std::variant<JsonNull, JsonBool, JsonNum, JsonStr, JsonArr, JsonObj> v;

  bool isNull() const { return std::holds_alternative<JsonNull>(v); }
  bool isBool() const { return std::holds_alternative<JsonBool>(v); }
  bool isNum()  const { return std::holds_alternative<JsonNum>(v); }
  bool isStr()  const { return std::holds_alternative<JsonStr>(v); }
  bool isArr()  const { return std::holds_alternative<JsonArr>(v); }
  bool isObj()  const { return std::holds_alternative<JsonObj>(v); }

  const JsonObj& obj() const { return std::get<JsonObj>(v); }
  const JsonArr& arr() const { return std::get<JsonArr>(v); }
  const JsonStr& str() const { return std::get<JsonStr>(v); }
  JsonNum num() const { return std::get<JsonNum>(v); }
  JsonBool b()  const { return std::get<JsonBool>(v); }

  const Json* getPtr(const std::string& k) const {
    if (!isObj()) return nullptr;
    auto it = obj().find(k);
    if (it == obj().end()) return nullptr;
    return &it->second;
  }

  const Json& at(const std::string& k) const { return obj().at(k); }

  template<typename T>
  T value(const std::string& k, const T& def) const {
    auto* p = getPtr(k);
    if (!p) return def;
    if constexpr (std::is_same_v<T, int>) {
      if (!p->isNum()) return def;
      return static_cast<int>(p->num());
    } else if constexpr (std::is_same_v<T, double>) {
      if (!p->isNum()) return def;
      return p->num();
    } else if constexpr (std::is_same_v<T, bool>) {
      if (!p->isBool()) return def;
      return p->b();
    } else if constexpr (std::is_same_v<T, std::string>) {
      if (!p->isStr()) return def;
      return p->str();
    } else {
      return def;
    }
  }

  std::vector<double> valueArrNum(const std::string& k,
                                  const std::vector<double>& def) const {
    auto* p = getPtr(k);
    if (!p || !p->isArr()) return def;
    std::vector<double> out;
    for (auto& e : p->arr()) {
      if (e.isNum()) out.push_back(e.num());
    }
    return out;
  }
};

class JsonParser {
public:
  explicit JsonParser(std::string_view s) : m_s(s) {}

  Result<Json> Parse() {
    SkipWs();
    auto v = ParseValue();
    if (!v.ok()) return v;
    SkipWs();
    if (m_i != m_s.size())
      return Result<Json>::Err("JSON trailing characters at pos " + std::to_string(m_i));
    return v;
  }

private:
  void SkipWs() {
    while (m_i < m_s.size() && std::isspace((unsigned char)m_s[m_i])) m_i++;
  }

  char Peek() const { return (m_i < m_s.size()) ? m_s[m_i] : '\0'; }

  bool Consume(char c) {
    if (Peek() == c) { m_i++; return true; }
    return false;
  }

  bool Match(std::string_view lit) {
    if (m_s.substr(m_i, lit.size()) == lit) {
      m_i += lit.size();
      return true;
    }
    return false;
  }

  static Result<Json> Ok(Json j) {
    return Result<Json>::Ok(std::move(j));
  }

  Result<Json> ParseValue() {
    SkipWs();
    char c = Peek();
    if (c == '{') return ParseObj();
    if (c == '[') return ParseArr();
    if (c == '\"') return ParseStr();
    if (c == '-' || std::isdigit((unsigned char)c)) return ParseNum();
    if (Match("true"))  return Ok(Json{JsonBool(true)});
    if (Match("false")) return Ok(Json{JsonBool(false)});
    if (Match("null"))  return Ok(Json{JsonNull{}});
    return Result<Json>::Err("JSON unexpected token at pos " + std::to_string(m_i));
  }

  Result<Json> ParseStr() {
    if (!Consume('\"'))
      return Result<Json>::Err("JSON expected '\"' at pos " + std::to_string(m_i));
    std::string out;
    while (m_i < m_s.size()) {
      char c = m_s[m_i++];
      if (c == '\"') return Ok(Json{std::move(out)});
      if (c == '\\') {
        if (m_i >= m_s.size())
          return Result<Json>::Err("JSON bad escape at end");
        char e = m_s[m_i++];
        switch (e) {
        case '\"': out.push_back('\"'); break;
        case '\\': out.push_back('\\'); break;
        case '/':  out.push_back('/');  break;
        case 'b':  out.push_back('\b'); break;
        case 'f':  out.push_back('\f'); break;
        case 'n':  out.push_back('\n'); break;
        case 'r':  out.push_back('\r'); break;
        case 't':  out.push_back('\t'); break;
        default:
          return Result<Json>::Err("JSON unsupported escape at pos " + std::to_string(m_i));
        }
      } else {
        out.push_back(c);
      }
    }
    return Result<Json>::Err("JSON unterminated string");
  }

  Result<Json> ParseNum() {
    size_t start = m_i;
    if (Peek() == '-') m_i++;
    while (std::isdigit((unsigned char)Peek())) m_i++;
    if (Peek() == '.') {
      m_i++;
      while (std::isdigit((unsigned char)Peek())) m_i++;
    }
    if (Peek() == 'e' || Peek() == 'E') {
      m_i++;
      if (Peek() == '+' || Peek() == '-') m_i++;
      while (std::isdigit((unsigned char)Peek())) m_i++;
    }
    auto sv = m_s.substr(start, m_i - start);
    char* endp = nullptr;
    double x = std::strtod(std::string(sv).c_str(), &endp);
    if (!endp)
      return Result<Json>::Err("JSON number parse error");
    return Ok(Json{JsonNum(x)});
  }

  Result<Json> ParseArr() {
    if (!Consume('['))
      return Result<Json>::Err("JSON expected '['");
    SkipWs();
    JsonArr arr;
    if (Consume(']')) return Ok(Json{std::move(arr)});

    while (true) {
      auto v = ParseValue();
      if (!v.ok()) return v;
      arr.push_back(std::move(v.value()));
      SkipWs();
      if (Consume(']')) break;
      if (!Consume(','))
        return Result<Json>::Err("JSON expected ',' or ']' at pos " + std::to_string(m_i));
      SkipWs();
    }
    return Ok(Json{std::move(arr)});
  }

  Result<Json> ParseObj() {
    if (!Consume('{'))
      return Result<Json>::Err("JSON expected '{'");
    SkipWs();
    JsonObj obj;
    if (Consume('}')) return Ok(Json{std::move(obj)});

    while (true) {
      SkipWs();
      auto k = ParseStr();
      if (!k.ok() || !k.value().isStr())
        return Result<Json>::Err("JSON expected string key at pos " + std::to_string(m_i));
      SkipWs();
      if (!Consume(':'))
        return Result<Json>::Err("JSON expected ':' at pos " + std::to_string(m_i));
      SkipWs();
      auto v = ParseValue();
      if (!v.ok()) return v;
      obj.emplace(k.value().str(), std::move(v.value()));
      SkipWs();
      if (Consume('}')) break;
      if (!Consume(','))
        return Result<Json>::Err("JSON expected ',' or '}' at pos " + std::to_string(m_i));
      SkipWs();
    }
    return Ok(Json{std::move(obj)});
  }

  std::string_view m_s;
  size_t m_i{0};
};

inline Result<Json> ParseJson(std::string_view s) {
  JsonParser p(s);
  return p.Parse();
}

inline Result<Json> ReadJson(const std::string& path) {
  std::ifstream f(path);
  if (!f.is_open()) {
    return Result<Json>::Err("Cannot open JSON: " + path);
  }
  std::string s((std::istreambuf_iterator<char>(f)),
                std::istreambuf_iterator<char>());
  return ParseJson(s);
}

} // namespace util