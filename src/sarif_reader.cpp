#include "sarif_reader.h"

#include <cstdio>
#include <cstdlib>
#include <map>
#include <memory>

namespace edga {

namespace {

// A JSON value: just what reading SARIF needs.
struct Value {
  enum Kind { Null, Bool, Number, String, Array, Object } kind = Null;
  double number = 0;
  bool boolean = false;
  std::string text;
  std::vector<Value> items;
  std::map<std::string, Value> fields;

  const Value& operator[](const char* name) const {
    static const Value none;
    auto it = fields.find(name);
    return it == fields.end() ? none : it->second;
  }
  const Value& at(size_t i) const {
    static const Value none;
    return i < items.size() ? items[i] : none;
  }
};

class Parser {
 public:
  explicit Parser(const std::string& text) : s_(text) {}

  bool parse(Value& out) {
    skip();
    return value(out);
  }

 private:
  void skip() {
    while (i_ < s_.size() && (s_[i_] == ' ' || s_[i_] == '\n' || s_[i_] == '\r' || s_[i_] == '\t'))
      ++i_;
  }

  bool value(Value& out) {
    if (i_ >= s_.size()) return false;
    char c = s_[i_];
    if (c == '{') return object(out);
    if (c == '[') return array(out);
    if (c == '"') {
      out.kind = Value::String;
      return string(out.text);
    }
    if (s_.compare(i_, 4, "true") == 0) {
      out.kind = Value::Bool;
      out.boolean = true;
      i_ += 4;
      return true;
    }
    if (s_.compare(i_, 5, "false") == 0) {
      out.kind = Value::Bool;
      i_ += 5;
      return true;
    }
    if (s_.compare(i_, 4, "null") == 0) {
      i_ += 4;
      return true;
    }
    const char* start = s_.c_str() + i_;
    char* end = nullptr;
    out.number = std::strtod(start, &end);
    if (end == start) return false;
    out.kind = Value::Number;
    i_ += static_cast<size_t>(end - start);
    return true;
  }

  bool object(Value& out) {
    out.kind = Value::Object;
    ++i_;
    skip();
    while (i_ < s_.size() && s_[i_] != '}') {
      std::string key;
      if (s_[i_] != '"' || !string(key)) return false;
      skip();
      if (i_ >= s_.size() || s_[i_] != ':') return false;
      ++i_;
      skip();
      if (!value(out.fields[key])) return false;
      skip();
      if (i_ < s_.size() && s_[i_] == ',') {
        ++i_;
        skip();
      }
    }
    if (i_ >= s_.size()) return false;
    ++i_;
    return true;
  }

  // Elements may lack the comma between them (see the header).
  bool array(Value& out) {
    out.kind = Value::Array;
    ++i_;
    skip();
    while (i_ < s_.size() && s_[i_] != ']') {
      out.items.emplace_back();
      if (!value(out.items.back())) return false;
      skip();
      if (i_ < s_.size() && s_[i_] == ',') {
        ++i_;
        skip();
      }
    }
    if (i_ >= s_.size()) return false;
    ++i_;
    return true;
  }

  bool string(std::string& out) {
    ++i_;
    while (i_ < s_.size() && s_[i_] != '"') {
      char c = s_[i_++];
      if (c != '\\') {
        out += c;
        continue;
      }
      if (i_ >= s_.size()) return false;
      char e = s_[i_++];
      switch (e) {
        case 'n':
          out += '\n';
          break;
        case 't':
          out += '\t';
          break;
        case 'r':
          out += '\r';
          break;
        case 'b':
          out += '\b';
          break;
        case 'f':
          out += '\f';
          break;
        case 'u': {
          if (i_ + 4 > s_.size()) return false;
          unsigned long cp = std::strtoul(s_.substr(i_, 4).c_str(), nullptr, 16);
          i_ += 4;
          if (cp < 0x80) {
            out += static_cast<char>(cp);
          } else if (cp < 0x800) {
            out += static_cast<char>(0xC0 | (cp >> 6));
            out += static_cast<char>(0x80 | (cp & 0x3F));
          } else {
            out += static_cast<char>(0xE0 | (cp >> 12));
            out += static_cast<char>(0x80 | ((cp >> 6) & 0x3F));
            out += static_cast<char>(0x80 | (cp & 0x3F));
          }
          break;
        }
        default:
          out += e;
      }
    }
    if (i_ >= s_.size()) return false;
    ++i_;
    return true;
  }

  const std::string& s_;
  size_t i_ = 0;
};

Diagnostic diagnosticOf(const Value& result) {
  Diagnostic d;
  d.level = result["level"].text;
  d.code = result["ruleId"].text;
  d.message = result["message"]["text"].text;
  const Value& location = result["locations"].at(0)["physicalLocation"];
  d.file = location["artifactLocation"]["uri"].text;
  d.line = static_cast<long>(location["region"]["startLine"].number);
  d.column = static_cast<long>(location["region"]["startColumn"].number);
  return d;
}

}  // namespace

std::vector<Diagnostic> readSarifResults(const std::string& text) {
  std::vector<Diagnostic> out;
  static const std::string start = "{\"ruleId\"";
  for (size_t at = text.find(start); at != std::string::npos; at = text.find(start, at + 1)) {
    const std::string rest = text.substr(at);
    Value result;
    if (Parser(rest).parse(result) && result.kind == Value::Object)
      out.push_back(diagnosticOf(result));
  }
  return out;
}

std::vector<Diagnostic> readSarifDiagnostics(const std::string& path) {
  std::vector<Diagnostic> out;
  std::FILE* f = std::fopen(path.c_str(), "rb");
  if (f == nullptr) return out;
  std::string text;
  char buf[65536];
  size_t n;
  while ((n = std::fread(buf, 1, sizeof buf, f)) > 0) text.append(buf, n);
  std::fclose(f);
  Value root;
  if (!Parser(text).parse(root)) return out;
  for (const Value& run : root["runs"].items) {
    for (const Value& result : run["results"].items) out.push_back(diagnosticOf(result));
  }
  return out;
}

}  // namespace edga
