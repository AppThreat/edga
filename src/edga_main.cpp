// The exporter's main program: takes out its own options and runs the front end with the rest.
// The front end calls the back end (edga_back_end.cpp) once the translation unit is parsed; the
// diagnostics it wrote are added to the document afterwards.

#include <fcntl.h>
#include <unistd.h>

#include <cctype>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <string>
#include <vector>

#include "edga_options.h"
#include "edga_version.h"
#include "sarif_reader.h"

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
  bool userDiagnostics = false, userErrorLimit = false, userPruning = false,
       userInstantiation = false, cppDialect = false, dialect = false;
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
      // the dialect options: C++ (`--c++17`, `--g++`) or C (`--c11`, `--gcc`)
      if (startsWith(arg, "--c++") || std::strcmp(arg, "-p") == 0 || std::strcmp(arg, "--g++") == 0) {
        cppDialect = true;
        dialect = true;
      } else if (std::strcmp(arg, "--c") == 0 || (startsWith(arg, "--c") && std::isdigit(arg[3])) ||
                 std::strcmp(arg, "--gcc") == 0) {
        dialect = true;
      }
      if (startsWith(arg, "--instantiate") || std::strcmp(arg, "-t") == 0 ||
          startsWith(arg, "--auto_instantiation") || startsWith(arg, "--no_auto_instantiation") ||
          std::strcmp(arg, "-T") == 0)
        userInstantiation = true;
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

  // Defaults for exporting: keep the bodies of functions nothing calls, give every template the
  // unit uses its instances' bodies (here, without the prelinker's files), keep going past many errors (the valid parts of the file are
  // still exported), and diagnostics as SARIF for the document. The user's own options come
  // after, so they win.
  std::vector<std::string> args;
  args.push_back(argv[0]);
  if (!userPruning) args.push_back("--no_remove_unneeded_entities");
  // without dialect options, the source file's name says which language it is
  bool cpp = cppDialect;
  if (!dialect && sourceFile != nullptr) {
    std::string name = sourceFile;
    for (const char* suffix : {".cpp", ".cc", ".cxx", ".c++", ".C", ".hpp", ".hh", ".hxx", ".ipp"})
      if (name.size() > std::strlen(suffix) &&
          name.compare(name.size() - std::strlen(suffix), std::string::npos, suffix) == 0)
        cpp = true;
  }
  if (cpp && !userInstantiation) {
    args.push_back("--instantiate");
    args.push_back("used");
    // in this translation unit, without the prelinker's template information file
    args.push_back("--no_auto_instantiation");
  }
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

  // Errors in the command line are reported before the SARIF log is open: as SARIF results on the
  // standard error, which is captured to give the document them too.
  std::string stderrFile = sarifFile.empty() ? std::string() : temporaryFile();
  int savedStderr = -1;
  if (!stderrFile.empty()) {
    std::fflush(stderr);
    int captured = open(stderrFile.c_str(), O_WRONLY | O_TRUNC);
    if (captured >= 0) {
      savedStderr = dup(STDERR_FILENO);
      dup2(captured, STDERR_FILENO);
      close(captured);
    }
  }

  int status = edg::edg_main(static_cast<int>(frontEndArgs.size() - 1), frontEndArgs.data());

  std::string earlyErrors;
  if (savedStderr >= 0) {
    std::fflush(stderr);
    dup2(savedStderr, STDERR_FILENO);
    close(savedStderr);
    if (std::FILE* f = std::fopen(stderrFile.c_str(), "rb")) {
      char buf[65536];
      for (size_t n; (n = std::fread(buf, 1, sizeof buf, f)) > 0;) earlyErrors.append(buf, n);
      std::fclose(f);
    }
  }
  if (!stderrFile.empty()) std::remove(stderrFile.c_str());

  std::vector<edga::Diagnostic> diagnostics = edga::readSarifResults(earlyErrors);
  // anything else the front end wrote there is passed on
  if (diagnostics.empty() && !earlyErrors.empty()) std::fputs(earlyErrors.c_str(), stderr);
  if (!sarifFile.empty()) {
    std::vector<edga::Diagnostic> logged = edga::readSarifDiagnostics(sarifFile);
    diagnostics.insert(diagnostics.end(), logged.begin(), logged.end());
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
