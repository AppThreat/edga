// Reads the diagnostics the front end wrote as SARIF. The front end writes no comma between two
// results when it reports no errors (`...}{"ruleId"...`), so the reader accepts an array's elements
// with or without one.
#ifndef EDGA_SARIF_READER_H
#define EDGA_SARIF_READER_H

#include <string>
#include <vector>

namespace edga {

struct Diagnostic {
  std::string level;  // "error", "warning", "note", ...
  std::string code;   // the front end's rule id
  std::string message;
  std::string file;  // as the front end reports it (a file URI or path)
  long line = 0;
  long column = 0;
};

// The results of a SARIF log; empty when the file is missing or unreadable.
std::vector<Diagnostic> readSarifDiagnostics(const std::string& path);

}  // namespace edga

#endif
