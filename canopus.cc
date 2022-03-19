#define SYSTEM "dummy"
#define HAVE_BOEHMGC 1

#include <linux/landlock.h>
#include <sys/prctl.h>
#include <sys/syscall.h>
#include <iostream>
#include "nix/eval-inline.hh"
#include "nix/store-api.hh"
#include "nix/callback.hh"

struct DummyStoreConfig : virtual nix::StoreConfig {
  using nix::StoreConfig::StoreConfig;
  const std::string name() override { return "Dummy Store"; }
};

struct DummyStore : public virtual DummyStoreConfig, public virtual nix::Store {
  DummyStore(const std::string scheme,
             const std::string uri,
             const nix::Store::Params& params)
      : DummyStore(params) {}

  DummyStore(const nix::Store::Params& params)
      : nix::StoreConfig(params),
        DummyStoreConfig(params),
        nix::Store(params) {}
  nix::StorePath addToStoreFromDump(
      nix::Source& dump,
      std::string_view name,
      nix::FileIngestionMethod method,
      nix::HashType hashAlgo,
      nix::RepairFlag repair,
      const nix::StorePathSet& references) override {
    auto hashSink = std::make_unique<nix::HashSink>(hashAlgo);
    return nix::StorePath("aaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaa-invalid");
  }
  nix::StorePath addTextToStore(std::string_view name,
                                std::string_view s,
                                const nix::StorePathSet& references,
                                nix::RepairFlag repair) override {
    auto hash = nix::hashString(nix::htSHA256, s);
    return makeTextPath(name, hash, references);
  }

  std::string getUri() override { return *uriSchemes().begin(); }

  void queryPathInfoUncached(
      const nix::StorePath& path,
      nix::Callback<std::shared_ptr<const nix::ValidPathInfo>>
          callback) noexcept override {
    callback(nullptr);
  }

  static std::set<std::string> uriSchemes() { return {"dummy"}; }

  std::optional<nix::StorePath> queryPathFromHashPart(
      const std::string& hashPart) override {
    unsupported("queryPathFromHashPart");
  }

  void addToStore(const nix::ValidPathInfo& info,
                  nix::Source& source,
                  nix::RepairFlag repair,
                  nix::CheckSigsFlag checkSigs) override {
    unsupported("addToStore");
  }

  void narFromPath(const nix::StorePath& path, nix::Sink& sink) override {
    unsupported("narFromPath");
  }

  void queryRealisationUncached(
      const nix::DrvOutput&,
      nix::Callback<std::shared_ptr<const nix::Realisation>> callback) noexcept
      override {
    callback(nullptr);
  }
};

struct Evaluator {
  std::shared_ptr<nix::EvalState> state;

  Evaluator(std::list<std::string> search) {
    nix::Store::Params params;
    auto dummy = std::make_shared<DummyStore>(params);
    dummy->init();
    state = std::allocate_shared<nix::EvalState>(
        traceable_allocator<nix::EvalState>(), search,
        nix::ref<nix::Store>(dummy));
  }

  std::string eval(std::string expr) {
    nix::Value value;
    std::set<nix::Value*> seen;
    std::ostringstream out;
    state->eval(state->parseExprFromString(expr, "/var/empty"), value);
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
    nix::evalSettings.nixPath = {};
    nix::evalSettings.restrictEval = true;
    nix::evalSettings.enableImportFromDerivation = false;
    nix::evalSettings.useEvalCache = false;
  }
};

static inline int landlock_create_ruleset(
    const struct landlock_ruleset_attr* const attr,
    const size_t size,
    const __u32 flags) {
  return syscall(__NR_landlock_create_ruleset, attr, size, flags);
}
static inline int landlock_add_rule(const int ruleset_fd,
                                    const enum landlock_rule_type rule_type,
                                    const void* const rule_attr,
                                    const __u32 flags) {
  return syscall(__NR_landlock_add_rule, ruleset_fd, rule_type, rule_attr,
                 flags);
}
static inline int landlock_restrict_self(const int ruleset_fd,
                                         const __u32 flags) {
  return syscall(__NR_landlock_restrict_self, ruleset_fd, flags);
}

int main(int argc, char* argv[]) {
  if (argc != 3) {
    std::cout << "usage: canopus [path to nixpkgs] [expression]" << std::endl;
    exit(1);
  }

  auto nixpkgs = nix::absPath(argv[1]);
  auto expr = argv[2];
  Evaluator::init();

  struct landlock_ruleset_attr ruleset_attr = {
      .handled_access_fs =
          LANDLOCK_ACCESS_FS_EXECUTE | LANDLOCK_ACCESS_FS_WRITE_FILE |
          LANDLOCK_ACCESS_FS_READ_FILE | LANDLOCK_ACCESS_FS_READ_DIR |
          LANDLOCK_ACCESS_FS_REMOVE_DIR | LANDLOCK_ACCESS_FS_REMOVE_FILE |
          LANDLOCK_ACCESS_FS_MAKE_CHAR | LANDLOCK_ACCESS_FS_MAKE_DIR |
          LANDLOCK_ACCESS_FS_MAKE_REG | LANDLOCK_ACCESS_FS_MAKE_SOCK |
          LANDLOCK_ACCESS_FS_MAKE_FIFO | LANDLOCK_ACCESS_FS_MAKE_BLOCK |
          LANDLOCK_ACCESS_FS_MAKE_SYM,
  };
  int ruleset_fd = landlock_create_ruleset(&ruleset_attr, sizeof(ruleset_attr), 0);
  if (ruleset_fd < 0)
    throw std::runtime_error("failed to create landlock ruleset");

  struct landlock_path_beneath_attr path_beneath = {
    .allowed_access = LANDLOCK_ACCESS_FS_READ_FILE |
                      LANDLOCK_ACCESS_FS_READ_DIR,
    .parent_fd = open(nixpkgs.c_str(), O_PATH | O_CLOEXEC),
  };
  if (path_beneath.parent_fd < 0)
    throw std::runtime_error("failed to open path to nixpkgs");
  if (landlock_add_rule(ruleset_fd, LANDLOCK_RULE_PATH_BENEATH, &path_beneath, 0))
    throw std::runtime_error("failed to add landlock rule");
  close(path_beneath.parent_fd);

  if (prctl(PR_SET_NO_NEW_PRIVS, 1, 0, 0, 0))
    throw std::runtime_error("failed to set no new privs");
  if (landlock_restrict_self(ruleset_fd, 0))
    throw std::runtime_error("failed to restrict self");
  close(ruleset_fd);

  auto evaluator = Evaluator({"nixpkgs="+nixpkgs});
  std::cout << evaluator.eval(expr);
}
