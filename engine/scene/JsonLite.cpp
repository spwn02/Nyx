#include "JsonLite.h"

#include <cctype>
#include <cstdlib>
#include <sstream>

namespace Nyx::JsonLite {

static const Object &emptyObj() {
  static Object o{};
  return o;
}
static const Array &emptyArr() {
  static Array a{};
  return a;
}
static const std::string &emptyStr() {
  static std::string s{};
  return s;
}

const Object &Value::asObject() const {
  if (!isObject())
    return emptyObj();
  return std::get<Object>(v);
}
const Array &Value::asArray() const {
  if (!isArray())
    return emptyArr();
  return std::get<Array>(v);
}
const std::string &Value::asString() const {
  if (!isString())
    return emptyStr();
  return std::get<std::string>(v);
}
double Value::asNum(double def) const {
  if (!isNum())
    return def;
  return std::get<double>(v);
}
bool Value::asBool(bool def) const {
  if (!isBool())
    return def;
  return std::get<bool>(v);
}

const Value *Value::get(const char *key) const {
  if (!isObject())
    return nullptr;
  const auto &o = std::get<Object>(v);
  auto it = o.find(key);
  if (it == o.end())
    return nullptr;
  return &it->second;
}
Value *Value::get(const char *key) {
  if (!isObject())
    return nullptr;
  auto &o = std::get<Object>(v);
  auto it = o.find(key);
  if (it == o.end())
    return nullptr;
  return &it->second;
}

// ---------------- Parser ----------------

struct P final {
  std::string_view s;
  size_t i = 0;

  char peek() const { return (i < s.size()) ? s[i] : '\0'; }
  char get() { return (i < s.size()) ? s[i++] : '\0'; }

  void skipWS() {
    while (i < s.size()) {
      char c = s[i];
      if (c == ' ' || c == '\t' || c == '\n' || c == '\r') {
        ++i;
        continue;
      }
      break;
    }
  }

