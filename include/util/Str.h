#pragma once
#include <string>
#include <string_view>
#include <vector>
#include <cctype>

namespace util {

inline void LTrim(std::string& s) {
  size_t i = 0;
  while (i < s.size() && std::isspace(static_cast<unsigned char>(s[i]))) i++;
  s.erase(0, i);
}

inline void RTrim(std::string& s) {
  size_t i = s.size();
  while (i > 0 && std::isspace(static_cast<unsigned char>(s[i-1]))) i--;
  s.erase(i);
}

inline std::string Trim(std::string s) { LTrim(s); RTrim(s); return s; }

inline bool StartsWith(std::string_view s, std::string_view p) {
  return s.size() >= p.size() && s.substr(0, p.size()) == p;
}

inline std::vector<std::string> Split(std::string_view s, char delim) {
  std::vector<std::string> out;
  size_t i = 0;
  while (i <= s.size()) {
    size_t j = s.find(delim, i);
    if (j == std::string_view::npos) j = s.size();
    out.emplace_back(s.substr(i, j - i));
    i = j + 1;
  }
  return out;
}

}
