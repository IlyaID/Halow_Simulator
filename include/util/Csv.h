#pragma once
#include "Result.h"
#include "Str.h"
#include <string>
#include <vector>
#include <unordered_map>
#include <fstream>

namespace util {

struct CsvTable {
  std::vector<std::string> header;
  std::vector<std::vector<std::string>> rows;

  int ColIndex(const std::string& name) const {
    for (int i = 0; i < (int)header.size(); i++) if (header[i] == name) return i;
    return -1;
  }
};

inline Result<std::vector<std::string>> ParseCsvLine(const std::string& line) {
  std::vector<std::string> out;
  std::string cur;
  bool inQuotes = false;

  for (size_t i = 0; i < line.size(); i++) {
    char c = line[i];
    if (inQuotes) {
      if (c == '"') {
        if (i + 1 < line.size() && line[i + 1] == '"') { // escaped quote
          cur.push_back('"');
          i++;
        } else {
          inQuotes = false;
        }
      } else {
        cur.push_back(c);
      }
    } else {
      if (c == ',') {
        out.push_back(cur);
        cur.clear();
      } else if (c == '"') {
        inQuotes = true;
      } else {
        cur.push_back(c);
      }
    }
  }

  if (inQuotes) return Result<std::vector<std::string>>::Err("CSV parse error: unterminated quotes");
  out.push_back(cur);
  return Result<std::vector<std::string>>::Ok(out);
}

inline Result<CsvTable> ReadCsv(const std::string& path, bool hasHeader = true) {
  std::ifstream f(path);
  if (!f) return Result<CsvTable>::Err("Cannot open CSV: " + path);

  CsvTable t;
  std::string line;
  bool first = true;

  while (std::getline(f, line)) {
    if (!line.empty() && (line.back() == '\r')) line.pop_back();
    if (util::Trim(line).empty()) continue;
    if (util::StartsWith(util::Trim(line), "#")) continue;

    auto parsed = ParseCsvLine(line);
    if (!parsed.ok()) return Result<CsvTable>::Err(parsed.error());

    if (first && hasHeader) {
      t.header = parsed.value();
      for (auto& h : t.header) h = util::Trim(h);
      first = false;
      continue;
    }

    auto row = parsed.value();
    for (auto& x : row) x = util::Trim(x);
    t.rows.push_back(std::move(row));
    first = false;
  }

  return Result<CsvTable>::Ok(std::move(t));
}

}
