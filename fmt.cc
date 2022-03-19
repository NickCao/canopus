#include "fmt.hh"

static bool isVarName(std::string_view s) {
  if (s.size() == 0)
    return false;
  char c = s[0];
  if ((c >= '0' && c <= '9') || c == '-' || c == '\'')
    return false;
  for (auto& i : s)
    if (!((i >= 'a' && i <= 'z') || (i >= 'A' && i <= 'Z') ||
          (i >= '0' && i <= '9') || i == '_' || i == '-' || i == '\''))
      return false;
  return true;
}

std::ostream& printStringValue(std::ostream& str, const char* string) {
  str << "\"";
  for (const char* i = string; *i; i++)
    if (*i == '\"' || *i == '\\')
      str << "\\" << *i;
    else if (*i == '\n')
      str << "\\n";
    else if (*i == '\r')
      str << "\\r";
    else if (*i == '\t')
      str << "\\t";
    else
      str << *i;
  str << "\"";
  return str;
}

std::ostream& printValue(std::ostream& str,
                         nix::EvalState& state,
                         nix::Value& v,
                         unsigned int maxDepth,
                         std::set<nix::Value*>& seen) {
  state.forceValue(v, [&]() { return v.determinePos(nix::noPos); });
  switch (v.type()) {
    case nix::nInt:
      str << v.integer;
      break;
    case nix::nBool:
      str << (v.boolean ? "true" : "false");
      break;
    case nix::nString:
      printStringValue(str, v.string.s);
      break;
    case nix::nPath:
      str << v.path;  // !!! escaping?
      break;
    case nix::nNull:
      str << "null";
      break;
    case nix::nAttrs: {
      seen.insert(&v);
      bool isDrv = state.isDerivation(v);
      if (isDrv) {
        str << "«derivation ";
        nix::Bindings::iterator i = v.attrs->find(state.sDrvPath);
        nix::PathSet context;
        if (i != v.attrs->end())
          str << state.store->printStorePath(
              state.coerceToStorePath(*i->pos, *i->value, context));
        else
          str << "???";
        str << "»";
      }
      else if (maxDepth > 0) {
        str << "{ ";
        typedef std::map<std::string, nix::Value*> Sorted;
        Sorted sorted;
        for (auto& i : *v.attrs)
          sorted[i.name] = i.value;
        for (auto& i : sorted) {
          if (isVarName(i.first))
            str << i.first;
          else
            printStringValue(str, i.first.c_str());
          str << " = ";
          if (seen.count(i.second))
            str << "«repeated»";
          else
            try {
              printValue(str, state, *i.second, maxDepth - 1, seen);
            } catch (nix::AssertionError& e) {
              str << "«error: " << e.msg() << "»";
            }
          str << "; ";
        }
        str << "}";
      } else
        str << "{ ... }";
      break;
    }
    case nix::nList:
      seen.insert(&v);
      str << "[ ";
      if (maxDepth > 0)
        for (auto elem : v.listItems()) {
          if (seen.count(elem))
            str << "«repeated»";
          else
            try {
              printValue(str, state, *elem, maxDepth - 1, seen);
            } catch (nix::AssertionError& e) {
              str << "«error: " << e.msg() << "»";
            }
          str << " ";
        }
      else
        str << "... ";
      str << "]";
      break;
    case nix::nFunction:
      if (v.isLambda()) {
        std::ostringstream s;
        s << v.lambda.fun->pos;
        str << "«lambda @ " << nix::filterANSIEscapes(s.str()) << "»";
      } else if (v.isPrimOp()) {
        str << "«primop»";
      } else if (v.isPrimOpApp()) {
        str << "«primop-app»";
      } else {
        abort();
      }
      break;
    case nix::nFloat:
      str << v.fpoint;
      break;
    default:
      str << "«unknown»";
      break;
  }
  return str;
}
