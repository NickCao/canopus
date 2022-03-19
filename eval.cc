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
  std::list<std::string> search = {};
  auto state = std::allocate_shared<EvalState>(
      traceable_allocator<EvalState>(), search, openStore("file:///tmp/eval"),
      openStore("file:///tmp/eval"));
  auto env = &state->allocEnv(32768);
  Value v;
  Expr* e = state->parseExprFromString("with import <nixpkgs> {};hello",
                                       absPath("."));
  state->eval(e, v);
  state->forceValue(v, [&]() { return v.determinePos(noPos); });
  printValue(std::cout, *state, v, 1) << std::endl;
}

