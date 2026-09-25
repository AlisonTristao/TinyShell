#ifndef USER_TYPE_CONVERTERS_H
#define USER_TYPE_CONVERTERS_H

// This file is intentionally user-editable: teach TinyShell a new command
// parameter type by specializing tinyshell::ArgType<T> here (or anywhere that
// is visible before the command using it is registered).
//
// Contract (see TypeConversion.h):
//   - `code` is the short type name the help listing shows;
//   - parse() receives the argument already trimmed, unescaped and
//     NUL-terminated, valid only while the command runs;
//   - return false on bad input, optionally pointing `error` at a static
//     reason string; never allocate for its own sake, never throw.
//
// Example -- a 2D point typed as "x;y":
//
//   struct Point { float x, y; };
//
//   template <>
//   struct tinyshell::ArgType<Point> {
//       static constexpr const char* code = "pt";
//       static bool parse(const char* token, Point& out, const char*& error) {
//           const char* sep = std::strchr(token, ';');
//           if (sep == nullptr) { error = "expected x;y"; return false; }
//           out.x = std::strtof(token, nullptr);
//           out.y = std::strtof(sep + 1, nullptr);
//           return true;
//       }
//   };
//
//   shell.add([](Point p) -> uint8_t { ... }, "goto", "Move to x;y", "motion");

#endif  // USER_TYPE_CONVERTERS_H
