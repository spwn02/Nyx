#pragma once
#include <cstdint>
#include <cstddef>
#include <string>
#include <string_view>
#include <unordered_map>
#include <variant>
#include <vector>

namespace Nyx::JsonLite {

struct Value;
using Object = std::unordered_map<std::string, Value>;
using Array = std::vector<Value>;

struct Value final {
  using Storage = std::variant<std::nullptr_t, bool, double, std::string, Array,
                               Object>;
  Storage v = nullptr;

  Value() = default;
  Value(std::nullptr_t) : v(nullptr) {}
  Value(bool b) : v(b) {}
  Value(double n) : v(n) {}
  Value(int n) : v(double(n)) {}
  Value(uint64_t n) : v(double(n)) {}
  Value(const char *s) : v(std::string(s)) {}
  Value(std::string s) : v(std::move(s)) {}
  Value(Array a) : v(std::move(a)) {}
  Value(Object o) : v(std::move(o)) {}

  bool isNull() const { return std::holds_alternative<std::nullptr_t>(v); }
  bool isBool() const { return std::holds_alternative<bool>(v); }
  bool isNum() const { return std::holds_alternative<double>(v); }
  bool isString() const { return std::holds_alternative<std::string>(v); }
  bool isArray() const { return std::holds_alternative<Array>(v); }
  bool isObject() const { return std::holds_alternative<Object>(v); }

  const Object &asObject() const;
  const Array &asArray() const;
  const std::string &asString() const;
  double asNum(double def = 0.0) const;
  bool asBool(bool def = false) const;

  const Value *get(const char *key) const;
  Value *get(const char *key);
};

// Parsing / writing
struct ParseError final {
  size_t offset = 0;
  std::string message;
};

bool parse(std::string_view src, Value &out, ParseError &err);
std::string stringify(const Value &v, bool pretty = true, int indent = 2);

} // namespace Nyx::JsonLite
