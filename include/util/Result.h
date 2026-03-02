#pragma once
#include <string>
#include <utility>

namespace util {

// Primary template: Result<T>
template <class T>
class Result {
public:
  static Result Ok(T v) { return Result(true, std::move(v), ""); }
  static Result Err(std::string e) { return Result(false, T{}, std::move(e)); }

  bool ok() const { return m_ok; }
  const T& value() const { return m_val; }
  T& value() { return m_val; }
  const std::string& error() const { return m_err; }

private:
  Result(bool ok, T v, std::string e) : m_ok(ok), m_val(std::move(v)), m_err(std::move(e)) {}
  bool m_ok{false};
  T m_val{};
  std::string m_err;
};

// Specialization: Result<void>
template <>
class Result<void> {
public:
  static Result Ok() { return Result(true, ""); }
  static Result Err(std::string e) { return Result(false, std::move(e)); }

  bool ok() const { return m_ok; }
  const std::string& error() const { return m_err; }

private:
  Result(bool ok, std::string e) : m_ok(ok), m_err(std::move(e)) {}
  bool m_ok{false};
  std::string m_err;
};

} // namespace util
