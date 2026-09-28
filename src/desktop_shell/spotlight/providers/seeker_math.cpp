#include "desktop_shell/spotlight/providers/seeker_registry.hpp"

#include <cmath>
#include <cstdio>
#include <optional>

namespace eh::shell::seeker {

namespace {

struct Parser {
  std::string_view s;
  std::size_t pos = 0;
  bool ok = true;

  void skip() {
    while (pos < s.size() && (s[pos] == ' ' || s[pos] == '\t')) ++pos;
  }
  std::optional<double> expr() {
    auto v = term();
    if (!v) return std::nullopt;
    for (;;) {
      skip();
      if (pos >= s.size()) break;
      char c = s[pos];
      if (c != '+' && c != '-') break;
      ++pos;
      auto r = term();
      if (!r) return std::nullopt;
      v = (c == '+') ? (*v + *r) : (*v - *r);
    }
    return v;
  }
  std::optional<double> term() {
    auto v = factor();
    if (!v) return std::nullopt;
    for (;;) {
      skip();
      if (pos >= s.size()) break;
      char c = s[pos];
      if (c != '*' && c != '/' && c != '%') break;
      ++pos;
      if (c == '*' && pos < s.size() && s[pos] == '*') {
        ++pos;
        auto r = factor();
        if (!r) return std::nullopt;
        *v = std::pow(*v, *r);
        continue;
      }
      auto r = factor();
      if (!r) return std::nullopt;
      if (c == '*') *v *= *r;
      else if (c == '/') {
        if (*r == 0.0) {
          ok = false;
          return std::nullopt;
        }
        *v /= *r;
      } else {
        if (*r == 0.0) {
          ok = false;
          return std::nullopt;
        }
        *v = std::fmod(*v, *r);
      }
    }
    return v;
  }
  std::optional<double> factor() {
    skip();
    if (pos >= s.size()) return std::nullopt;
    if (s[pos] == '(') {
      ++pos;
      auto v = expr();
      if (!v) return std::nullopt;
      skip();
      if (pos >= s.size() || s[pos] != ')') return std::nullopt;
      ++pos;
      return v;
    }
    if (s[pos] == '+' || s[pos] == '-') {
      bool neg = (s[pos] == '-');
      ++pos;
      auto v = factor();
      if (!v) return std::nullopt;
      return neg ? -*v : *v;
    }
    std::size_t start = pos;
    bool dot = false;
    while (pos < s.size() && ((s[pos] >= '0' && s[pos] <= '9') || s[pos] == '.')) {
      if (s[pos] == '.') {
        if (dot) break;
        dot = true;
      }
      ++pos;
    }
    if (start == pos) return std::nullopt;
    try {
      return std::stod(std::string(s.substr(start, pos - start)));
    } catch (...) {
      return std::nullopt;
    }
  }
};

std::optional<double> evaluate(std::string_view text) {
  if (text.empty()) return std::nullopt;
  bool hasDigit = false, hasOp = false;
  for (char c : text) {
    if (c >= '0' && c <= '9') hasDigit = true;
    if (c == '+' || c == '-' || c == '*' || c == '/' || c == '%' || c == '(' || c == '^') hasOp = true;
  }
  if (!hasDigit || !hasOp) return std::nullopt;
  Parser p{text, 0, true};
  auto v = p.expr();
  if (!v || !p.ok) return std::nullopt;
  p.skip();
  if (p.pos != p.s.size()) return std::nullopt;
  if (!std::isfinite(*v)) return std::nullopt;
  return v;
}

void math_query(std::string_view q, std::vector<SpotlightHit>& out) {
  auto v = evaluate(q);
  if (!v) return;
  char buf[64];
  if (*v == std::floor(*v) && std::abs(*v) < 1e15)
    std::snprintf(buf, sizeof(buf), "%.0f", *v);
  else
    std::snprintf(buf, sizeof(buf), "%.6g", *v);
  SpotlightHit h;
  h.name = buf;
  h.genericName = "Calculator";
  h.exec = "";
  h.iconKey = "accessories-calculator";
  h.score = 1000;
  out.push_back(std::move(h));
}

struct MathReg {
  MathReg() {
    Provider p;
    p.name = "math";
    p.prefix = "calc";
    p.global = true;
    p.query = math_query;
    register_provider(std::move(p));
  }
};

MathReg g_mathReg;

}

}
