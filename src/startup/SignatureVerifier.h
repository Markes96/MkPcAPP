#pragma once
#include <string>
#include <unordered_map>
#include <windows.h>

namespace startup {

// Authenticode-based "is this Microsoft's own binary" check, used to
// exclude core Windows components from the startup list entirely -- never
// just greyed out, actually invisible, per the "no apto para torpes" safety
// goal. Stateful: caches verdicts per (exePath, last-write-time) so a full
// rescan doesn't re-run WinVerifyTrust on every unchanged exe every cycle.
class SignatureVerifier {
public:
    // Any verification failure (unsigned, untrusted, malformed PE, access
    // denied) is treated as "not Microsoft" -- callers show the entry rather
    // than risk hiding something by mistake.
    bool IsMicrosoftSigned(const std::wstring& exePath);

private:
    struct CacheEntry {
        FILETIME lastWriteTime;
        bool isMicrosoftSigned;
    };
    std::unordered_map<std::wstring, CacheEntry> cache_;
};

} // namespace startup
