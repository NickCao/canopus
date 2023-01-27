#define SYSTEM "dummy"
#define HAVE_BOEHMGC 1

#include <cassert>
#include <linux/landlock.h>
#include <seccomp.h>
#include <sys/prctl.h>
#include <sys/syscall.h>
#include <iostream>
#include "nix/callback.hh"
#include "nix/eval-inline.hh"
#include "nix/store-api.hh"
#include "nix/shared.hh"

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
    state->eval(state->parseExprFromString(expr, "/var/empty"), value);
    state->forceValue(value, nix::noPos);
    if (state->isDerivation(value)) {
      auto i = value.attrs->find(state->sDrvPath);
      nix::PathSet context;
      std::ostringstream ss;
      ss << "<derivation ";
      if (i != value.attrs->end()) {
        ss << state->store->printStorePath(
            state->coerceToStorePath(i->pos, *i->value, context));
      } else {
        ss << "???";
      }
      ss << ">";
      return ss.str();
    } else {
      return nix::printValue(*state, value);
    }
  }

  static void init() {
    nix::initNix();
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
  int ruleset_fd =
      landlock_create_ruleset(&ruleset_attr, sizeof(ruleset_attr), 0);
  if (ruleset_fd < 0)
    throw std::runtime_error("failed to create landlock ruleset");

  for (auto path : {"/proc/self", nixpkgs.c_str()}) {
    struct landlock_path_beneath_attr path_beneath = {
        .allowed_access =
            LANDLOCK_ACCESS_FS_READ_FILE | LANDLOCK_ACCESS_FS_READ_DIR,
        .parent_fd = open(path, O_PATH | O_CLOEXEC),
    };
    if (path_beneath.parent_fd < 0)
      throw std::runtime_error("failed to open path to nixpkgs");
    if (landlock_add_rule(ruleset_fd, LANDLOCK_RULE_PATH_BENEATH, &path_beneath,
                          0))
      throw std::runtime_error("failed to add landlock rule");
    close(path_beneath.parent_fd);
  };

  if (prctl(PR_SET_NO_NEW_PRIVS, 1, 0, 0, 0))
    throw std::runtime_error("failed to set no new privs");
  if (landlock_restrict_self(ruleset_fd, 0))
    throw std::runtime_error("failed to restrict self");
  close(ruleset_fd);

  scmp_filter_ctx ctx = seccomp_init(SCMP_ACT_KILL_PROCESS);
  if (ctx == NULL)
    throw std::runtime_error("failed to init seccomp context");
  for (auto nr : {
           SCMP_SYS(read),
           SCMP_SYS(write),
           SCMP_SYS(close),
           SCMP_SYS(openat),
           SCMP_SYS(newfstatat),
           SCMP_SYS(fgetxattr),
           SCMP_SYS(brk),
           SCMP_SYS(mmap),
           SCMP_SYS(munmap),
           SCMP_SYS(futex),
           SCMP_SYS(getegid32),
           SCMP_SYS(exit_group),
           SCMP_SYS(prlimit64),
           SCMP_SYS(gettid),
           SCMP_SYS(getpid),
           SCMP_SYS(sched_getaffinity),
           SCMP_SYS(tgkill),
           SCMP_SYS(rt_sigprocmask),
       })
    if (seccomp_rule_add(ctx, SCMP_ACT_ALLOW, nr, 0) < 0)
      throw std::runtime_error("failed to add seccomp rule");
  if (seccomp_load(ctx) < 0)
    throw std::runtime_error("failed to load seccomp rule");
  seccomp_release(ctx);

  auto evaluator = Evaluator({"nixpkgs=" + nixpkgs});
  try {
    std::cout << evaluator.eval(expr);
  } catch (const std::exception& e) {
    std::cout << nix::filterANSIEscapes(e.what(), true);
  }
}
