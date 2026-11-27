#pragma once
// Red-team captive-portal input validation.
//
// Enforces the same kind of format rules a normal provider login page applies,
// so the portal only accepts a well-formed email + password and re-prompts on
// anything malformed. Keeps operator loot free of typos / bot junk and makes the
// page behave credibly. Pure, allocation-free, safe to call from the HTTP task.
#include <stddef.h>
#include <stdint.h>
#include <string.h>

namespace specter {

// Tunable rules (mirror a typical consumer login form).
constexpr size_t kEmailMinLen      = 6;    // shortest sane form: a@b.cd
constexpr size_t kEmailMaxLen      = 254;  // RFC 5321 practical total limit
constexpr size_t kEmailLocalMaxLen = 64;   // RFC 5321 local-part limit
constexpr size_t kPasswordMinLen   = 8;    // common portal minimum
constexpr size_t kPasswordMaxLen   = 128;  // sane upper bound
constexpr size_t kUsernameMinLen   = 1;    // non-email identifier: just non-empty
constexpr size_t kUsernameMaxLen   = 256;

inline bool isEmailLocalChar(char c) {
  return (c >= 'a' && c <= 'z') || (c >= 'A' && c <= 'Z') ||
         (c >= '0' && c <= '9') ||
         c == '.' || c == '_' || c == '%' || c == '+' || c == '-';
}

inline bool isEmailDomainChar(char c) {
  return (c >= 'a' && c <= 'z') || (c >= 'A' && c <= 'Z') ||
         (c >= '0' && c <= '9') || c == '.' || c == '-';
}

// Lightweight email format check:  local@domain.tld
//   - exactly one '@', non-empty local part (<= 64), domain with a dot
//   - TLD after the last dot is >= 2 chars and all-alpha
//   - no spaces / control chars, no leading/trailing/doubled dots, no edge dashes
inline bool isPlausibleEmail(const char* s, size_t n) {
  if (!s || n < kEmailMinLen || n > kEmailMaxLen) return false;

  size_t at = 0;
  bool haveAt = false;
  for (size_t i = 0; i < n; i++) {
    unsigned char c = (unsigned char)s[i];
    if (c <= 0x20 || c == 0x7f) return false;   // no spaces / controls
    if (c == '@') {
      if (haveAt) return false;                 // only one '@'
      haveAt = true;
      at = i;
    }
  }
  if (!haveAt) return false;

  const size_t localLen = at;
  const size_t domStart = at + 1;
  const size_t domLen = n - domStart;
  if (localLen == 0 || localLen > kEmailLocalMaxLen) return false;
  if (domLen < 3) return false;                 // need x.yy at minimum

  // Local part.
  if (s[0] == '.' || s[at - 1] == '.') return false;
  for (size_t i = 0; i < at; i++) {
    char c = s[i];
    if (!isEmailLocalChar(c)) return false;
    if (c == '.' && i + 1 < at && s[i + 1] == '.') return false;  // no ".."
  }

  // Domain part.
  const char* d = s + domStart;
  if (d[0] == '.' || d[domLen - 1] == '.') return false;
  if (d[0] == '-' || d[domLen - 1] == '-') return false;
  bool haveDot = false;
  size_t lastDot = 0;
  for (size_t i = 0; i < domLen; i++) {
    char c = d[i];
    if (!isEmailDomainChar(c)) return false;
    if (c == '.') {
      if (i + 1 < domLen && d[i + 1] == '.') return false;        // no ".."
      haveDot = true;
      lastDot = i;
    }
  }
  if (!haveDot) return false;

  // TLD: >= 2 chars, all alphabetic.
  const size_t tldLen = domLen - (lastDot + 1);
  if (tldLen < 2) return false;
  for (size_t i = lastDot + 1; i < domLen; i++) {
    char c = d[i];
    if (!((c >= 'a' && c <= 'z') || (c >= 'A' && c <= 'Z'))) return false;
  }
  return true;
}

// Password policy: length window + at least one non-space, no control chars.
// This is format sanity, not strength enforcement.
inline bool meetsPasswordPolicy(const char* s, size_t n) {
  if (!s || n < kPasswordMinLen || n > kPasswordMaxLen) return false;
  bool hasNonSpace = false;
  for (size_t i = 0; i < n; i++) {
    unsigned char c = (unsigned char)s[i];
    if (c < 0x20 || c == 0x7f) return false;    // no control chars
    if (c != ' ') hasNonSpace = true;
  }
  return hasNonSpace;
}

// Non-email identifier (username/user/login): just require a non-empty,
// control-free value within bounds so real username portals are not broken.
inline bool meetsUsernamePolicy(const char* s, size_t n) {
  if (!s || n < kUsernameMinLen || n > kUsernameMaxLen) return false;
  bool hasNonSpace = false;
  for (size_t i = 0; i < n; i++) {
    unsigned char c = (unsigned char)s[i];
    if (c < 0x20 || c == 0x7f) return false;
    if (c != ' ') hasNonSpace = true;
  }
  return hasNonSpace;
}

}  // namespace specter
