//===- qasm3-import.cpp - OpenQASM 2/3 → FTQC MLIR importer ──────────────===//
//
// Standalone tool.  All circuit parsing and MLIR emission lives in
// QASM3Importer.h; this file is the CLI entry point only.
//
// Usage:
//   qasm3-import [--ecc=<kind>] [--distance=<N>] [--func-name=<name>]
//               <input.qasm | ->
//
// Options:
//   --ecc=<kind>       steane (default), surface, color_code
//   --distance=<N>     code distance (default: 3)
//   --func-name=<name> MLIR function name (default: stem of input filename)
//
// Example:
//   qasm3-import bell.qasm | ftqc-opt --ftqc-insert-syndrome
//   qasm3-import --ecc=surface --distance=5 qft.qasm
//
//===----------------------------------------------------------------------===//

#include "QASM3Importer.h"

#include <fstream>
#include <iostream>
#include <sstream>
#include <string>

int main(int argc, char **argv) {
  std::string ecc = "steane", fn, file;
  int dist = 3;

  for (int i = 1; i < argc; ++i) {
    std::string a = argv[i];
    if (a == "--help" || a == "-h") {
      std::cerr <<
        "Usage: qasm3-import [--ecc=<steane|surface|color_code>]\n"
        "                    [--distance=<N>] [--func-name=<name>]\n"
        "                    <input.qasm | ->\n\n"
        "Converts OpenQASM 2/3 to FTQC MLIR text (stdout).\n\n"
        "Example:\n"
        "  qasm3-import bell.qasm | ftqc-opt --ftqc-insert-syndrome\n"
        "  qasm3-import --ecc=surface --distance=5 circuit.qasm\n";
      return 0;
    } else if (a.substr(0, 6)  == "--ecc=")       { ecc  = a.substr(6); }
    else if   (a.substr(0, 11) == "--distance=")  { dist = std::stoi(a.substr(11)); }
    else if   (a.substr(0, 12) == "--func-name=") { fn   = a.substr(12); }
    else if   (a[0] != '-')                       { file = a; }
    else { std::cerr << "Unknown option: " << a << "\n"; return 1; }
  }

  if (ecc != "steane" && ecc != "surface" && ecc != "color_code") {
    std::cerr << "Error: --ecc must be steane, surface, or color_code\n";
    return 1;
  }
  if (fn.empty())
    fn = file.empty() ? "circuit" : qasm3import::stemOf(file);

  std::string src;
  if (file.empty() || file == "-") {
    std::ostringstream ss; ss << std::cin.rdbuf(); src = ss.str();
  } else {
    std::ifstream f(file);
    if (!f) { std::cerr << "Error: cannot open '" << file << "'\n"; return 1; }
    std::ostringstream ss; ss << f.rdbuf(); src = ss.str();
  }

  std::cout << qasm3import::importQASM3(src, ecc, dist, fn);
  return 0;
}
