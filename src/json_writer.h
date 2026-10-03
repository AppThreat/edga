// A streaming JSON writer: values are written as they are produced, so a translation unit of any
// size takes constant memory to emit. Strings are written as UTF-8 with the escapes JSON requires;
// bytes that are not valid UTF-8 are written as U+FFFD.
#ifndef EDGA_JSON_WRITER_H
#define EDGA_JSON_WRITER_H

#include <cstdint>
#include <cstdio>
#include <cstring>
#include <string>
#include <vector>

namespace edga {

class JsonWriter {
 public:
  explicit JsonWriter(std::FILE* out) : out_(out) {}

  void beginObject() {
    value();
    put('{');
    first_.push_back(true);
  }
  void endObject() {
    first_.pop_back();
    put('}');
  }
  void beginArray() {
    value();
    put('[');
    first_.push_back(true);
  }
  void endArray() {
    first_.pop_back();
    put(']');
  }

  // The key of the next value in the current object.
  void key(const char* name) {
    separate();
    writeString(name, std::strlen(name));
    put(':');
    afterKey_ = true;
  }

  void string(const char* text) {
    if (text == nullptr) {
      null();
      return;
    }
    string(text, std::strlen(text));
  }
  void string(const char* text, size_t length) {
    value();
    writeString(text, length);
  }
  void string(const std::string& text) { string(text.data(), text.size()); }

  void integer(long long n) {
    value();
    std::fprintf(out_, "%lld", n);
  }
  void unsignedInteger(unsigned long long n) {
    value();
    std::fprintf(out_, "%llu", n);
  }
  void boolean(bool b) {
    value();
    std::fputs(b ? "true" : "false", out_);
  }
  void null() {
    value();
    std::fputs("null", out_);
  }

  // key + value shorthands
  void field(const char* name, const char* text) {
    key(name);
    string(text);
  }
  void field(const char* name, const std::string& text) {
    key(name);
    string(text);
  }
  void field(const char* name, long long n) {
    key(name);
    integer(n);
  }
  void fieldBool(const char* name, bool b) {
    key(name);
    boolean(b);
  }

 private:
  void put(char c) { std::fputc(c, out_); }

  // A comma before every element of an array or member of an object but the first.
  void separate() {
    if (first_.empty()) return;
    if (first_.back()) {
      first_.back() = false;
    } else {
      put(',');
    }
  }

  // Before a value: a value that follows its key needs no separator.
  void value() {
    if (afterKey_) {
      afterKey_ = false;
    } else {
      separate();
    }
  }

  void writeString(const char* text, size_t length) {
    put('"');
    const unsigned char* s = reinterpret_cast<const unsigned char*>(text);
    size_t i = 0;
    while (i < length) {
      unsigned char c = s[i];
      if (c < 0x80) {
        switch (c) {
          case '"':
            std::fputs("\\\"", out_);
            break;
          case '\\':
            std::fputs("\\\\", out_);
            break;
          case '\n':
            std::fputs("\\n", out_);
            break;
          case '\r':
            std::fputs("\\r", out_);
            break;
          case '\t':
            std::fputs("\\t", out_);
            break;
          case '\b':
            std::fputs("\\b", out_);
            break;
          case '\f':
            std::fputs("\\f", out_);
            break;
          default:
            if (c < 0x20) {
              std::fprintf(out_, "\\u%04x", c);
            } else {
              put(static_cast<char>(c));
            }
        }
        ++i;
        continue;
      }
      size_t n = sequenceLength(s + i, length - i);
      if (n == 0) {
        std::fputs("\xEF\xBF\xBD", out_);  // U+FFFD
        ++i;
      } else {
        std::fwrite(s + i, 1, n, out_);
        i += n;
      }
    }
    put('"');
  }

  // The length of the valid UTF-8 sequence at `s`, or 0 when it is not one.
  static size_t sequenceLength(const unsigned char* s, size_t available) {
    unsigned char c = s[0];
    size_t n;
    uint32_t min;
    uint32_t cp;
    if ((c & 0xE0) == 0xC0) {
      n = 2;
      min = 0x80;
      cp = c & 0x1F;
    } else if ((c & 0xF0) == 0xE0) {
      n = 3;
      min = 0x800;
      cp = c & 0x0F;
    } else if ((c & 0xF8) == 0xF0) {
      n = 4;
      min = 0x10000;
      cp = c & 0x07;
    } else {
      return 0;
    }
    if (n > available) return 0;
    for (size_t k = 1; k < n; ++k) {
      if ((s[k] & 0xC0) != 0x80) return 0;
      cp = (cp << 6) | (s[k] & 0x3F);
    }
    if (cp < min || cp > 0x10FFFF || (cp >= 0xD800 && cp <= 0xDFFF)) return 0;
    return n;
  }

  std::FILE* out_;
  std::vector<bool> first_;
  bool afterKey_ = false;
};

}  // namespace edga

#endif
