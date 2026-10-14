// Platform differences the exporter has: real paths, temporary files and file descriptors. The
// POSIX and Windows implementations write the same facts: paths are absolute, with '/' as the
// separator (a Windows path keeps its drive letter).
#ifndef EDGA_PLATFORM_H
#define EDGA_PLATFORM_H

#include <cstdio>
#include <cstdlib>
#include <string>
#include <vector>

#ifdef _WIN32
#include <fcntl.h>
#include <io.h>
#include <sys/stat.h>
#include <windows.h>
#else
#include <fcntl.h>
#include <unistd.h>
#endif

namespace edga {

#ifdef _WIN32
// The one temporary stream the exporter opens at a time (the back end's output when it goes to
// standard output), by name, so it can be removed when the stream closes.
inline std::string& temporaryStreamName() {
  static std::string name;
  return name;
}
#endif

// The file's absolute path as the platform resolves it: symlinks followed where the platform does
// so itself, '.' and '..' removed. Empty for null or empty input, the input itself when it cannot
// be resolved.
inline std::string realPath(const char* path) {
  if (path == nullptr || *path == '\0') return "";
#ifdef _WIN32
  char resolved[4096];
  if (_fullpath(resolved, path, sizeof resolved) == nullptr) return path;
  std::string out(resolved);
  for (char& c : out)
    if (c == '\\') c = '/';
  return out;
#else
  char* resolved = ::realpath(path, nullptr);
  if (resolved == nullptr) return path;
  std::string out(resolved);
  std::free(resolved);
  return out;
#endif
}

// A name for a new empty temporary file, or "": the file is created (and closed again), and the
// caller removes it when done with it.
inline std::string temporaryFile() {
#ifdef _WIN32
  char dir[MAX_PATH + 1];
  char name[MAX_PATH + 1];
  if (GetTempPathA(sizeof dir, dir) == 0 || GetTempFileNameA(dir, "edga", 0, name) == 0) return "";
  return std::string(name);
#else
  const char* dir = std::getenv("TMPDIR");
  std::string pattern = std::string(dir != nullptr && *dir ? dir : "/tmp") + "/edga-XXXXXX";
  std::vector<char> name(pattern.begin(), pattern.end());
  name.push_back('\0');
  int fd = mkstemp(name.data());
  if (fd < 0) return "";
  close(fd);
  return std::string(name.data());
#endif
}

// A stream over a new empty temporary file, or null: what std::tmpfile gives on POSIX, but in the
// platform's temporary directory on Windows, where std::tmpfile uses the current drive's root.
// closeStream removes the file again.
inline std::FILE* temporaryStream() {
#ifdef _WIN32
  std::string& name = temporaryStreamName();
  name = temporaryFile();
  std::FILE* f = name.empty() ? nullptr : std::fopen(name.c_str(), "w+b");
  if (f == nullptr) name.clear();
  return f;
#else
  return std::tmpfile();
#endif
}

// Closes a stream temporaryStream made, removing its file; other streams as fclose does.
inline void closeStream(std::FILE* f) {
  std::fclose(f);
#ifdef _WIN32
  std::string& name = temporaryStreamName();
  if (!name.empty()) {
    std::remove(name.c_str());
    name.clear();
  }
#endif
}

inline int openForWriting(const char* path) {
#ifdef _WIN32
  return _open(path, _O_WRONLY | _O_TRUNC | _O_CREAT | _O_BINARY, _S_IREAD | _S_IWRITE);
#else
  return open(path, O_WRONLY | O_TRUNC);
#endif
}

inline int duplicateFd(int fd) {
#ifdef _WIN32
  return _dup(fd);
#else
  return dup(fd);
#endif
}

inline void replaceFd(int fd, int with) {
#ifdef _WIN32
  _dup2(with, fd);
#else
  dup2(with, fd);
#endif
}

inline void closeFd(int fd) {
#ifdef _WIN32
  _close(fd);
#else
  close(fd);
#endif
}

}  // namespace edga

#endif