  bool consume(char c) {
    skipWS();
    if (peek() == c) {
      ++i;
      return true;
    }
    return false;
  }
};

static bool fail(P &p, ParseError &err, const char *msg) {
  err.offset = p.i;
  err.message = msg;
  return false;
}

static bool parseValue(P &p, Value &out, ParseError &err);

static bool parseString(P &p, std::string &out, ParseError &err) {
  p.skipWS();
  if (p.get() != '\"')
    return fail(p, err, "expected '\"' to start string");

  std::string r;
  while (true) {
    char c = p.get();
    if (c == '\0')
      return fail(p, err, "unterminated string");
    if (c == '"')
      break;
    if (c == '\\') {
      char e = p.get();
      if (e == '\0')
        return fail(p, err, "bad escape");
      switch (e) {
      case '"':
        r.push_back('"');
        break;
      case '\\':
        r.push_back('\\');
        break;
      case '/':
        r.push_back('/');
        break;
      case 'b':
        r.push_back('\b');
        break;
      case 'f':
        r.push_back('\f');
        break;
      case 'n':
        r.push_back('\n');
        break;
      case 'r':
        r.push_back('\r');
        break;
      case 't':
        r.push_back('\t');
        break;
      default:
        return fail(p, err,
                    "unsupported escape (only basic escapes supported)");
      }
    } else {
      r.push_back(c);
    }
  }

  out = std::move(r);
  return true;
}

static bool parseNumber(P &p, Value &out, ParseError &err) {
  p.skipWS();
  const size_t start = p.i;

  if (p.peek() == '-')
    p.i++;

  bool any = false;
  while (std::isdigit((unsigned char)p.peek())) {
    p.i++;
    any = true;
  }
  if (p.peek() == '.') {
    p.i++;
    while (std::isdigit((unsigned char)p.peek())) {
      p.i++;
      any = true;
    }
  }
  if (!any)
    return fail(p, err, "expected number");

  if (p.peek() == 'e' || p.peek() == 'E') {
    p.i++;
    if (p.peek() == '+' || p.peek() == '-')
      p.i++;
    bool expAny = false;
    while (std::isdigit((unsigned char)p.peek())) {
      p.i++;
      expAny = true;
    }
    if (!expAny)
      return fail(p, err, "bad exponent");
  }

  const std::string tmp(p.s.substr(start, p.i - start));
  char *endp = nullptr;
  double v = std::strtod(tmp.c_str(), &endp);
  if (!endp)
    return fail(p, err, "bad number parse");

  out = Value(v);
  return true;
}

static bool parseArray(P &p, Value &out, ParseError &err) {
  if (!p.consume('['))
    return fail(p, err, "expected '['");

  Array arr;
  p.skipWS();
  if (p.consume(']')) {
    out = Value(std::move(arr));
    return true;
  }

  while (true) {
    Value v;
    if (!parseValue(p, v, err))
      return false;
    arr.push_back(std::move(v));

    p.skipWS();
    if (p.consume(']'))
      break;
    if (!p.consume(','))
      return fail(p, err, "expected ',' or ']'");
  }

  out = Value(std::move(arr));
  return true;
}

static bool parseObject(P &p, Value &out, ParseError &err) {
  if (!p.consume('{'))
    return fail(p, err, "expected '{'");

  Object obj;
  p.skipWS();
  if (p.consume('}')) {
    out = Value(std::move(obj));
    return true;
  }

  while (true) {
    std::string key;
    if (!parseString(p, key, err))
      return false;
    if (!p.consume(':'))
      return fail(p, err, "expected ':'");

    Value v;
    if (!parseValue(p, v, err))
      return false;
    obj.emplace(std::move(key), std::move(v));

    p.skipWS();
    if (p.consume('}'))
      break;
    if (!p.consume(','))
      return fail(p, err, "expected ',' or '}'");
  }

  out = Value(std::move(obj));
  return true;
}

static bool parseValue(P &p, Value &out, ParseError &err) {
  p.skipWS();
  const char c = p.peek();

  if (c == '{')
    return parseObject(p, out, err);
  if (c == '[')
    return parseArray(p, out, err);
  if (c == '"') {
    std::string s;
    if (!parseString(p, s, err))
      return false;
    out = Value(std::move(s));
    return true;
  }
  if (c == '-' || std::isdigit((unsigned char)c))
    return parseNumber(p, out, err);

  if (p.s.substr(p.i, 4) == "true") {
    p.i += 4;
    out = Value(true);
    return true;
  }
  if (p.s.substr(p.i, 5) == "false") {
    p.i += 5;
    out = Value(false);
    return true;
  }
  if (p.s.substr(p.i, 4) == "null") {
    p.i += 4;
    out = Value(nullptr);
    return true;
  }

  return fail(p, err, "unexpected token");
}

bool parse(std::string_view src, Value &out, ParseError &err) {
  P p{src, 0};
  if (!parseValue(p, out, err))
    return false;
  p.skipWS();
  if (p.i != p.s.size())
    return fail(p, err, "trailing characters");
  return true;
}

// ---------------- Writer ----------------

static void indent(std::ostringstream &os, int n) {
  for (int i = 0; i < n; ++i)
    os << ' ';
}

static void writeEscaped(std::ostringstream &os, const std::string &s) {
  os << '"';
  for (char c : s) {
    switch (c) {
    case '"':
      os << "\\\"";
      break;
    case '\\':
      os << "\\\\";
      break;
    case '\n':
      os << "\\n";
      break;
    case '\r':
      os << "\\r";
      break;
    case '\t':
      os << "\\t";
      break;
    default:
      os << c;
      break;
    }
  }
  os << '"';
}

static void writeValue(std::ostringstream &os, const Value &v, bool pretty,
                       int ind, int step);

static void writeArray(std::ostringstream &os, const Array &a, bool pretty,
                       int ind, int step) {
  os << '[';
  if (a.empty()) {
    os << ']';
    return;
  }
  if (pretty)
    os << "\n";
  for (size_t i = 0; i < a.size(); ++i) {
    if (pretty)
      indent(os, ind + step);
    writeValue(os, a[i], pretty, ind + step, step);
    if (i + 1 < a.size())
      os << ',';
    if (pretty)
      os << "\n";
  }
  if (pretty)
    indent(os, ind);
  os << ']';
}

static void writeObject(std::ostringstream &os, const Object &o, bool pretty,
                        int ind, int step) {
  os << '{';
  if (o.empty()) {
    os << '}';
    return;
  }
  if (pretty)
    os << "\n";

  size_t i = 0;
  for (const auto &kv : o) {
    if (pretty)
      indent(os, ind + step);
    writeEscaped(os, kv.first);
    os << (pretty ? ": " : ":");
    writeValue(os, kv.second, pretty, ind + step, step);
    if (++i < o.size())
      os << ',';
    if (pretty)
      os << "\n";
  }
  if (pretty)
    indent(os, ind);
  os << '}';
}

static void writeValue(std::ostringstream &os, const Value &v, bool pretty,
                       int ind, int step) {
  if (v.isNull()) {
    os << "null";
    return;
  }
  if (v.isBool()) {
    os << (std::get<bool>(v.v) ? "true" : "false");
    return;
  }
  if (v.isNum()) {
    os << std::get<double>(v.v);
    return;
  }
  if (v.isString()) {
    writeEscaped(os, std::get<std::string>(v.v));
    return;
  }
  if (v.isArray()) {
    writeArray(os, std::get<Array>(v.v), pretty, ind, step);
    return;
  }
  if (v.isObject()) {
    writeObject(os, std::get<Object>(v.v), pretty, ind, step);
    return;
  }
  os << "null";
}

std::string stringify(const Value &v, bool pretty, int indentStep) {
  std::ostringstream os;
  writeValue(os, v, pretty, 0, indentStep);
  return os.str();
}

} // namespace Nyx::JsonLite
