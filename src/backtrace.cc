/**
 * @file backtrace.cc
 * @author Basit Ayantunde <rlamarrr@gmail.com>
 * @brief
 * @version  0.1
 * @date 2020-05-16
 *
 * @copyright Copyright (c) 2020
 *
 */

// debug-build assertions
#if !defined(NDEBUG) && defined(STX_ENABLE_DEBUG_ASSERTIONS)
#include <cassert>
#include <string>

using namespace std::literals;

#define LOG(x) \
  std::cout << "[" << __FILE__ << ":" << __LINE__ << "] " << x << std::endl
#define ASSERT_UNREACHABLE()        \
  LOG("Source location reached"sv); \
  assert(false)

#define ASSERT_NEQ(a, b)                                                    \
  if (!((a) != (b))) {                                                      \
    LOG("Assertion: '"s + std::to_string(a) + " != "s + std::to_string(b) + \
        "' failed"s);                                                       \
    assert((a) != (b));                                                     \
  }
#define ASSERT_EQ(a, b)                                                     \
  if (!((a) == (b))) {                                                      \
    LOG("Assertion: '"s + std::to_string(a) + " == "s + std::to_string(b) + \
        "' failed"s);                                                       \
    assert((a) == (b));                                                     \
  }
#else
#define LOG(x) (void)0
#define ASSERT_UNREACHABLE() (void)0
#define ASSERT_NEQ(a, b) (void)0
#define ASSERT_EQ(a, b) (void)0
#endif

#include <stdio.h>

#include <array>
#include <csignal>
#include <cstring>
#include <iostream>

#include "absl/debugging/stacktrace.h"
#include "absl/debugging/symbolize.h"
#include "stx/backtrace.h"

namespace stx {

auto backtrace::Symbol::raw() const noexcept -> std::string_view {
  return std::string_view(symbol_.data);
}

size_t backtrace::trace(Callback callback) {
  int skip_count = 1;
  void* ips[STX_MAX_STACK_FRAME_DEPTH] = {};
  void* sps[STX_MAX_STACK_FRAME_DEPTH] = {};
  int sizes[STX_MAX_STACK_FRAME_DEPTH] = {};
  int ips_depth =
      absl::GetStackTrace(ips, sizeof(ips) / sizeof(ips[0]), skip_count);
  int sps_depth = absl::GetStackFrames(sps, sizes, sizeof(sps) / sizeof(sps[0]),
                                       skip_count);

  int depth = ips_depth > sps_depth ? sps_depth : ips_depth;

  char symbol[STX_SYMBOL_BUFFER_SIZE] = {};
  auto max_len = sizeof(symbol) / sizeof(symbol[0]);

  for (int i = 0; i < depth; i++) {
    std::memset(symbol, 0, max_len);
    Frame frame{};
    if (absl::Symbolize(ips[i], symbol, max_len)) {
      auto span = backtrace::CharSpan(symbol, max_len);
      frame.symbol = Some(backtrace::Symbol(std::move(span)));
    }

    frame.ip = Some(reinterpret_cast<uintptr_t>(ips[i]));
    frame.sp = Some(reinterpret_cast<uintptr_t>(sps[i]));

    if (callback(std::move(frame), depth - i)) break;
  }

  return depth;
}

namespace {

void print_backtrace() {
  fputs(
      "\n\nBacktrace:\nip: Instruction Pointer,  sp: Stack "
      "Pointer\n\n",
      stderr);

  backtrace::trace([](backtrace::Frame frame, int i) {
    auto const print_none = []() { fputs("<unknown>", stderr); };

    fprintf(stderr, "#%d\t\t", i);

    frame.symbol.as_ref().match(
        [](Ref<backtrace::Symbol> sym) {
          for (char c : sym.get().raw()) {
            fputc(c, stderr);
          }
        },
        print_none);

    fputs("\t (ip: 0x", stderr);

    frame.ip.as_ref().match(
        [](Ref<uintptr_t> ip) { fprintf(stderr, "%" PRIxPTR, ip.get()); },
        print_none);

    fputs(", sp: 0x", stderr);

    frame.sp.as_ref().match(
        [](Ref<uintptr_t> sp) { fprintf(stderr, "%" PRIxPTR, sp.get()); },
        print_none);

    fputs(")\n", stderr);

    return false;
  });

  fputs("\n", stderr);
}

[[noreturn]] void signal_handler(int signal) {
  fputs("\n\n", stderr);
  switch (signal) {
    case SIGSEGV:
      fputs(
          "Received 'SIGSEGV' signal. Invalid memory access occurred "
          "(segmentation fault).",
          stderr);
      break;
    case SIGILL:
      fputs(
          "Received 'SIGILL' signal. Invalid program image (illegal/invalid "
          "instruction, i.e. nullptr dereferencing).",
          stderr);
      break;
    case SIGFPE:
      fputs(
          "Received 'SIGFPE' signal. Erroneous arithmetic operation (i.e. "
          "divide by zero).",
          stderr);
      break;
  }

  print_backtrace();
  std::abort();
}
}  // namespace

auto backtrace::handle_signal(int signal) noexcept
    -> Result<void (*)(int), SignalError> {
  if (signal != SIGSEGV && signal != SIGILL && signal != SIGFPE)
    return Err(SignalError::Unknown);

  auto err = std::signal(signal, signal_handler);

  if (err == SIG_ERR) return Err(SignalError::SigErr);

  return Ok(std::move(err));
}

};  // namespace stx
