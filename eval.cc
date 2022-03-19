#include "fmt.hh"

#include <cstdlib>
#include <cstring>
#include <iostream>

#include "eval-inline.hh"
#include "eval.hh"
#include "store-api.hh"

using namespace nix;
int main() {
  initGC();

  // evalSettings.pureEval = true;
  // evalSettings.nixPath = {};
  evalSettings.restrictEval = true;
  evalSettings.enableImportFromDerivation = false;
  evalSettings.useEvalCache = false;

  std::list<std::string> search = {};
  auto state = std::allocate_shared<EvalState>(traceable_allocator<EvalState>(), search, openStore("file:///tmp/eval"));
  Expr* expr = state->parseExprFromString("with import <nixpkgs> {}; hello", absPath("."));
  Value value;
  state->eval(expr, value);
  state->forceValue(value, [&]() { return value.determinePos(noPos); });
  printValue(std::cout, *state, value, 1) << std::endl;
}
