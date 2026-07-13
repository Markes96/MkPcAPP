#include "SignatureVerifier.h"
#include <softpub.h>
#include <wintrust.h>
#include <wincrypt.h>
#include <algorithm>
#include <cwctype>
#include <optional>
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

// WinVerifyTrust is a state-machine API: dwStateAction selects verify vs.
// close. The second call (WTD_STATEACTION_CLOSE) is required cleanup for the
// trust provider state handle, easy to miss since it isn't a normal
// Release()/Close() call.
bool VerifyTrust(const std::wstring& path) {
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

    trustData.dwStateAction = WTD_STATEACTION_CLOSE;
    WinVerifyTrust(nullptr, &action, &trustData);

    return status == ERROR_SUCCESS;
}

// Extracts the signer's subject common name (lowercased) from the file's
// Authenticode signature. Only meaningful to call after VerifyTrust() has
// already returned true for this path.
std::optional<std::wstring> GetSignerDisplayNameLower(const std::wstring& path) {
    HCERTSTORE store = nullptr;
    HCRYPTMSG msg = nullptr;
    DWORD encoding = 0, contentType = 0, formatType = 0;

    if (!CryptQueryObject(CERT_QUERY_OBJECT_FILE, path.c_str(), CERT_QUERY_CONTENT_FLAG_PE,
                           CERT_QUERY_FORMAT_FLAG_BINARY, 0, &encoding, &contentType, &formatType, &store,
                           &msg, nullptr)) {
        return std::nullopt;
    }

    std::optional<std::wstring> result;
    DWORD signerInfoSize = 0;
    if (CryptMsgGetParam(msg, CMSG_SIGNER_INFO_PARAM, 0, nullptr, &signerInfoSize) && signerInfoSize > 0) {
        std::vector<BYTE> signerInfoBuffer(signerInfoSize);
        CMSG_SIGNER_INFO* signerInfo = reinterpret_cast<CMSG_SIGNER_INFO*>(signerInfoBuffer.data());
        if (CryptMsgGetParam(msg, CMSG_SIGNER_INFO_PARAM, 0, signerInfo, &signerInfoSize)) {
            CERT_INFO certInfo = {};
            certInfo.Issuer = signerInfo->Issuer;
            certInfo.SerialNumber = signerInfo->SerialNumber;
            PCCERT_CONTEXT certContext =
                CertFindCertificateInStore(store, encoding, 0, CERT_FIND_SUBJECT_CERT, &certInfo, nullptr);
            if (certContext) {
                wchar_t nameBuffer[256] = {};
                CertGetNameStringW(certContext, CERT_NAME_SIMPLE_DISPLAY_TYPE, 0, nullptr, nameBuffer,
                                    ARRAYSIZE(nameBuffer));
                std::wstring name(nameBuffer);
                std::transform(name.begin(), name.end(), name.begin(),
                                [](wchar_t c) { return static_cast<wchar_t>(std::towlower(c)); });
                result = name;
                CertFreeCertificateContext(certContext);
            }
        }
    }

    CryptMsgClose(msg);
    CertCloseStore(store, 0);
    return result;
}

} // namespace

bool SignatureVerifier::IsMicrosoftSigned(const std::wstring& exePath) {
    if (exePath.empty()) {
        return false;
    }

    FILETIME currentWriteTime = {};
    bool haveWriteTime = GetFileLastWriteTime(exePath, currentWriteTime);

    auto it = cache_.find(exePath);
    if (it != cache_.end() && haveWriteTime && SameFileTime(it->second.lastWriteTime, currentWriteTime)) {
        return it->second.isMicrosoftSigned;
    }

    bool verdict = false;
    if (VerifyTrust(exePath)) {
        std::optional<std::wstring> signerName = GetSignerDisplayNameLower(exePath);
        if (signerName.has_value()) {
            verdict = signerName->find(L"microsoft windows") != std::wstring::npos ||
                      signerName->find(L"microsoft corporation") != std::wstring::npos;
        }
    }

    if (haveWriteTime) {
        cache_[exePath] = CacheEntry{currentWriteTime, verdict};
    }
    return verdict;
}

} // namespace startup
