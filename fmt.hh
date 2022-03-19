#define SYSTEM "dummy"
#define HAVE_BOEHMGC 1

#include <iostream>

#include "nix/store-api.hh"
#include "nix/eval.hh"
#include "nix/eval-inline.hh"

std::ostream& printStringValue(std::ostream&, const char*);
std::ostream& printValue(std::ostream&,
                           nix::EvalState&,
                           nix::Value& v,
                           unsigned int,
                           std::set<nix::Value*> &);
