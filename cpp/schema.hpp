// Host-side reader for schema 2 JSON (types with methods, functions, context). dslvm.hpp only hashes the schema bytes;
// the host reads the signatures to (a) type-check every argument and result at call time, because the VM verifier checks names
// and arities but not types, so hand-built bytecode with the right schema hash could otherwise hand a Path to a Paint parameter,
// and (b) refuse to start when its registry and the schema disagree. No dependencies; strict about the shape it understands.
#pragma once
#include <cctype>
#include <cstdlib>
#include <map>
#include <string>
#include <vector>

#include "dslvm.hpp"

namespace schema {

struct Sig {
  std::vector<std::string> params;
  std::string returns;
};

struct Schema {
  std::map<std::string, std::map<std::string, Sig>> types;  // type -> method -> signature
  std::map<std::string, Sig> functions;
  std::map<std::string, std::string> context;               // variable -> type
};

namespace detail {

struct Json {
  enum Kind { OBJ, ARR, STR, OTHER } kind = OTHER;
  std::string s;
  std::vector<std::pair<std::string, Json>> members;  // for OBJ
  std::vector<Json> items;                             // for ARR
  const Json* find(const std::string& key) const {
    for (const auto& m : members) if (m.first == key) return &m.second;
    return nullptr;
  }
};

class Parser {
 public:
  explicit Parser(const std::string& text) : t_(text) {}
  Json parse() {
    Json v = value();
    ws();
    if (i_ != t_.size()) fail("trailing text");
    return v;
  }

 private:
  [[noreturn]] void fail(const std::string& why) { throw dslvm::Error("schema is not valid JSON at offset " + std::to_string(i_) + ": " + why); }
  void ws() { while (i_ < t_.size() && isspace(static_cast<unsigned char>(t_[i_]))) ++i_; }
  void expect(char c) { ws(); if (i_ >= t_.size() || t_[i_] != c) fail(std::string("expected '") + c + "'"); ++i_; }
  Json value() {
    ws();
    if (i_ >= t_.size()) fail("unexpected end");
    char c = t_[i_];
    Json v;
    if (c == '{') {
      v.kind = Json::OBJ; ++i_; ws();
      if (t_[i_] == '}') { ++i_; return v; }
      for (;;) {
        ws(); std::string key = str(); expect(':');
        v.members.emplace_back(key, value()); ws();
        if (i_ < t_.size() && t_[i_] == ',') { ++i_; continue; }
        expect('}'); return v;
      }
    }
    if (c == '[') {
      v.kind = Json::ARR; ++i_; ws();
      if (t_[i_] == ']') { ++i_; return v; }
      for (;;) {
        v.items.push_back(value()); ws();
        if (i_ < t_.size() && t_[i_] == ',') { ++i_; continue; }
        expect(']'); return v;
      }
    }
    if (c == '"') { v.kind = Json::STR; v.s = str(); return v; }
    size_t start = i_;  // number, true, false, null: not needed by the schema, skipped
    while (i_ < t_.size() && (isalnum(static_cast<unsigned char>(t_[i_])) || t_[i_] == '-' || t_[i_] == '.' || t_[i_] == '+')) ++i_;
    if (i_ == start) fail("unexpected character");
    return v;
  }
  std::string str() {
    if (i_ >= t_.size() || t_[i_] != '"') fail("expected a string");
    std::string out;
    for (++i_; i_ < t_.size() && t_[i_] != '"'; ++i_) {
      if (t_[i_] == '\\' && i_ + 1 < t_.size()) ++i_;  // schema names never contain escapes; keep the next character
      out += t_[i_];
    }
    if (i_ >= t_.size()) fail("unterminated string");
    ++i_;
    return out;
  }
  const std::string& t_;
  size_t i_ = 0;
};

inline Sig sig(const Json& j, const std::string& selfType = {}) {
  Sig s;
  const Json* p = j.find("params");
  const Json* r = j.find("returns");
  if (!p || p->kind != Json::ARR || !r || r->kind != Json::STR) throw dslvm::Error("schema: a signature needs params (array) and returns (string)");
  for (const Json& x : p->items) s.params.push_back(x.s);
  s.returns = r->s == "self" ? selfType : r->s;
  return s;
}

}  // namespace detail

inline Schema parse(const std::string& text) {
  detail::Json root = detail::Parser(text).parse();
  Schema out;
  if (root.kind != detail::Json::OBJ) throw dslvm::Error("schema: the top level must be an object");
  if (const detail::Json* types = root.find("types"))
    for (const auto& t : types->members) {
      const detail::Json* methods = t.second.find("methods");
      auto& dst = out.types[t.first];
      if (methods) for (const auto& m : methods->members) {
        // Schema 2 keeps one or more signatures in an array under each name. The
        // lmvideo host currently has no overloaded registrations, so retain the
        // sole declaration while accepting the common representation.
        if (m.second.kind != detail::Json::ARR || m.second.items.size() != 1)
          throw dslvm::Error("schema: host registration requires exactly one signature array for method " + t.first + "." + m.first);
        const detail::Json* entry = &m.second.items.front();
        dst[m.first] = detail::sig(*entry, t.first);
      }
    }
  if (const detail::Json* fns = root.find("functions"))
    for (const auto& f : fns->members) {
      if (f.second.kind != detail::Json::ARR || f.second.items.size() != 1)
        throw dslvm::Error("schema: host registration requires exactly one signature array for function " + f.first);
      const detail::Json* entry = &f.second.items.front();
      out.functions[f.first] = detail::sig(*entry);
    }
  if (const detail::Json* ctx = root.find("context"))
    for (const auto& c : ctx->members) out.context[c.first] = c.second.s;
  return out;
}

}  // namespace schema
