// The exporter's main program: takes out its own options and runs the front end with the rest.
// The front end calls the back end (edga_back_end.cpp) once the translation unit is parsed; the
// diagnostics it wrote are added to the document afterwards.

#include <unistd.h>

#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <string>
#include <vector>

#include "edga_options.h"
#include "edga_version.h"

namespace edg {
int edg_main(int argc, char* argv[]);
}

namespace edga {

Options& options() {
  static Options instance;
  return instance;
}

bool backEndRan = false;

}  // namespace edga

namespace {

void printVersion() {
  std::printf("edga %s\nEDG commit %s\nconfiguration %s %s\n", EDGA_VERSION, EDGA_EDG_COMMIT,
              EDGA_CONFIG_NAME, EDGA_CONFIG_FINGERPRINT);
}

void printUsage() {
  std::printf(
      "Usage: edga [front end options...] [--edga-out <file.json>] [--edga-root <dir>]\n"
      "            [--edga-headers] <source file>\n"
      "\n"
      "  --edga-out <file>   write the translation unit as JSON to <file> (default: stdout)\n"
      "  --edga-root <dir>   export function bodies only for files under <dir>\n"
      "  --edga-headers      also export bodies defined in headers outside <dir>\n"
      "  --edga-version      print the exporter's version, the EDG commit and configuration\n"
      "\n"
      "Every other option goes to the EDG front end.\n");
}

bool startsWith(const char* s, const char* prefix) {
  return std::strncmp(s, prefix, std::strlen(prefix)) == 0;
}

std::string temporaryFile() {
  const char* dir = std::getenv("TMPDIR");
  std::string pattern = std::string(dir != nullptr && *dir ? dir : "/tmp") + "/edga-XXXXXX";
  std::vector<char> name(pattern.begin(), pattern.end());
  name.push_back('\0');
  int fd = mkstemp(name.data());
  if (fd < 0) return "";
  close(fd);
  return std::string(name.data());
}

}  // namespace

int main(int argc, char* argv[]) {
  if (argc == 2 && std::strcmp(argv[1], "--version") == 0) {
    printVersion();
    return 0;
  }
  std::vector<std::string> userArgs;
  const char* sourceFile = nullptr;
  bool userDiagnostics = false, userErrorLimit = false, userPruning = false;
  for (int i = 1; i < argc; ++i) {
    const char* arg = argv[i];
    if (std::strcmp(arg, "--edga-version") == 0) {
      printVersion();
      return 0;
    } else if (std::strcmp(arg, "--edga-help") == 0 || std::strcmp(arg, "--help") == 0) {
      printUsage();
      return 0;
    } else if (std::strcmp(arg, "--edga-headers") == 0) {
      edga::options().headers = true;
    } else if (std::strcmp(arg, "--edga-out") == 0 || std::strcmp(arg, "--edga-root") == 0) {
      if (i + 1 >= argc) {
        std::fprintf(stderr, "edga: %s needs a value\n", arg);
        return 2;
      }
      std::string value = argv[++i];
      if (std::strcmp(arg, "--edga-out") == 0) {
        edga::options().out = value;
      } else {
        edga::options().root = value;
      }
    } else {
      if (startsWith(arg, "--output_mode") || startsWith(arg, "--error_output"))
        userDiagnostics = true;
      if (startsWith(arg, "--error_limit") || std::strcmp(arg, "-e") == 0) userErrorLimit = true;
      if (startsWith(arg, "--remove_unneeded_entities") ||
          startsWith(arg, "--no_remove_unneeded_entities"))
        userPruning = true;
      userArgs.push_back(arg);
      // the source file: the last argument that is not an option and names a readable file
      if (arg[0] != '-') {
        if (std::FILE* f = std::fopen(arg, "rb")) {
          std::fclose(f);
          sourceFile = arg;
        }
      }
    }
  }

  // Defaults for exporting: keep the bodies of functions nothing calls, keep going past many
  // errors (the valid parts of the file are still exported), and diagnostics as SARIF for the
  // document. The user's own options come after, so they win.
  std::vector<std::string> args;
  args.push_back(argv[0]);
  if (!userPruning) args.push_back("--no_remove_unneeded_entities");
  if (!userErrorLimit) {
    args.push_back("--error_limit");
    args.push_back("1000000");
  }
  std::string sarifFile;
  if (!userDiagnostics) {
    sarifFile = temporaryFile();
    if (!sarifFile.empty()) {
      args.push_back("--output_mode");
      args.push_back("sarif");
      args.push_back("--error_output");
      args.push_back(sarifFile);
    }
  }
  args.insert(args.end(), userArgs.begin(), userArgs.end());
  std::vector<char*> frontEndArgs;
  for (std::string& a : args) frontEndArgs.push_back(&a[0]);
  frontEndArgs.push_back(nullptr);

  int status = edg::edg_main(static_cast<int>(frontEndArgs.size() - 1), frontEndArgs.data());

  std::vector<edga::Diagnostic> diagnostics;
  if (!sarifFile.empty()) {
    diagnostics = edga::readSarifDiagnostics(sarifFile);
    std::remove(sarifFile.c_str());
    for (const edga::Diagnostic& d : diagnostics) {
      if (d.level == "error")
        std::fprintf(stderr, "%s:%ld:%ld: error: %s\n", d.file.c_str(), d.line, d.column,
                     d.message.c_str());
    }
  }
  if (edga::backEndRan) {
    edga::finishUnit(diagnostics);
  } else if (sourceFile != nullptr) {
    edga::writeFailedUnit(sourceFile, diagnostics);
  }
  return status;
}
