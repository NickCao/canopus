#define SYSTEM "dummy"
#define HAVE_BOEHMGC 1

#include <iostream>
#include "nix/eval-inline.hh"
#include "nix/store-api.hh"

struct Evaluator {
  std::shared_ptr<nix::EvalState> state;

  Evaluator(std::list<std::string> search, std::string store) {
    state = std::allocate_shared<nix::EvalState>(
        traceable_allocator<nix::EvalState>(), search, nix::openStore(store));
  }

  std::string eval(std::string expr, nix::Path path) {
    nix::Value value;
    std::set<nix::Value*> seen;
    std::ostringstream out;
    state->eval(state->parseExprFromString(expr, path), value);
    format_value(out, value, 1, seen);
    return out.str();
  }

  static bool check_varname(std::string_view str) {
    if (str.size() == 0)
      return false;
    char chr = str[0];
    if ((chr >= '0' && chr <= '9') || chr == '-' || chr == '\'')
      return false;
    for (auto& i : str)
      if (!((i >= 'a' && i <= 'z') || (i >= 'A' && i <= 'Z') ||
            (i >= '0' && i <= '9') || i == '_' || i == '-' || i == '\''))
        return false;
    return true;
  }

  static std::ostream& format_string(std::ostream& out, const char* str) {
    out << "\"";
    for (const char* i = str; *i; i++)
      if (*i == '\"' || *i == '\\')
        out << "\\" << *i;
      else if (*i == '\n')
        out << "\\n";
      else if (*i == '\r')
        out << "\\r";
      else if (*i == '\t')
        out << "\\t";
      else
        out << *i;
    out << "\"";
    return out;
  }

  std::ostream& format_value(std::ostream& out,
                             nix::Value& value,
                             uint64_t depth,
                             std::set<nix::Value*>& seen) {
    state->forceValue(value, [&]() { return value.determinePos(nix::noPos); });
    switch (value.type()) {
      case nix::nInt:
        out << value.integer;
        break;
      case nix::nBool:
        out << (value.boolean ? "true" : "false");
        break;
      case nix::nString:
        format_string(out, value.string.s);
        break;
      case nix::nPath:
        out << value.path;
        break;
      case nix::nNull:
        out << "null";
        break;
      case nix::nAttrs: {
        seen.insert(&value);
        bool isDrv = state->isDerivation(value);
        if (isDrv) {
          out << "«derivation ";
          nix::Bindings::iterator i = value.attrs->find(state->sDrvPath);
          nix::PathSet context;
          if (i != value.attrs->end())
            out << state->store->printStorePath(
                state->coerceToStorePath(*i->pos, *i->value, context));
          else
            out << "???";
          out << "»";
        } else if (depth > 0) {
          out << "{ ";
          typedef std::map<std::string, nix::Value*> Sorted;
          Sorted sorted;
          for (auto& i : *value.attrs)
            sorted[i.name] = i.value;
          for (auto& i : sorted) {
            if (check_varname(i.first))
              out << i.first;
            else
              format_string(out, i.first.c_str());
            out << " = ";
            if (seen.count(i.second))
              out << "«repeated»";
            else
              try {
                format_value(out, *i.second, depth - 1, seen);
              } catch (nix::AssertionError& e) {
                out << "«error: " << e.msg() << "»";
              }
            out << "; ";
          }
          out << "}";
        } else
          out << "{ ... }";
        break;
      }
      case nix::nList:
        seen.insert(&value);
        out << "[ ";
        if (depth > 0)
          for (auto elem : value.listItems()) {
            if (seen.count(elem))
              out << "«repeated»";
            else
              try {
                format_value(out, *elem, depth - 1, seen);
              } catch (nix::AssertionError& e) {
                out << "«error: " << e.msg() << "»";
              }
            out << " ";
          }
        else
          out << "... ";
        out << "]";
        break;
      case nix::nFunction:
        if (value.isLambda()) {
          std::ostringstream s;
          s << value.lambda.fun->pos;
          out << "«lambda @ " << nix::filterANSIEscapes(s.str()) << "»";
        } else if (value.isPrimOp()) {
          out << "«primop»";
        } else if (value.isPrimOpApp()) {
          out << "«primop-app»";
        } else {
          throw std::invalid_argument("unrecognized function");
        }
        break;
      case nix::nFloat:
        out << value.fpoint;
        break;
      default:
        out << "«unknown»";
        break;
    }
    return out;
  }

  static void init() {
    nix::initGC();

    // evalSettings.pureEval = true;
    // evalSettings.nixPath = {};
    nix::evalSettings.restrictEval = true;
    nix::evalSettings.enableImportFromDerivation = false;
    nix::evalSettings.useEvalCache = false;
  }
};

int main(int argc, char* argv[]) {
  Evaluator::init();
  auto evaluator = Evaluator({}, "file:///tmp/eval");
  std::cout << evaluator.eval("(import <nixpkgs> {}).firefoxPackages",
                              "/var/empty");
}
