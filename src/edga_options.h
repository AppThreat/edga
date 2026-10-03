// The exporter's own command-line options, taken out of the front end's arguments by the main
// program and read by the back end.
#ifndef EDGA_OPTIONS_H
#define EDGA_OPTIONS_H

#include <string>
#include <vector>

#include "sarif_reader.h"

namespace edga {

struct Options {
  // Where the JSON goes; "-" is standard output.
  std::string out = "-";
  // The project directory: bodies are exported only for files under it (all files when empty).
  std::string root;
  // Export the bodies of functions defined in headers outside the project as well.
  bool headers = false;
};

Options& options();

// Set by the back end once it has written the translation unit.
extern bool backEndRan;

// Completes the document the back end wrote, with the front end's diagnostics.
void finishUnit(const std::vector<Diagnostic>& diagnostics);

// Writes the document for a translation unit the front end gave up on before the back end ran.
void writeFailedUnit(const char* sourceFile, const std::vector<Diagnostic>& diagnostics);

}  // namespace edga

#endif
