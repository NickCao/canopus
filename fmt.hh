#define SYSTEM "dummy"
#define HAVE_BOEHMGC 1

#include <cstdlib>
#include <cstring>
#include <iostream>

#include "eval-inline.hh"
#include "eval.hh"
#include "store-api.hh"

using namespace nix;
typedef std::set<Value*> ValuesSeen;

std::ostream& printStringValue(std::ostream& str, const char* string);
std::ostream& printValue(std::ostream& str,
                           EvalState& state,
                           Value& v,
                           unsigned int maxDepth,
                           ValuesSeen& seen);
std::ostream& printValue(std::ostream& str,
                           EvalState& state,
                           Value& v,
                           unsigned int maxDepth);

