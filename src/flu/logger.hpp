#pragma once

#include <cstdlib>
#include <iostream>
#include <ostream>
#include <string>
#include <utility>

#include <fmt/core.h>
#include <fmt/format.h>

namespace flu {

// Prefixed, verbosity-gated reporter for the tool's own messages: option
// errors, progress notes, and anything else that is about the run rather than
// about the Fortran being read. Diagnostics *about the source* go through
// flu/diagnostics.hpp instead, so that they carry a symbol's location and are
// flushed by flang's message machinery.
//
// A logger emits only when its verbosity is at or below the global threshold,
// which starts at FLAIR_VERBOSE (0 if unset or unparsable) and is raised by
// -v. error() and warning() sit at 0 and so always emit; report(), like a
// plain `logger{...}`, sits at the default 1 and emits only under -v.
class logger {

  static int env_verbose() {
    char const *v = std::getenv("FLAIR_VERBOSE");
    if (v == nullptr)
      return 0;
    char *end = nullptr;
    long const n = std::strtol(v, &end, 10);
    return end == v ? 0 : static_cast<int>(n);
  }

  inline static int s_verbose = env_verbose();

  bool active_ = false;
  int verbosity_ = 1;
  std::string intro_;
  std::string intro_spaces_;
  std::string head_line_;
  std::string head_line_spaces_;

  // Continuation lines are indented under the prefix, so a multi-line message
  // stays visually one block.
  void emit_(std::ostream &os, std::string const &mess) const {
    os << head_line_ << intro_;
    for (size_t pos = 0, line = 0;; ++line) {
      size_t const nl = mess.find('\n', pos);
      if (line > 0)
        os << '\n' << head_line_spaces_ << intro_spaces_;
      os << mess.substr(pos, nl == std::string::npos ? nl : nl - pos);
      if (nl == std::string::npos)
        break;
      pos = nl + 1;
    }
    os << '\n';
  }

public:
  logger() = default;

  logger(std::string headline, std::string introduction = {}, int verbosity = 1)
      : active_{true}, verbosity_{verbosity}, intro_{std::move(introduction)},
        intro_spaces_(intro_.size(), ' '), head_line_{std::move(headline)},
        head_line_spaces_(head_line_.size(), ' ') {}

  void operator()(std::string const &mess) const {
    if (active_ && verbosity_ <= s_verbose)
      emit_(std::cerr, mess);
  }

  template <typename... T>
  void operator()(fmt::format_string<T...> const &s, T &&...args) const {
    operator()(fmt::format(s, std::forward<T>(args)...));
  }

  static void set_verbose(int v) { s_verbose = v; }
  [[nodiscard]] static int verbose() { return s_verbose; }

  static logger error() { return {"-- ", "\033[1;31merror: \033[0m", 0}; }
  static logger warning() { return {"-- ", "\033[1;35mwarning: \033[0m", 0}; }
  static logger report() { return {"-- ", ""}; }
  static logger debug() { return {"-- ", "\033[1;31mDEBUG: \033[0m", 5}; }
};

} // namespace flu
