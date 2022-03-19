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

std::ostream& printValue(std::ostream& str,
                         EvalState& state,
                         Value& v,
                         unsigned int maxDepth) {
  ValuesSeen seen;
  return printValue(str, state, v, maxDepth, seen);
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
                         EvalState& state,
                         Value& v,
                         unsigned int maxDepth,
                         ValuesSeen& seen) {
  switch (v.type()) {
    case nInt:
      str << v.integer;
      break;
    case nBool:
      str << (v.boolean ? "true" : "false");
      break;
    case nString:
      printStringValue(str, v.string.s);
      break;
    case nPath:
      str << v.path;  // !!! escaping?
      break;
    case nNull:
      str << "null";
      break;
    case nAttrs: {
      seen.insert(&v);
      bool isDrv = state.isDerivation(v);
      if (isDrv) {
        str << "«derivation ";
        Bindings::iterator i = v.attrs->find(state.sDrvPath);
        PathSet context;
        if (i != v.attrs->end())
          str << state.store->printStorePath(
              state.coerceToStorePath(*i->pos, *i->value, context));
        else
          str << "???";
        str << "»";
      }
      else if (maxDepth > 0) {
        str << "{ ";
        typedef std::map<std::string, Value*> Sorted;
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
            } catch (AssertionError& e) {
              str << "«error: " << e.msg() << "»";
            }
          str << "; ";
        }
        str << "}";
      } else
        str << "{ ... }";
      break;
    }
    case nList:
      seen.insert(&v);
      str << "[ ";
      if (maxDepth > 0)
        for (auto elem : v.listItems()) {
          if (seen.count(elem))
            str << "«repeated»";
          else
            try {
              printValue(str, state, *elem, maxDepth - 1, seen);
            } catch (AssertionError& e) {
              str << "«error: " << e.msg() << "»";
            }
          str << " ";
        }
      else
        str << "... ";
      str << "]";
      break;
    case nFunction:
      if (v.isLambda()) {
        std::ostringstream s;
        s << v.lambda.fun->pos;
        str << "«lambda @ " << filterANSIEscapes(s.str()) << "»";
      } else if (v.isPrimOp()) {
        str << "«primop»";
      } else if (v.isPrimOpApp()) {
        str << "«primop-app»";
      } else {
        abort();
      }
      break;
    case nFloat:
      str << v.fpoint;
      break;
    default:
      str << "«unknown»";
      break;
  }
  return str;
}
