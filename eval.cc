#include "fmt.hh"

#include <cstdlib>
#include <cstring>
#include <iostream>

#include "nix/eval-inline.hh"
#include "nix/eval.hh"
#include "nix/store-api.hh"

int main(int argc, char *argv[]) {
  nix::initGC();

  // evalSettings.pureEval = true;
  // evalSettings.nixPath = {};
  nix::evalSettings.restrictEval = true;
  nix::evalSettings.enableImportFromDerivation = false;
  nix::evalSettings.useEvalCache = false;

  std::list<std::string> search = {};
  auto state = std::allocate_shared<nix::EvalState>(traceable_allocator<nix::EvalState>(), search, nix::openStore("file:///tmp/eval"));
  nix::Expr* expr = state->parseExprFromString(argv[1], "/var/empty");
  nix::Value value;
  state->eval(expr, value);
  std::set<nix::Value*> seen;
  printValue(std::cout, *state, value, 1, seen) << std::endl;
}
