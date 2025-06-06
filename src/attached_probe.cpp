#ifndef _GNU_SOURCE
#define _GNU_SOURCE
#endif

#include <algorithm>
#include <cstring>
#include <elf.h>
#include <fcntl.h>
#include <iostream>
#include <linux/hw_breakpoint.h>
#include <linux/limits.h>
#include <linux/perf_event.h>
#include <sys/ioctl.h>
#include <sys/syscall.h>
#include <sys/utsname.h>
#include <unistd.h>

#include <bcc/bcc_elf.h>
#include <bcc/bcc_syms.h>
#include <bcc/bcc_usdt.h>
#include <bpf/bpf.h>
#include <bpf/libbpf.h>

#include "attached_probe.h"
#include "bpftrace.h"
#include "disasm.h"
#include "log.h"
#include "probe_matcher.h"
#include "usdt.h"
#include "util/bpf_names.h"
#include "util/cpus.h"
#include "util/exceptions.h"
#include "util/kernel.h"
#include "util/symbols.h"

namespace bpftrace {

bpf_probe_attach_type attachtype(ProbeType t)
{
  // clang-format off
  switch (t)
  {
    case ProbeType::kprobe:    return BPF_PROBE_ENTRY;  break;
    case ProbeType::kretprobe: return BPF_PROBE_RETURN; break;
    case ProbeType::special:   return BPF_PROBE_ENTRY;  break;
    case ProbeType::uprobe:    return BPF_PROBE_ENTRY;  break;
    case ProbeType::uretprobe: return BPF_PROBE_RETURN; break;
    case ProbeType::usdt:      return BPF_PROBE_ENTRY;  break;
    default:
      LOG(BUG) << "invalid probe attachtype \"" << t << "\"";
  }
  // clang-format on
}

libbpf::bpf_prog_type progtype(ProbeType t)
{
  switch (t) {
      // clang-format off
    case ProbeType::special:    return libbpf::BPF_PROG_TYPE_RAW_TRACEPOINT; break;
    case ProbeType::kprobe:     return libbpf::BPF_PROG_TYPE_KPROBE; break;
    case ProbeType::kretprobe:  return libbpf::BPF_PROG_TYPE_KPROBE; break;
    case ProbeType::uprobe:     return libbpf::BPF_PROG_TYPE_KPROBE; break;
    case ProbeType::uretprobe:  return libbpf::BPF_PROG_TYPE_KPROBE; break;
    case ProbeType::usdt:       return libbpf::BPF_PROG_TYPE_KPROBE; break;
    case ProbeType::tracepoint: return libbpf::BPF_PROG_TYPE_TRACEPOINT; break;
    case ProbeType::profile:    return libbpf::BPF_PROG_TYPE_PERF_EVENT; break;
    case ProbeType::interval:   return libbpf::BPF_PROG_TYPE_PERF_EVENT; break;
    case ProbeType::software:   return libbpf::BPF_PROG_TYPE_PERF_EVENT; break;
    case ProbeType::watchpoint: return libbpf::BPF_PROG_TYPE_PERF_EVENT; break;
    case ProbeType::asyncwatchpoint: return libbpf::BPF_PROG_TYPE_PERF_EVENT; break;
    case ProbeType::hardware:   return libbpf::BPF_PROG_TYPE_PERF_EVENT; break;
    case ProbeType::fentry:     return libbpf::BPF_PROG_TYPE_TRACING; break;
    case ProbeType::fexit:      return libbpf::BPF_PROG_TYPE_TRACING; break;
    case ProbeType::iter:       return libbpf::BPF_PROG_TYPE_TRACING; break;
    case ProbeType::rawtracepoint: return libbpf::BPF_PROG_TYPE_TRACING; break;
    // clang-format on
    case ProbeType::invalid:
      LOG(BUG) << "program type invalid";
  }

  return {}; // unreached
}

std::string progtypeName(libbpf::bpf_prog_type t)
{
  switch (t) {
      // clang-format off
    case libbpf::BPF_PROG_TYPE_KPROBE:     return "BPF_PROG_TYPE_KPROBE";     break;
    case libbpf::BPF_PROG_TYPE_TRACEPOINT: return "BPF_PROG_TYPE_TRACEPOINT"; break;
    case libbpf::BPF_PROG_TYPE_PERF_EVENT: return "BPF_PROG_TYPE_PERF_EVENT"; break;
    case libbpf::BPF_PROG_TYPE_TRACING:    return "BPF_PROG_TYPE_TRACING";    break;
    // clang-format on
    default:
      LOG(BUG) << "invalid program type: " << t;
  }
}

void AttachedProbe::attach_fentry()
{
  if (progfd_ < 0)
    return;

  tracing_fd_ = bpf_raw_tracepoint_open(nullptr, progfd_);
  if (tracing_fd_ < 0) {
    throw util::FatalUserException("Error attaching probe: " + probe_.name);
  }
}

int AttachedProbe::detach_fentry()
{
  close(tracing_fd_);
  return 0;
}

void AttachedProbe::attach_iter(std::optional<int> pid)
{
  if (!pid.has_value()) {
    linkfd_ = bpf_link_create(progfd_,
                              0,
                              static_cast<enum ::bpf_attach_type>(
                                  libbpf::BPF_TRACE_ITER),
                              nullptr);
  } else {
    BPFTRACE_LIBBPF_OPTS(bpf_link_create_opts, opts);
    union bpf_iter_link_info linfo;
    memset(&linfo, 0, sizeof(linfo));
    linfo.task.pid = *pid;
    opts.iter_info = &linfo;
    opts.iter_info_len = sizeof(linfo);
    linkfd_ = bpf_link_create(progfd_,
                              0,
                              static_cast<enum ::bpf_attach_type>(
                                  libbpf::BPF_TRACE_ITER),
                              &opts);
  }

  if (linkfd_ < 0) {
    throw util::FatalUserException("Error attaching probe: " + probe_.name);
  }
}

int AttachedProbe::detach_iter()
{
  close(linkfd_);
  return 0;
}

void AttachedProbe::attach_raw_tracepoint()
{
  tracing_fd_ = bpf_raw_tracepoint_open(nullptr, progfd_);
  if (tracing_fd_ < 0) {
    if (tracing_fd_ == -ENOENT)
      throw util::FatalUserException("Probe does not exist: " + probe_.name);
    else if (tracing_fd_ == -EINVAL)
      throw util::FatalUserException(
          "Error attaching probe: " + probe_.name +
          ", maybe trying to access arguments beyond "
          "what's available in this tracepoint");
    else
      throw util::FatalUserException("Error attaching probe: " + probe_.name);
  }
}

int AttachedProbe::detach_raw_tracepoint()
{
  close(tracing_fd_);
  return 0;
}

AttachedProbe::AttachedProbe(Probe &probe,
                             const BpfProgram &prog,
                             std::optional<int> pid,
                             BPFtrace &bpftrace,
                             bool safe_mode)
    : probe_(probe), progfd_(prog.fd()), bpftrace_(bpftrace)
{
  LOG(V1) << "Attaching " << probe_.orig_name;
  switch (probe_.type) {
    case ProbeType::kprobe:
      attach_kprobe();
      break;
    case ProbeType::kretprobe:
      attach_kprobe();
      break;
    case ProbeType::tracepoint:
      attach_tracepoint();
      break;
    case ProbeType::profile:
      attach_profile(pid);
      break;
    case ProbeType::interval:
      attach_interval(pid);
      break;
    case ProbeType::software:
      attach_software(pid);
      break;
    case ProbeType::hardware:
      attach_hardware(pid);
      break;
    case ProbeType::fentry:
    case ProbeType::fexit:
      attach_fentry();
      break;
    case ProbeType::iter:
      attach_iter(pid);
      break;
    case ProbeType::rawtracepoint:
      attach_raw_tracepoint();
      break;
    case ProbeType::usdt:
      attach_usdt(pid, *bpftrace_.feature_);
      break;
    case ProbeType::watchpoint:
    case ProbeType::asyncwatchpoint:
      attach_watchpoint(pid, probe.mode);
      break;
    case ProbeType::uprobe:
    case ProbeType::uretprobe:
      attach_uprobe(pid, safe_mode);
      break;
    default:
      LOG(BUG) << "invalid attached probe type \"" << probe_.type << "\"";
  }
}

AttachedProbe::~AttachedProbe()
{
  int err = 0;
  for (int perf_event_fd : perf_event_fds_) {
    err = bpf_close_perf_event_fd(perf_event_fd);
    if (err)
      LOG(WARNING) << "failed to close perf event FDs for probe: "
                   << probe_.name;
  }

  err = 0;
  switch (probe_.type) {
    case ProbeType::kprobe:
    case ProbeType::kretprobe:
      if (probe_.funcs.empty())
        err = bpf_detach_kprobe(eventname().c_str());
      else
        close(linkfd_);
      break;
    case ProbeType::fentry:
    case ProbeType::fexit:
      err = detach_fentry();
      break;
    case ProbeType::iter:
      err = detach_iter();
      break;
    case ProbeType::uprobe:
    case ProbeType::uretprobe:
    case ProbeType::usdt:
      if (usdt_destructor_)
        usdt_destructor_();
      err = bpf_detach_uprobe(eventname().c_str());
      break;
    case ProbeType::tracepoint:
      err = bpf_detach_tracepoint(probe_.path.c_str(), eventname().c_str());
      break;
    case ProbeType::special:
    case ProbeType::profile:
    case ProbeType::interval:
    case ProbeType::software:
    case ProbeType::watchpoint:
    case ProbeType::asyncwatchpoint:
    case ProbeType::hardware:
      break;
    case ProbeType::rawtracepoint:
      err = detach_raw_tracepoint();
      break;
    case ProbeType::invalid:
      LOG(BUG) << "invalid attached probe type \"" << probe_.type
               << "\" at destructor";
  }

  if (err)
    LOG(WARNING) << "failed to detach probe: " << probe_.name;

  if (close_progfd_ && progfd_ >= 0)
    close(progfd_);
}

const Probe &AttachedProbe::probe() const
{
  return probe_;
}

int AttachedProbe::progfd() const
{
  return progfd_;
}

std::string AttachedProbe::eventprefix() const
{
  switch (attachtype(probe_.type)) {
    case BPF_PROBE_ENTRY:
      return "p_";
    case BPF_PROBE_RETURN:
      return "r_";
  }

  return {}; // unreached
}

std::string AttachedProbe::eventname() const
{
  std::ostringstream offset_str;
  std::string index_str = "_" + std::to_string(probe_.index);
  switch (probe_.type) {
    case ProbeType::kprobe:
    case ProbeType::kretprobe:
    case ProbeType::rawtracepoint:
      offset_str << std::hex << offset_;
      return eventprefix() +
             util::sanitise_bpf_program_name(probe_.attach_point) + "_" +
             offset_str.str() + index_str;
    case ProbeType::uprobe:
    case ProbeType::uretprobe:
    case ProbeType::usdt:
      offset_str << std::hex << offset_;
      return eventprefix() + util::sanitise_bpf_program_name(probe_.path) +
             "_" + offset_str.str() + index_str;
    case ProbeType::tracepoint:
      return probe_.attach_point;
    default:
      LOG(BUG) << "invalid eventname probe \"" << probe_.type << "\"";
  }
}

static uint64_t resolve_offset(const std::string &path,
                               const std::string &symbol,
                               uint64_t loc)
{
  bcc_symbol bcc_sym;

  if (bcc_resolve_symname(
          path.c_str(), symbol.c_str(), loc, 0, nullptr, &bcc_sym))
    throw util::FatalUserException("Could not resolve symbol: " + path + ":" +
                                   symbol);

  // Have to free sym.module, see:
  // https://github.com/iovisor/bcc/blob/ba73657cb8c4dab83dfb89eed4a8b3866255569a/src/cc/bcc_syms.h#L98-L99
  if (bcc_sym.module)
    ::free(const_cast<char *>(bcc_sym.module));

  return bcc_sym.offset;
}

static constexpr std::string_view hint_unsafe =
    "\nUse --unsafe to force attachment. WARNING: This option could lead to "
    "data corruption in the target process.";

static void check_alignment(std::string &path,
                            std::string &symbol,
                            uint64_t sym_offset,
                            uint64_t func_offset,
                            bool safe_mode,
                            ProbeType type)
{
  Disasm dasm(path);
  AlignState aligned = dasm.is_aligned(sym_offset, func_offset);

  std::string tmp = path + ":" + symbol + "+" + std::to_string(func_offset);

  switch (aligned) {
    case AlignState::Ok:
      return;
    case AlignState::NotAlign:
      if (safe_mode) {
        auto msg = "Could not add " + probetypeName(type) +
                   " into middle of instruction: " + tmp +
                   std::string{ hint_unsafe };
        throw util::FatalUserException(std::move(msg));
      } else {
        std::string_view hint;
        LOG(WARNING) << "Unsafe " << type
                     << " in the middle of the instruction: " << tmp << hint;
      }
      break;

    case AlignState::Fail:
      if (safe_mode)
        throw util::FatalUserException(
            "Failed to check if " + probetypeName(type) +
            " is in proper place: " + tmp + std::string{ hint_unsafe });
      else
        LOG(WARNING) << "Unchecked " << type << ": " << tmp;
      break;

    case AlignState::NotSupp:
      if (safe_mode)
        throw util::FatalUserException("Can't check if " + probetypeName(type) +
                                       " is in proper place (compiled without "
                                       "(k|u)probe offset support): " +
                                       tmp + std::string{ hint_unsafe });
      else
        LOG(WARNING) << "Unchecked " << type << " : " << tmp;
      break;
  }
}

bool AttachedProbe::resolve_offset_uprobe(bool safe_mode, bool has_multiple_aps)
{
  struct bcc_symbol_option option = {};
  struct symbol sym = {};
  std::string &symbol = probe_.attach_point;
  uint64_t func_offset = probe_.func_offset;

  sym.name = "";
  option.use_debug_file = 1;
  option.use_symbol_type = BCC_SYM_ALL_TYPES ^ (1 << STT_NOTYPE);

  if (symbol.empty()) {
    sym.address = probe_.address;
    bcc_elf_foreach_sym(
        probe_.path.c_str(), util::sym_address_cb, &option, &sym);

    if (!sym.start) {
      if (safe_mode) {
        std::stringstream ss;
        ss << "0x" << std::hex << probe_.address;
        throw util::FatalUserException(
            "Could not resolve address: " + probe_.path + ":" + ss.str());
      } else {
        LOG(WARNING) << "Could not determine instruction boundary for "
                     << probe_.name
                     << " (binary appears stripped). Misaligned probes "
                        "can lead to tracee crashes!";
        offset_ = probe_.address;
        return true;
      }
    }

    symbol = sym.name;
    func_offset = probe_.address - sym.start;
  } else {
    sym.name = symbol;
    bcc_elf_foreach_sym(probe_.path.c_str(), util::sym_name_cb, &option, &sym);

    if (!sym.start) {
      const std::string msg = "Could not resolve symbol: " + probe_.path + ":" +
                              symbol;
      const auto missing_probes = bpftrace_.config_->missing_probes;
      if (!has_multiple_aps || missing_probes == ConfigMissingProbes::error) {
        throw util::FatalUserException(msg + ", cannot attach probe.");
      } else {
        if (missing_probes == ConfigMissingProbes::warn)
          LOG(WARNING) << msg << ", skipping probe.";
        return false;
      }
    }
  }

  if (probe_.type == ProbeType::uretprobe && func_offset != 0) {
    throw util::FatalUserException("uretprobes cannot be attached at function "
                                   "offset. (address resolved to: " +
                                   symbol + "+" + std::to_string(func_offset) +
                                   ")");
  }

  if (sym.size == 0 && func_offset == 0) {
    if (safe_mode) {
      std::stringstream msg;
      msg << "Could not determine boundary for " << sym.name
          << " (symbol has size 0).";
      if (probe_.orig_name == probe_.name) {
        msg << hint_unsafe;
        throw util::FatalUserException(msg.str());
      } else {
        LOG(WARNING) << msg.str() << " Skipping attachment." << hint_unsafe;
      }
      return false;
    }
  } else if (func_offset >= sym.size) {
    throw util::FatalUserException("Offset outside the function bounds ('" +
                                   symbol + "' size is " +
                                   std::to_string(sym.size) + ")");
  }

  uint64_t sym_offset = resolve_offset(probe_.path,
                                       probe_.attach_point,
                                       probe_.loc);
  offset_ = sym_offset + func_offset;

  // If we are not aligned to the start of the symbol,
  // check if we are on the instruction boundary.
  if (func_offset == 0)
    return true;

  check_alignment(
      probe_.path, symbol, sym_offset, func_offset, safe_mode, probe_.type);
  return true;
}

void AttachedProbe::resolve_offset_kprobe()
{
  offset_ = probe_.func_offset;

  // If we are using only the symbol, we don't need to check the offset.
  bool is_symbol_kprobe = !probe_.attach_point.empty();
  if (is_symbol_kprobe && probe_.func_offset == 0)
    return;

  // Setup the symbol to resolve, either using the address or the name.
  struct symbol sym = {};
  if (is_symbol_kprobe)
    sym.name = probe_.attach_point;
  else
    sym.address = probe_.address;

  auto path = find_vmlinux(&sym);
  if (!path.has_value()) {
    if (!is_symbol_kprobe)
      throw util::FatalUserException("Could not resolve address: " +
                                     std::to_string(probe_.address));

    LOG(V1) << "Could not resolve symbol " << probe_.attach_point
            << ". Skipping usermode offset checking.";
    LOG(V1) << "The kernel will verify the safety of the location but "
               "will also allow the offset to be in a different symbol.";
    return;
  }

  // Populate probe_ fields according to the resolved symbol.
  if (is_symbol_kprobe) {
    probe_.address = sym.start + probe_.func_offset;
  } else {
    probe_.attach_point = std::move(sym.name);
    if (__builtin_sub_overflow(probe_.address, sym.start, &probe_.func_offset))
      LOG(BUG) << "Offset before the function bounds ('" << probe_.attach_point
               << "' address is " << std::to_string(sym.start) << ")";
    offset_ = probe_.func_offset;
    // Set the name of the probe to the resolved symbol+offset, so that failure
    // to attach can be ignored if the user set ConfigMissingProbes::warn.
    probe_.name = "kprobe:" + probe_.attach_point + "+" +
                  std::to_string(probe_.func_offset);
  }

  if (probe_.func_offset >= sym.size)
    throw util::FatalUserException("Offset outside the function bounds ('" +
                                   probe_.attach_point + "' size is " +
                                   std::to_string(sym.size) + ")");
}

void AttachedProbe::attach_multi_kprobe()
{
  BPFTRACE_LIBBPF_OPTS(bpf_link_create_opts, opts);
  std::vector<const char *> syms;
  unsigned int i = 0;

  for (i = 0; i < probe_.funcs.size(); i++) {
    syms.push_back(probe_.funcs[i].c_str());
  }

  opts.kprobe_multi.syms = syms.data();
  opts.kprobe_multi.cnt = syms.size();
  opts.kprobe_multi.flags = probe_.type == ProbeType::kretprobe
                                ? BPF_F_KPROBE_MULTI_RETURN
                                : 0;

  if (bt_verbose) {
    LOG(V1) << "Attaching to " << probe_.funcs.size() << " functions";
    for (i = 0; i < opts.kprobe_multi.cnt; i++) {
      LOG(V1) << " " << syms[i];
    }
  }

  auto attach_type = probe_.is_session ? libbpf::BPF_TRACE_KPROBE_SESSION
                                       : libbpf::BPF_TRACE_KPROBE_MULTI;

  linkfd_ = bpf_link_create(
      progfd_, 0, static_cast<enum ::bpf_attach_type>(attach_type), &opts);
  if (linkfd_ < 0) {
    throw util::FatalUserException("Error attaching probe: " + probe_.name);
  }
}

void AttachedProbe::attach_kprobe()
{
  if (!probe_.funcs.empty()) {
    attach_multi_kprobe();
    return;
  }

  // Construct a string containing "module:function."
  // Also log a warning or throw an error if the module doesn't exist,
  // before attempting to attach.
  // Note that we do not pass vmlinux, if it is specified.
  std::string funcname = probe_.attach_point;
  const std::string &modname = probe_.path;
  if ((!modname.empty()) && modname != "vmlinux") {
    if (!util::is_module_loaded(modname)) {
      std::string message = "specified module " + modname + " in probe " +
                            probe_.name + " is not loaded.";
      if (probe_.orig_name != probe_.name) {
        // Wildcard usage just gets a warning
        LOG(WARNING) << message;
      } else {
        // Explicitly specified modules should fail
        throw util::FatalUserException("Error attaching probe: " + probe_.name +
                                       ": " + message);
      }
    }
    funcname = modname + ":" + funcname;
  }

  // If the user requested to ignore warnings on non-existing probes and the
  // function is not traceable, do not even try to attach as that would yield
  // warnings from BCC which we don't want to see.
  if (bpftrace_.config_->missing_probes == ConfigMissingProbes::ignore &&
      probe_.name != probe_.orig_name &&
      (!funcname.empty() || probe_.address != 0) &&
      !bpftrace_.is_traceable_func(funcname))
    return;

  // The kprobe can either be defined by a symbol+offset or an address:
  // For symbol+offset kprobe, we need to check the validity of the offset.
  // For address kprobe, we need to resolve into the symbol+offset and
  // populate `funcname` with the results stored back in the probe_.
  bool is_symbol_kprobe = !probe_.attach_point.empty();
  resolve_offset_kprobe();
  if (!is_symbol_kprobe)
    funcname += probe_.attach_point;

  LOG(V1) << "bpf_attach_kprobe(" << progfd_ << ", " << probe_.type << ", "
          << eventname() << ", " << funcname << ", " << offset_ << ", 0)";
  int perf_event_fd = bpf_attach_kprobe(progfd_,
                                        attachtype(probe_.type),
                                        eventname().c_str(),
                                        funcname.c_str(),
                                        offset_,
                                        0);

  if (perf_event_fd < 0) {
    if (probe_.orig_name != probe_.name &&
        bpftrace_.config_->missing_probes == ConfigMissingProbes::warn) {
      // a wildcard expansion couldn't probe something, just print a warning
      // as this is normal for some kernel functions (eg, do_debug())
      LOG(WARNING) << "could not attach probe " << probe_.name << ", skipping.";
    } else {
      if (errno == EILSEQ)
        LOG(ERROR) << "Possible attachment attempt in the middle of an "
                      "instruction, try a different offset.";
      // an explicit match failed, so fail as the user must have wanted it
      throw util::FatalUserException("Error attaching probe: " + probe_.name);
    }
  }

  perf_event_fds_.push_back(perf_event_fd);
}

#ifdef HAVE_LIBBPF_UPROBE_MULTI
struct bcc_sym_cb_data {
  std::vector<std::string> &syms;
  std::set<uint64_t> &offsets;
};

static int bcc_sym_cb(const char *symname,
                      uint64_t start,
                      uint64_t /*unused*/,
                      void *p)
{
  auto *data = static_cast<struct bcc_sym_cb_data *>(p);
  std::vector<std::string> &syms = data->syms;

  if (std::ranges::binary_search(syms, symname)) {
    data->offsets.insert(start);
  }

  return 0;
}

struct addr_offset {
  uint64_t addr;
  uint64_t offset;
};

static int bcc_load_cb(uint64_t v_addr,
                       uint64_t mem_sz,
                       uint64_t file_offset,
                       void *p)
{
  auto *addrs = static_cast<std::vector<struct addr_offset> *>(p);

  for (auto &a : *addrs) {
    if (a.addr >= v_addr && a.addr < (v_addr + mem_sz)) {
      a.offset = a.addr - v_addr + file_offset;
    }
  }

  return 0;
}

static void resolve_offset_uprobe_multi(const std::string &path,
                                        const std::string &probe_name,
                                        const std::vector<std::string> &funcs,
                                        std::vector<std::string> &syms,
                                        std::vector<unsigned long> &offsets)
{
  struct bcc_symbol_option option = {};
  int err;

  // Parse symbols names into syms vector
  for (const std::string &func : funcs) {
    auto pos = func.find(':');

    if (pos == std::string::npos) {
      throw util::FatalUserException("Error resolving probe: " + probe_name);
    }

    syms.push_back(func.substr(pos + 1));
  }

  std::ranges::sort(syms);

  option.use_debug_file = 1;
  option.use_symbol_type = BCC_SYM_ALL_TYPES ^ (1 << STT_NOTYPE);

  std::vector<struct addr_offset> addrs;
  std::set<uint64_t> set;
  struct bcc_sym_cb_data data = {
    .syms = syms,
    .offsets = set,
  };

  // Resolve symbols into addresses
  err = bcc_elf_foreach_sym(path.c_str(), bcc_sym_cb, &option, &data);
  if (err) {
    throw util::FatalUserException("Failed to list symbols for probe: " +
                                   probe_name);
  }

  for (auto a : set) {
    struct addr_offset addr = {
      .addr = a,
      .offset = 0x0,
    };

    addrs.push_back(addr);
  }

  // Translate addresses into offsets
  err = bcc_elf_foreach_load_section(path.c_str(), bcc_load_cb, &addrs);
  if (err) {
    throw util::FatalUserException(
        "Failed to resolve symbols offsets for probe: " + probe_name);
  }

  for (auto a : addrs) {
    offsets.push_back(a.offset);
  }
}

void AttachedProbe::attach_multi_uprobe(std::optional<int> pid)
{
  std::vector<std::string> syms;
  std::vector<unsigned long> offsets;
  unsigned int i;

  // Resolve probe_.funcs into offsets and syms vector
  resolve_offset_uprobe_multi(
      probe_.path, probe_.name, probe_.funcs, syms, offsets);

  // Attach uprobe through uprobe_multi link
  BPFTRACE_LIBBPF_OPTS(bpf_link_create_opts, opts);

  opts.uprobe_multi.path = probe_.path.c_str();
  opts.uprobe_multi.offsets = offsets.data();
  opts.uprobe_multi.cnt = offsets.size();
  opts.uprobe_multi.flags = probe_.type == ProbeType::uretprobe
                                ? BPF_F_UPROBE_MULTI_RETURN
                                : 0;
  if (pid.has_value()) {
    opts.uprobe_multi.pid = *pid;
  }

  if (bt_verbose) {
    LOG(V1) << "Attaching to " << probe_.funcs.size() << " functions";
    for (i = 0; i < syms.size(); i++) {
      LOG(V1) << probe_.path << ":" << syms[i];
    }
  }

  linkfd_ = bpf_link_create(progfd_,
                            0,
                            static_cast<enum ::bpf_attach_type>(
                                libbpf::BPF_TRACE_UPROBE_MULTI),
                            &opts);
  if (linkfd_ < 0) {
    throw util::FatalUserException("Error attaching probe: " + probe_.name);
  }
}
#else
void AttachedProbe::attach_multi_uprobe([[maybe_unused]] std::optional<int> pid)
{
}
#endif // HAVE_LIBBPF_UPROBE_MULTI

void AttachedProbe::attach_uprobe(std::optional<int> pid, bool safe_mode)
{
  if (!probe_.funcs.empty()) {
    attach_multi_uprobe(pid);
    return;
  }

  if (!resolve_offset_uprobe(safe_mode, probe_.orig_name != probe_.name))
    return;

  int perf_event_fd = bpf_attach_uprobe(progfd_,
                                        attachtype(probe_.type),
                                        eventname().c_str(),
                                        probe_.path.c_str(),
                                        offset_,
                                        pid.has_value() ? *pid : -1,
                                        0);

  if (perf_event_fd < 0) {
    throw util::FatalUserException("Error attaching probe: " + probe_.name);
  }

  perf_event_fds_.push_back(perf_event_fd);
}

int AttachedProbe::usdt_sem_up_manual(const std::string &fn_name, void *ctx)
{
  int err;

#ifdef BCC_USDT_HAS_FULLY_SPECIFIED_PROBE
  if (probe_.ns.empty())
    err = bcc_usdt_enable_probe(ctx,
                                probe_.attach_point.c_str(),
                                fn_name.c_str());
  else
    err = bcc_usdt_enable_fully_specified_probe(
        ctx, probe_.ns.c_str(), probe_.attach_point.c_str(), fn_name.c_str());
#else
  err = bcc_usdt_enable_probe(ctx,
                              probe_.attach_point.c_str(),
                              fn_name.c_str());
#endif // BCC_USDT_HAS_FULLY_SPECIFIED_PROBE

  // Defer context destruction until probes are detached b/c context
  // destruction will decrement usdt semaphore count.
  usdt_destructor_ = [ctx]() { bcc_usdt_close(ctx); };

  return err;
}

int AttachedProbe::usdt_sem_up_manual_addsem(int pid,
                                             const std::string &fn_name,
                                             void *ctx)
{
  // NB: we are careful to capture by value here everything that will not
  // be available in AttachedProbe destructor.
  auto addsem = [this, fn_name](void *c, int16_t val) -> int {
    if (this->probe_.ns.empty())
      return bcc_usdt_addsem_probe(
          c, this->probe_.attach_point.c_str(), fn_name.c_str(), val);
    else
      return bcc_usdt_addsem_fully_specified_probe(
          c,
          this->probe_.ns.c_str(),
          this->probe_.attach_point.c_str(),
          fn_name.c_str(),
          val);
  };

  // Set destructor to decrement the semaphore count
  usdt_destructor_ = [pid, addsem]() {
    void *c = bcc_usdt_new_frompid(pid, nullptr);
    if (!c)
      return;

    addsem(c, -1);
    bcc_usdt_close(c);
  };

  // Use semaphore increment API to avoid having to hold onto the usdt context
  // for the entire tracing session. Reason we do it this way instead of
  // holding onto usdt context is b/c each usdt context can take lots of memory
  // (~10MB). This, coupled with --usdt-file-activation and tracees that have a
  // forking model can cause bpftrace to use huge amounts of memory if we hold
  // onto the contexts.
  int err = addsem(ctx, +1);

  // Now close the context to save some memory
  bcc_usdt_close(ctx);

  return err;
}

int AttachedProbe::usdt_sem_up([[maybe_unused]] BPFfeature &feature,
                               [[maybe_unused]] int pid,
                               const std::string &fn_name,
                               void *ctx)
{
  // If we have BCC and kernel support for uprobe refcnt API, then we don't
  // need to do anything here. The kernel will increment the semaphore count
  // for us when we provide the semaphore offset.
  if (feature.has_uprobe_refcnt()) {
    bcc_usdt_close(ctx);
    return 0;
  }

  return usdt_sem_up_manual_addsem(pid, fn_name, ctx);
}

void AttachedProbe::attach_usdt(std::optional<int> pid, BPFfeature &feature)
{
  struct bcc_usdt_location loc = {};
  int err;
  void *ctx;
  // TODO: fn_name may need a unique suffix for each attachment on the same
  // probe:
  std::string fn_name = "probe_" + probe_.attach_point + "_1";

  if (pid.has_value()) {
    // FIXME when iovisor/bcc#2064 is merged, optionally pass probe_.path
    ctx = bcc_usdt_new_frompid(*pid, nullptr);
    if (!ctx)
      throw util::FatalUserException(
          "Error initializing context for probe: " + probe_.name +
          ", for PID: " + std::to_string(*pid));
  } else {
    ctx = bcc_usdt_new_frompath(probe_.path.c_str());
    if (!ctx)
      throw util::FatalUserException("Error initializing context for probe: " +
                                     probe_.name);
  }

  // Resolve location of usdt probe
  auto u = usdt_helper.find(pid, probe_.path, probe_.ns, probe_.attach_point);
  if (!u.has_value())
    throw util::FatalUserException("Failed to find usdt probe: " + eventname());
  probe_.path = u->path;

  err = bcc_usdt_get_location(ctx,
                              probe_.ns.c_str(),
                              probe_.attach_point.c_str(),
                              probe_.usdt_location_idx,
                              &loc);
  if (err)
    throw util::FatalUserException("Error finding location for probe: " +
                                   probe_.name);
  probe_.loc = loc.address;

  offset_ = resolve_offset(probe_.path, probe_.attach_point, probe_.loc);

  // Should be 0 if there's no semaphore
  //
  // Cast to 32 bits b/c kernel API only takes 32 bit offset
  [[maybe_unused]] auto semaphore_offset = static_cast<uint32_t>(
      u->semaphore_offset);

  // Increment the semaphore count (will noop if no semaphore)
  //
  // NB: Do *not* use `ctx` after this call. It may either be open or closed,
  // depending on which path was taken.
  err = usdt_sem_up(feature, pid.value_or(0), fn_name, ctx);

  if (err) {
    throw util::FatalUserException(
        "Error finding or enabling probe: " + probe_.name +
        "\n Try using -p or --usdt-file-activation if there's USDT semaphores");
  }

  int perf_event_fd = bpf_attach_uprobe(progfd_,
                                        attachtype(probe_.type),
                                        eventname().c_str(),
                                        probe_.path.c_str(),
                                        offset_,
                                        pid.has_value() ? *pid : -1,
                                        semaphore_offset);

  if (perf_event_fd < 0) {
    if (pid.has_value())
      throw util::FatalUserException("Error attaching probe: " + probe_.name +
                                     ", to PID: " + std::to_string(*pid));
    else
      throw util::FatalUserException("Error attaching probe: " + probe_.name);
  }

  perf_event_fds_.push_back(perf_event_fd);
}

void AttachedProbe::attach_tracepoint()
{
  int perf_event_fd = bpf_attach_tracepoint(progfd_,
                                            probe_.path.c_str(),
                                            eventname().c_str());

  if (perf_event_fd < 0 && probe_.name == probe_.orig_name) {
    throw util::FatalUserException("Error attaching probe: " + probe_.name);
  }

  perf_event_fds_.push_back(perf_event_fd);
}

void AttachedProbe::attach_profile(std::optional<int> pid)
{
  int group_fd = -1;

  uint64_t period, freq;
  if (probe_.path == "hz") {
    period = 0;
    freq = probe_.freq;
  } else if (probe_.path == "s") {
    period = probe_.freq * 1e9;
    freq = 0;
  } else if (probe_.path == "ms") {
    period = probe_.freq * 1e6;
    freq = 0;
  } else if (probe_.path == "us") {
    period = probe_.freq * 1e3;
    freq = 0;
  } else {
    throw util::FatalUserException("invalid profile path \"" + probe_.path +
                                   "\"");
  }

  std::vector<int> cpus = util::get_online_cpus();
  for (int cpu : cpus) {
    int perf_event_fd = bpf_attach_perf_event(progfd_,
                                              PERF_TYPE_SOFTWARE,
                                              PERF_COUNT_SW_CPU_CLOCK,
                                              period,
                                              freq,
                                              pid.has_value() ? *pid : -1,
                                              cpu,
                                              group_fd);

    if (perf_event_fd < 0) {
      throw util::FatalUserException("Error attaching probe: " + probe_.name);
    }

    perf_event_fds_.push_back(perf_event_fd);
  }
}

void AttachedProbe::attach_interval(std::optional<int> pid)
{
  int group_fd = -1;
  int cpu = 0;

  uint64_t period = 0, freq = 0;
  if (probe_.path == "s") {
    period = probe_.freq * 1e9;
  } else if (probe_.path == "ms") {
    period = probe_.freq * 1e6;
  } else if (probe_.path == "us") {
    period = probe_.freq * 1e3;
  } else if (probe_.path == "hz") {
    freq = probe_.freq;
  } else {
    throw util::FatalUserException("invalid interval path \"" + probe_.path +
                                   "\"");
  }

  int perf_event_fd = bpf_attach_perf_event(progfd_,
                                            PERF_TYPE_SOFTWARE,
                                            PERF_COUNT_SW_CPU_CLOCK,
                                            period,
                                            freq,
                                            pid.has_value() ? *pid : -1,
                                            cpu,
                                            group_fd);

  if (perf_event_fd < 0) {
    throw util::FatalUserException("Error attaching probe: " + probe_.name);
  }

  perf_event_fds_.push_back(perf_event_fd);
}

void AttachedProbe::attach_software(std::optional<int> pid)
{
  int group_fd = -1;

  uint64_t period = probe_.freq;
  uint64_t defaultp = 1;
  uint32_t type = 0;

  // from linux/perf_event.h, with aliases from perf:
  for (const auto &probeListItem : SW_PROBE_LIST) {
    if (probe_.path == probeListItem.path ||
        probe_.path == probeListItem.alias) {
      type = probeListItem.type;
      defaultp = probeListItem.defaultp;
    }
  }

  if (period == 0)
    period = defaultp;

  std::vector<int> cpus = util::get_online_cpus();
  for (int cpu : cpus) {
    int perf_event_fd = bpf_attach_perf_event(progfd_,
                                              PERF_TYPE_SOFTWARE,
                                              type,
                                              period,
                                              0,
                                              pid.has_value() ? *pid : -1,
                                              cpu,
                                              group_fd);

    if (perf_event_fd < 0) {
      throw util::FatalUserException("Error attaching probe: " + probe_.name);
    }

    perf_event_fds_.push_back(perf_event_fd);
  }
}

void AttachedProbe::attach_hardware(std::optional<int> pid)
{
  int group_fd = -1;

  uint64_t period = probe_.freq;
  uint64_t defaultp = 1000000;
  uint32_t type = 0;

  // from linux/perf_event.h, with aliases from perf:
  for (const auto &probeListItem : HW_PROBE_LIST) {
    if (probe_.path == probeListItem.path ||
        probe_.path == probeListItem.alias) {
      type = probeListItem.type;
      defaultp = probeListItem.defaultp;
    }
  }

  if (period == 0)
    period = defaultp;

  std::vector<int> cpus = util::get_online_cpus();
  for (int cpu : cpus) {
    int perf_event_fd = bpf_attach_perf_event(progfd_,
                                              PERF_TYPE_HARDWARE,
                                              type,
                                              period,
                                              0,
                                              pid.has_value() ? *pid : -1,
                                              cpu,
                                              group_fd);

    if (perf_event_fd < 0) {
      throw util::FatalUserException("Error attaching probe: " + probe_.name);
    }

    perf_event_fds_.push_back(perf_event_fd);
  }
}

void AttachedProbe::attach_watchpoint(std::optional<int> pid,
                                      const std::string &mode)
{
  struct perf_event_attr attr = {};
  attr.type = PERF_TYPE_BREAKPOINT;
  attr.size = sizeof(struct perf_event_attr);
  attr.config = 0;

  attr.bp_type = HW_BREAKPOINT_EMPTY;
  for (const char c : mode) {
    if (c == 'r')
      attr.bp_type |= HW_BREAKPOINT_R;
    else if (c == 'w')
      attr.bp_type |= HW_BREAKPOINT_W;
    else if (c == 'x')
      attr.bp_type |= HW_BREAKPOINT_X;
  }

  attr.bp_addr = probe_.address;
  attr.bp_len = probe_.len;
  // Generate a notification every 1 event; we care about every event
  attr.sample_period = 1;

  std::vector<int> cpus;
  if (pid.has_value()) {
    cpus = { -1 };
  } else {
    cpus = util::get_online_cpus();
  }

  for (int cpu : cpus) {
    // We copy paste the code from bcc's bpf_attach_perf_event_raw here
    // because we need to know the exact error codes (and also we don't
    // want bcc's noisy error messages).
    int perf_event_fd = syscall(__NR_perf_event_open,
                                &attr,
                                pid.has_value() ? *pid : -1,
                                cpu,
                                -1,
                                PERF_FLAG_FD_CLOEXEC);
    if (perf_event_fd < 0) {
      if (errno == ENOSPC)
        throw util::EnospcException("No more HW registers left");
      else
        throw std::system_error(errno,
                                std::generic_category(),
                                "Error attaching probe: " + probe_.name);
    }
    if (ioctl(perf_event_fd, PERF_EVENT_IOC_SET_BPF, progfd_) != 0) {
      close(perf_event_fd);
      throw std::system_error(errno,
                              std::generic_category(),
                              "Error attaching probe: " + probe_.name);
    }
    if (ioctl(perf_event_fd, PERF_EVENT_IOC_ENABLE, 0) != 0) {
      close(perf_event_fd);
      throw std::system_error(errno,
                              std::generic_category(),
                              "Error attaching probe: " + probe_.name);
    }

    perf_event_fds_.push_back(perf_event_fd);
  }
}

} // namespace bpftrace
