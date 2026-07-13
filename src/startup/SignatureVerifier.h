#pragma once
#include <optional>
#include <string>
#include <unordered_map>
#include <windows.h>

namespace startup {

enum class SignatureStatus { Trusted, NotSigned, VerificationFailed };

struct SignatureInfo {
    SignatureStatus status = SignatureStatus::VerificationFailed;
    bool isMicrosoftSigned = false; // only meaningful when status == Trusted
    // Signer's certificate subject name (original case). Only set when
    // status == Trusted -- callers show "Sin firmar" for NotSigned and "No
    // se pudo comprobar la firma" for VerificationFailed instead of relying
    // on this being empty.
    std::optional<std::wstring> signerName;
};

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

    // Used by the "info" popup to show the actual signer/trust status, not
    // just the Microsoft/not-Microsoft verdict. Shares the same
    // per-(path,mtime) cache as IsMicrosoftSigned, so opening the info
    // popup after a scan already computed the verdict for this path
    // doesn't re-verify.
    SignatureInfo GetSignatureInfo(const std::wstring& exePath);

private:
    struct CacheEntry {
        FILETIME lastWriteTime = {};
        SignatureStatus status = SignatureStatus::VerificationFailed;
        bool isMicrosoftSigned = false;
        std::optional<std::wstring> signerName;
    };
    CacheEntry Resolve(const std::wstring& exePath);

    std::unordered_map<std::wstring, CacheEntry> cache_;
};

} // namespace startup
