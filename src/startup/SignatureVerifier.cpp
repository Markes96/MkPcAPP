#include "SignatureVerifier.h"
#include <softpub.h>
#include <wintrust.h>
#include <wincrypt.h>
#include <mscat.h>
#include <bcrypt.h>
#include <algorithm>
#include <cwctype>
#include <vector>

namespace startup {

namespace {

bool GetFileLastWriteTime(const std::wstring& path, FILETIME& outTime) {
    WIN32_FILE_ATTRIBUTE_DATA data;
    if (!GetFileAttributesExW(path.c_str(), GetFileExInfoStandard, &data)) {
        return false;
    }
    outTime = data.ftLastWriteTime;
    return true;
}

bool SameFileTime(const FILETIME& a, const FILETIME& b) {
    return a.dwLowDateTime == b.dwLowDateTime && a.dwHighDateTime == b.dwHighDateTime;
}

struct TrustResult {
    SignatureStatus status = SignatureStatus::VerificationFailed;
    std::optional<std::wstring> signerName;
};

// Pulls the signer's subject common name (original case) straight out of
// the trust provider's own verification state via WTHelperProvDataFromStateData
// / WTHelperGetProvSignerFromChain, instead of re-deriving it with a second
// CryptQueryObject(..., CERT_QUERY_CONTENT_FLAG_PE, ...) pass. That older
// approach only sees an *embedded* Authenticode signature, so it silently
// missed catalog-signed system binaries (e.g. SecurityHealthSystray.exe,
// signed via a .cat catalog rather than an embedded signature) -- they'd
// come back Trusted with no signer name, isMicrosoftSigned=false, and leak
// into the visible startup list despite being core Windows components.
// Reading the signer from the trust state instead covers both cases,
// because WinVerifyTrust itself already consulted the catalog database to
// produce that Trusted verdict.
std::optional<std::wstring> ExtractSignerName(HANDLE stateData) {
    CRYPT_PROVIDER_DATA* provData = WTHelperProvDataFromStateData(stateData);
    if (!provData) {
        return std::nullopt;
    }
    CRYPT_PROVIDER_SGNR* signer = WTHelperGetProvSignerFromChain(provData, 0, FALSE, 0);
    if (!signer || signer->csCertChain == 0) {
        return std::nullopt;
    }
    CRYPT_PROVIDER_CERT* cert = WTHelperGetProvCertFromChain(signer, 0);
    if (!cert || !cert->pCert) {
        return std::nullopt;
    }
    wchar_t nameBuffer[256] = {};
    CertGetNameStringW(cert->pCert, CERT_NAME_SIMPLE_DISPLAY_TYPE, 0, nullptr, nameBuffer, ARRAYSIZE(nameBuffer));
    return std::wstring(nameBuffer);
}

// WinVerifyTrust is a state-machine API: dwStateAction selects verify vs.
// close. The second call (WTD_STATEACTION_CLOSE) is required cleanup for the
// trust provider state handle, easy to miss since it isn't a normal
// Release()/Close() call. TRUST_E_NOSIGNATURE is a documented WinVerifyTrust
// status distinct from other failures -- distinguishing it lets callers show
// "Sin firmar" separately from "no se pudo comprobar la firma". The signer
// name must be extracted before the CLOSE call, which tears down the state
// WTHelperProvDataFromStateData reads from.
TrustResult CheckTrustAsFile(const std::wstring& path) {
    WINTRUST_FILE_INFO fileInfo = {};
    fileInfo.cbStruct = sizeof(fileInfo);
    fileInfo.pcwszFilePath = path.c_str();

    GUID action = WINTRUST_ACTION_GENERIC_VERIFY_V2;
    WINTRUST_DATA trustData = {};
    trustData.cbStruct = sizeof(trustData);
    trustData.dwUIChoice = WTD_UI_NONE;
    trustData.fdwRevocationChecks = WTD_REVOKE_NONE;
    trustData.dwUnionChoice = WTD_CHOICE_FILE;
    trustData.pFile = &fileInfo;
    trustData.dwStateAction = WTD_STATEACTION_VERIFY;

    LONG status = WinVerifyTrust(nullptr, &action, &trustData);

    TrustResult result;
    if (status == ERROR_SUCCESS) {
        result.status = SignatureStatus::Trusted;
        result.signerName = ExtractSignerName(trustData.hWVTStateData);
    } else if (status == TRUST_E_NOSIGNATURE) {
        result.status = SignatureStatus::NotSigned;
    } else {
        result.status = SignatureStatus::VerificationFailed;
    }

    trustData.dwStateAction = WTD_STATEACTION_CLOSE;
    WinVerifyTrust(nullptr, &action, &trustData);

    return result;
}

// WINTRUST_ACTION_GENERIC_VERIFY_V2 against WTD_CHOICE_FILE only looks for
// an *embedded* Authenticode signature -- unlike Explorer's "Digital
// Signatures" property tab (and PowerShell's Get-AuthenticodeSignature), it
// does not automatically consult the Windows catalog database. Most files
// under system32 (SecurityHealthSystray.exe among them) are catalog-signed,
// not embedded-signed, so CheckTrustAsFile alone reports them as unsigned.
// This mirrors what those tools actually do: hash the file, find which
// catalog (if any) lists that hash, then verify trust against that catalog
// with WTD_CHOICE_CATALOG.
TrustResult CheckTrustViaCatalog(const std::wstring& path) {
    TrustResult result;
    result.status = SignatureStatus::NotSigned;

    HANDLE hFile = CreateFileW(path.c_str(), GENERIC_READ, FILE_SHARE_READ, nullptr, OPEN_EXISTING,
                                FILE_ATTRIBUTE_NORMAL, nullptr);
    if (hFile == INVALID_HANDLE_VALUE) {
        return result;
    }

    // DRIVER_ACTION_VERIFY expands to a brace-initializer GUID literal, not
    // an addressable object -- needs a named variable before it can be
    // passed by pointer, same as WINTRUST_ACTION_GENERIC_VERIFY_V2 below.
    GUID subsystem = DRIVER_ACTION_VERIFY;
    HCATADMIN hCatAdmin = nullptr;
    if (!CryptCATAdminAcquireContext2(&hCatAdmin, &subsystem, BCRYPT_SHA256_ALGORITHM, nullptr, 0)) {
        CloseHandle(hFile);
        return result;
    }

    DWORD hashSize = 0;
    CryptCATAdminCalcHashFromFileHandle2(hCatAdmin, hFile, &hashSize, nullptr, 0);
    if (hashSize == 0) {
        CryptCATAdminReleaseContext(hCatAdmin, 0);
        CloseHandle(hFile);
        return result;
    }
    std::vector<BYTE> hash(hashSize);
    if (!CryptCATAdminCalcHashFromFileHandle2(hCatAdmin, hFile, &hashSize, hash.data(), 0)) {
        CryptCATAdminReleaseContext(hCatAdmin, 0);
        CloseHandle(hFile);
        return result;
    }

    HCATINFO hCatInfo = CryptCATAdminEnumCatalogFromHash(hCatAdmin, hash.data(), hashSize, 0, nullptr);
    if (!hCatInfo) {
        CryptCATAdminReleaseContext(hCatAdmin, 0);
        CloseHandle(hFile);
        return result; // no catalog lists this hash -- genuinely unsigned
    }

    CATALOG_INFO catInfo = {};
    catInfo.cbStruct = sizeof(catInfo);
    CryptCATCatalogInfoFromContext(hCatInfo, &catInfo, 0);

    WINTRUST_CATALOG_INFO catalogData = {};
    catalogData.cbStruct = sizeof(catalogData);
    catalogData.pcwszCatalogFilePath = catInfo.wszCatalogFile;
    catalogData.pcwszMemberFilePath = path.c_str();
    catalogData.hMemberFile = hFile;
    catalogData.pbCalculatedFileHash = hash.data();
    catalogData.cbCalculatedFileHash = hashSize;
    catalogData.hCatAdmin = hCatAdmin;

    GUID action = WINTRUST_ACTION_GENERIC_VERIFY_V2;
    WINTRUST_DATA trustData = {};
    trustData.cbStruct = sizeof(trustData);
    trustData.dwUIChoice = WTD_UI_NONE;
    trustData.fdwRevocationChecks = WTD_REVOKE_NONE;
    trustData.dwUnionChoice = WTD_CHOICE_CATALOG;
    trustData.pCatalog = &catalogData;
    trustData.dwStateAction = WTD_STATEACTION_VERIFY;

    LONG status = WinVerifyTrust(nullptr, &action, &trustData);
    if (status == ERROR_SUCCESS) {
        result.status = SignatureStatus::Trusted;
        result.signerName = ExtractSignerName(trustData.hWVTStateData);
    } else if (status != TRUST_E_NOSIGNATURE) {
        result.status = SignatureStatus::VerificationFailed;
    }

    trustData.dwStateAction = WTD_STATEACTION_CLOSE;
    WinVerifyTrust(nullptr, &action, &trustData);

    CryptCATAdminReleaseCatalogContext(hCatAdmin, hCatInfo, 0);
    CryptCATAdminReleaseContext(hCatAdmin, 0);
    CloseHandle(hFile);
    return result;
}

TrustResult CheckTrust(const std::wstring& path) {
    TrustResult fileResult = CheckTrustAsFile(path);
    if (fileResult.status != SignatureStatus::NotSigned) {
        return fileResult;
    }
    // No embedded signature -- fall back to the catalog database before
    // concluding the file is genuinely unsigned.
    return CheckTrustViaCatalog(path);
}

bool NameIndicatesMicrosoft(const std::wstring& name) {
    std::wstring lower = name;
    std::transform(lower.begin(), lower.end(), lower.begin(),
                    [](wchar_t c) { return static_cast<wchar_t>(std::towlower(c)); });
    return lower.find(L"microsoft windows") != std::wstring::npos ||
           lower.find(L"microsoft corporation") != std::wstring::npos;
}

} // namespace

SignatureVerifier::CacheEntry SignatureVerifier::Resolve(const std::wstring& exePath) {
    if (exePath.empty()) {
        return {};
    }

    FILETIME currentWriteTime = {};
    bool haveWriteTime = GetFileLastWriteTime(exePath, currentWriteTime);

    auto it = cache_.find(exePath);
    if (it != cache_.end() && haveWriteTime && SameFileTime(it->second.lastWriteTime, currentWriteTime)) {
        return it->second;
    }

    CacheEntry entry;
    entry.lastWriteTime = currentWriteTime;
    TrustResult trust = CheckTrust(exePath);
    entry.status = trust.status;
    if (entry.status == SignatureStatus::Trusted) {
        entry.signerName = trust.signerName;
        entry.isMicrosoftSigned = entry.signerName.has_value() && NameIndicatesMicrosoft(*entry.signerName);
    }

    if (haveWriteTime) {
        cache_[exePath] = entry;
    }
    return entry;
}

bool SignatureVerifier::IsMicrosoftSigned(const std::wstring& exePath) {
    return Resolve(exePath).isMicrosoftSigned;
}

SignatureInfo SignatureVerifier::GetSignatureInfo(const std::wstring& exePath) {
    CacheEntry entry = Resolve(exePath);
    SignatureInfo info;
    info.status = entry.status;
    info.isMicrosoftSigned = entry.isMicrosoftSigned;
    info.signerName = entry.signerName;
    return info;
}

} // namespace startup
