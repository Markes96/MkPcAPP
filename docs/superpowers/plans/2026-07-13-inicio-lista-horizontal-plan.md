# Rediseño de "Inicio" a lista horizontal + info/ubicación/eliminar universal — Implementation Plan

> **For agentic workers:** REQUIRED SUB-SKILL: Use superpowers:subagent-driven-development (recommended) or superpowers:executing-plans to implement this plan task-by-task. Steps use checkbox (`- [ ]`) syntax for tracking.

**Goal:** Rediseñar la pestaña "Inicio" de una cuadrícula de tarjetas a una lista horizontal (tabla), y añadir tres utilidades por fila: info de la app, abrir su ubicación, y eliminar (ahora válido para cualquier entrada, con confirmación previa, nunca borra la app real).

**Architecture:** Se mantiene el módulo `src/startup/` existente (`StartupScanner` orquesta, `RegistryStartupControl`/`ShortcutStartupControl` hacen el trabajo de Win32 por fuente). Se generaliza el borrado (ya no solo para entradas añadidas por la app), se amplía `SignatureVerifier` para exponer el nombre del firmante además del veredicto, y se añade un módulo nuevo y pequeño (`AppInfoReader`) para leer tamaño de archivo y metadatos `VERSIONINFO`. La UI (`StartupTab`) pasa de tarjetas (`BeginChild`) a una tabla (`ImGui::BeginTable`), reutilizando el mismo patrón de tabla que ya usa `HardwareMonitorTab` para su lista de ventiladores.

**Tech Stack:** C++20, Win32 API (Registro, `IFileOperation`, `VERSIONINFO`, `ShellExecuteW`), Dear ImGui, CMake.

## Global Constraints

- **No hay framework de tests automatizados en este repo** (proyecto C++ nativo de Windows, solo compilable/ejecutable en Windows — ver `docs/PROJECT_STATUS.md`, sección "Desarrollo desde Linux"). Cada tarea sustituye el ciclo test-rojo/test-verde por: (a) una revisión manual de código con `grep`/lectura completa del archivo, comprobando específicamente gestión de recursos (handles, punteros COM) y que la lógica coincide con lo escrito aquí, y (b) al final del plan, una tarea de prueba real en la máquina Windows del usuario.
- **"Eliminar" nunca borra la app real ni su ejecutable** — solo el valor de Registro `Run` o el propio acceso directo `.lnk` (que es solo un puntero de arranque). Esto aplica a *todas* las entradas, no solo a las añadidas por esta app.
- **Todo dato que falte debe verse explícitamente en la UI** ("No disponible", "Sin firmar", "No se pudo comprobar la firma", "Archivo no encontrado") — nunca un campo en blanco ni una acción que falle en silencio.
- Seguir el patrón de módulos existente: funciones libres namespaced para control de sistema (`RegistryStartupControl`, `ShortcutStartupControl`), diálogos ImGui como clases pequeñas con `OpenForX()`/`Render()` llamado incondicionalmente cada frame (mismo idiom que `AddStartupEntryDialog`/`ProfileEditorDialog`).
- Cualquier `.cpp` nuevo debe añadirse explícitamente a `CMakeLists.txt` (no hay glob).

---

### Task 1: Simplificar `StartupEntry` — quitar `deletable`

**Files:**
- Modify: `src/startup/StartupTypes.h`

**Interfaces:**
- Consumes: nada nuevo.
- Produces: `StartupEntry` ya no tiene el campo `deletable` — cualquier código que lo lea/escriba debe quitarse (tareas 2 y 6 lo hacen).

- [ ] **Step 1: Quitar el campo `deletable` y su comentario**

En `src/startup/StartupTypes.h`, el struct actual termina así:

```cpp
    bool enabled = true;
    // The resolved exe (or shortcut target) didn't exist on disk at scan
    // time -- still shown (never hidden), signature check and icon
    // extraction are skipped for it.
    bool targetMissing = false;
    // True only for entries created by AddManualEntry() during this app
    // session -- only those offer a "Quitar" (delete) button; everything
    // else is disable/enable-only, never delete, per the "no apto para
    // torpes" safety goal.
    bool deletable = false;
};
```

Déjalo así (sin el campo `deletable` ni su comentario):

```cpp
    bool enabled = true;
    // The resolved exe (or shortcut target) didn't exist on disk at scan
    // time -- still shown (never hidden), signature check and icon
    // extraction are skipped for it.
    bool targetMissing = false;
};
```

- [ ] **Step 2: Revisión manual**

```bash
grep -rn "deletable" /repos/MkPCApp/src
```

Esperado en este punto: sigue apareciendo en `StartupScanner.cpp`/`.h` y `StartupTab.cpp` — se limpiará en las tareas 6 y 8. Confirma que ya no aparece en `StartupTypes.h`.

- [ ] **Step 3: Commit**

```bash
git add src/startup/StartupTypes.h
git commit -m "Quitar StartupEntry::deletable: eliminar sera universal"
```

---

### Task 2: Generalizar el borrado de Registro (`RegistryStartupControl`)

**Files:**
- Modify: `src/startup/RegistryStartupControl.h`
- Modify: `src/startup/RegistryStartupControl.cpp`

**Interfaces:**
- Consumes: nada nuevo.
- Produces: `RegistryStartupControl::DeleteRunEntry(HKEY hive, const std::wstring& valueName, bool isWow6432) -> bool` reemplaza a `DeleteUserRunEntry(const std::wstring&)`. Usado por la Task 6.

- [ ] **Step 1: Reemplazar la declaración en el header**

En `src/startup/RegistryStartupControl.h`, sustituye:

```cpp
// Deletes a HKCU Run value (only ever called for entries this session
// marked deletable). Best-effort also removes the matching StartupApproved
// value so no orphaned state blob lingers; that part's failure is ignored.
bool DeleteUserRunEntry(const std::wstring& valueName);
```

por:

```cpp
// Deletes a Run value in the given hive (HKCU or HKLM), valid for ANY
// entry now (not just ones this app added) -- never touches the .exe the
// value points at, only the autostart registration itself. Best-effort
// also removes the matching StartupApproved value so no orphaned state
// blob lingers; that part's failure is ignored. A value that's already
// gone (ERROR_FILE_NOT_FOUND) counts as success: the desired end state
// ("this doesn't autostart") already holds.
bool DeleteRunEntry(HKEY hive, const std::wstring& valueName, bool isWow6432);
```

- [ ] **Step 2: Reemplazar la implementación**

En `src/startup/RegistryStartupControl.cpp`, sustituye la función `DeleteUserRunEntry`:

```cpp
bool DeleteUserRunEntry(const std::wstring& valueName) {
    HKEY runKey;
    if (RegOpenKeyExW(HKEY_CURRENT_USER, kRunKeyPath, 0, KEY_SET_VALUE, &runKey) != ERROR_SUCCESS) {
        return false;
    }
    LSTATUS status = RegDeleteValueW(runKey, valueName.c_str());
    RegCloseKey(runKey);

    // Best-effort cleanup of the matching StartupApproved state, ignored on
    // failure -- an orphaned approved-state blob for a value that no longer
    // exists is harmless (Explorer only consults it alongside a live Run
    // value).
    HKEY approvedKey;
    if (RegOpenKeyExW(HKEY_CURRENT_USER, kApprovedRunKeyPath, 0, KEY_SET_VALUE, &approvedKey) ==
        ERROR_SUCCESS) {
        RegDeleteValueW(approvedKey, valueName.c_str());
        RegCloseKey(approvedKey);
    }

    return status == ERROR_SUCCESS;
}
```

por:

```cpp
bool DeleteRunEntry(HKEY hive, const std::wstring& valueName, bool isWow6432) {
    const wchar_t* runPath = isWow6432 ? kRunKeyPathWow6432 : kRunKeyPath;
    const wchar_t* approvedPath = isWow6432 ? kApprovedRunKeyPathWow6432 : kApprovedRunKeyPath;

    HKEY runKey;
    if (RegOpenKeyExW(hive, runPath, 0, KEY_SET_VALUE, &runKey) != ERROR_SUCCESS) {
        return false;
    }
    LSTATUS status = RegDeleteValueW(runKey, valueName.c_str());
    RegCloseKey(runKey);

    // Best-effort cleanup of the matching StartupApproved state, ignored on
    // failure -- an orphaned approved-state blob for a value that no longer
    // exists is harmless (Explorer only consults it alongside a live Run
    // value).
    HKEY approvedKey;
    if (RegOpenKeyExW(hive, approvedPath, 0, KEY_SET_VALUE, &approvedKey) == ERROR_SUCCESS) {
        RegDeleteValueW(approvedKey, valueName.c_str());
        RegCloseKey(approvedKey);
    }

    return status == ERROR_SUCCESS || status == ERROR_FILE_NOT_FOUND;
}
```

- [ ] **Step 3: Revisión manual**

Lee el archivo completo (`cat -n src/startup/RegistryStartupControl.cpp`) y confirma:
- `RegCloseKey` se llama en toda ruta de salida de `DeleteRunEntry` (incluida la de fallo al abrir `runKey`, que ya retorna antes de abrir nada más).
- No queda ninguna referencia a `DeleteUserRunEntry` en este archivo.

```bash
grep -n "DeleteUserRunEntry\|DeleteRunEntry" /repos/MkPCApp/src/startup/RegistryStartupControl.h /repos/MkPCApp/src/startup/RegistryStartupControl.cpp
```

Esperado: solo apariciones de `DeleteRunEntry`, ninguna de `DeleteUserRunEntry`.

- [ ] **Step 4: Commit**

```bash
git add src/startup/RegistryStartupControl.h src/startup/RegistryStartupControl.cpp
git commit -m "Generalizar borrado de Registro a cualquier hive (HKCU o HKLM)"
```

---

### Task 3: Borrado de accesos directos a la Papelera de reciclaje (`ShortcutStartupControl`)

**Files:**
- Modify: `src/startup/ShortcutStartupControl.h`
- Modify: `src/startup/ShortcutStartupControl.cpp`

**Interfaces:**
- Consumes: `StartupEntry.shortcutFilePath` (ya existente).
- Produces: `ShortcutStartupControl::DeleteToRecycleBin(const StartupEntry& entry) -> bool`. Usado por la Task 6. Requiere COM ya inicializado en el hilo llamante (igual que `ResolveShortcutTarget`).

- [ ] **Step 1: Añadir la declaración al header**

En `src/startup/ShortcutStartupControl.h`, añade tras `SetShortcutEnabled`:

```cpp
// Moves the .lnk between its Startup folder and a "Disabled" subfolder next
// to it (created on demand) -- never deletes the file. Updates
// entry.shortcutFilePath/enabled on success so a caller doesn't have to wait
// for the next rescan to see the change reflected.
bool SetShortcutEnabled(StartupEntry& entry, bool enabled);

// Sends the .lnk to the Recycle Bin (never the shortcut's target exe --
// the shortcut itself is just an autostart pointer, this is the delete
// counterpart to SetShortcutEnabled). Requires COM already initialized on
// this thread. A missing file counts as success: the desired end state
// ("this doesn't autostart") already holds.
bool DeleteToRecycleBin(const StartupEntry& entry);
```

- [ ] **Step 2: Añadir la implementación**

En `src/startup/ShortcutStartupControl.cpp`, añade tras `SetShortcutEnabled` (antes del cierre `} // namespace ShortcutStartupControl`):

```cpp
bool DeleteToRecycleBin(const StartupEntry& entry) {
    if (entry.shortcutFilePath.empty()) {
        return false;
    }
    if (!PathFileExistsW(entry.shortcutFilePath.c_str())) {
        return true; // already gone -- desired end state already holds
    }

    IFileOperation* fileOp = nullptr;
    HRESULT hr = CoCreateInstance(CLSID_FileOperation, nullptr, CLSCTX_ALL, IID_IFileOperation,
                                   reinterpret_cast<void**>(&fileOp));
    if (FAILED(hr) || !fileOp) {
        return false;
    }

    bool succeeded = false;
    // FOF_ALLOWUNDO: send to Recycle Bin, not a permanent delete.
    // FOF_NOCONFIRMATION/FOF_SILENT: this app already showed its own
    // confirmation dialog before calling this -- suppress Explorer's.
    if (SUCCEEDED(fileOp->SetOperationFlags(FOF_ALLOWUNDO | FOF_NOCONFIRMATION | FOF_SILENT))) {
        IShellItem* item = nullptr;
        hr = SHCreateItemFromParsingName(entry.shortcutFilePath.c_str(), nullptr, IID_IShellItem,
                                          reinterpret_cast<void**>(&item));
        if (SUCCEEDED(hr) && item) {
            if (SUCCEEDED(fileOp->DeleteItem(item, nullptr))) {
                succeeded = SUCCEEDED(fileOp->PerformOperations());
            }
            item->Release();
        }
    }
    fileOp->Release();
    return succeeded;
}
```

No hace falta ningún `#include` nuevo: `IFileOperation`/`IShellItem`/`SHCreateItemFromParsingName` ya están disponibles vía `<shobjidl.h>`/`<shlobj.h>`, ya incluidos en este archivo.

- [ ] **Step 3: Revisión manual**

```bash
grep -n "Release()\|CoCreateInstance\|SHCreateItemFromParsingName" /repos/MkPCApp/src/startup/ShortcutStartupControl.cpp
```

Confirma con una lectura completa de la función que **toda** ruta de `fileOp`/`item` obtenida se libera: `fileOp->Release()` se ejecuta siempre que `CoCreateInstance` tuvo éxito (incluso si `SetOperationFlags`/`DeleteItem`/`PerformOperations` fallan), e `item->Release()` se ejecuta siempre que `SHCreateItemFromParsingName` tuvo éxito.

- [ ] **Step 4: Commit**

```bash
git add src/startup/ShortcutStartupControl.h src/startup/ShortcutStartupControl.cpp
git commit -m "Añadir borrado de accesos directos a la Papelera de reciclaje"
```

---

### Task 4: Ampliar `SignatureVerifier` para exponer el firmante

**Files:**
- Modify: `src/startup/SignatureVerifier.h`
- Modify: `src/startup/SignatureVerifier.cpp`

**Interfaces:**
- Consumes: nada nuevo.
- Produces: `enum class SignatureStatus { Trusted, NotSigned, VerificationFailed }`, `struct SignatureInfo { SignatureStatus status; bool isMicrosoftSigned; std::optional<std::wstring> signerName; }`, y `SignatureVerifier::GetSignatureInfo(const std::wstring&) -> SignatureInfo`. `IsMicrosoftSigned` mantiene su firma y comportamiento actuales (usado sin cambios por `StartupScanner::Scan`). Usado por la Task 6/8 (popup de info).

- [ ] **Step 1: Reescribir el header completo**

Sustituye todo el contenido de `src/startup/SignatureVerifier.h` por:

```cpp
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
```

- [ ] **Step 2: Reescribir el `.cpp` completo**

Sustituye todo el contenido de `src/startup/SignatureVerifier.cpp` por:

```cpp
#include "SignatureVerifier.h"
#include <softpub.h>
#include <wintrust.h>
#include <wincrypt.h>
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

// WinVerifyTrust is a state-machine API: dwStateAction selects verify vs.
// close. The second call (WTD_STATEACTION_CLOSE) is required cleanup for the
// trust provider state handle, easy to miss since it isn't a normal
// Release()/Close() call. TRUST_E_NOSIGNATURE is a documented WinVerifyTrust
// status distinct from other failures -- distinguishing it lets callers show
// "Sin firmar" separately from "no se pudo comprobar la firma".
SignatureStatus CheckTrust(const std::wstring& path) {
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

    if (status == ERROR_SUCCESS) {
        return SignatureStatus::Trusted;
    }
    if (status == TRUST_E_NOSIGNATURE) {
        return SignatureStatus::NotSigned;
    }
    return SignatureStatus::VerificationFailed;
}

// Extracts the signer's subject common name (original case) from the
// file's Authenticode signature. Only meaningful to call after
// CheckTrust() has already returned Trusted for this path.
std::optional<std::wstring> GetSignerDisplayName(const std::wstring& path) {
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
                result = std::wstring(nameBuffer);
                CertFreeCertificateContext(certContext);
            }
        }
    }

    CryptMsgClose(msg);
    CertCloseStore(store, 0);
    return result;
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
    entry.status = CheckTrust(exePath);
    if (entry.status == SignatureStatus::Trusted) {
        entry.signerName = GetSignerDisplayName(exePath);
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
```

- [ ] **Step 3: Revisión manual**

```bash
grep -n "GetSignerDisplayNameLower\|GetSignerDisplayName" /repos/MkPCApp/src/startup/SignatureVerifier.cpp
```

Esperado: solo `GetSignerDisplayName` (sin el sufijo `Lower` de la versión anterior) — confirma que ya no queda la versión antigua que devolvía el nombre en minúsculas (ahora se compara en minúsculas dentro de `NameIndicatesMicrosoft`, pero se guarda/devuelve el nombre en su capitalización original).

Confirma también, leyendo la función completa, que `CryptMsgClose(msg)`/`CertCloseStore(store, 0)` se ejecutan en todo camino donde `CryptQueryObject` tuvo éxito (incluido cuando `CertFindCertificateInStore` no encuentra nada).

- [ ] **Step 4: Commit**

```bash
git add src/startup/SignatureVerifier.h src/startup/SignatureVerifier.cpp
git commit -m "Ampliar SignatureVerifier para exponer firmante y distinguir sin-firmar de error"
```

---

### Task 5: Nuevo módulo `AppInfoReader` (tamaño + VERSIONINFO)

**Files:**
- Create: `src/startup/AppInfoReader.h`
- Create: `src/startup/AppInfoReader.cpp`

**Interfaces:**
- Consumes: `platform::WideToUtf8` de `src/platform/StringConvert.h` (ya existente).
- Produces: `struct AppInfo { std::optional<uint64_t> fileSizeBytes; std::optional<std::string> productVersion; std::optional<std::string> fileDescription; }` y `startup::ReadAppInfo(const std::wstring& exePath) -> AppInfo`. Usado por la Task 8 (popup de info).

- [ ] **Step 1: Crear el header**

`src/startup/AppInfoReader.h`:

```cpp
#pragma once
#include <cstdint>
#include <optional>
#include <string>

namespace startup {

struct AppInfo {
    // Empty optional means "couldn't be read" -- callers show "No
    // disponible" rather than leaving a blank field, per the app's
    // degrade-visibly principle.
    std::optional<uint64_t> fileSizeBytes;
    std::optional<std::string> productVersion;
    std::optional<std::string> fileDescription;
};

// Pure Win32 (file attributes + VERSIONINFO resource), no ImGui/D3D
// knowledge -- reviewable independent of rendering, same split as
// IconExtractor. Never fails outright: a missing/malformed VERSIONINFO
// block or an inaccessible file just leaves the corresponding field empty.
AppInfo ReadAppInfo(const std::wstring& exePath);

} // namespace startup
```

- [ ] **Step 2: Crear la implementación**

`src/startup/AppInfoReader.cpp`:

```cpp
#include "AppInfoReader.h"
#include "../platform/StringConvert.h"
#include <windows.h>
#include <cwchar>
#include <vector>

namespace startup {

namespace {

std::optional<uint64_t> ReadFileSize(const std::wstring& path) {
    WIN32_FILE_ATTRIBUTE_DATA data;
    if (!GetFileAttributesExW(path.c_str(), GetFileExInfoStandard, &data)) {
        return std::nullopt;
    }
    ULARGE_INTEGER size;
    size.HighPart = data.nFileSizeHigh;
    size.LowPart = data.nFileSizeLow;
    return size.QuadPart;
}

// Reads a single string value (e.g. "ProductVersion", "FileDescription")
// out of the exe's VERSIONINFO resource for the first language/codepage
// block it finds -- most executables only ship one, and falling back to
// "whichever is first" degrades gracefully instead of requiring an exact
// language match.
std::optional<std::string> ReadVersionString(std::vector<BYTE>& versionData, const wchar_t* stringName) {
    struct LangAndCodepage {
        WORD language;
        WORD codepage;
    };
    LangAndCodepage* translations = nullptr;
    UINT translationsSize = 0;
    if (!VerQueryValueW(versionData.data(), L"\\VarFileInfo\\Translation",
                         reinterpret_cast<void**>(&translations), &translationsSize) ||
        translationsSize < sizeof(LangAndCodepage)) {
        return std::nullopt;
    }

    wchar_t subBlock[64];
    swprintf_s(subBlock, L"\\StringFileInfo\\%04x%04x\\%s", translations[0].language, translations[0].codepage,
               stringName);

    wchar_t* value = nullptr;
    UINT valueSize = 0;
    if (!VerQueryValueW(versionData.data(), subBlock, reinterpret_cast<void**>(&value), &valueSize) ||
        valueSize == 0 || !value) {
        return std::nullopt;
    }

    return platform::WideToUtf8(std::wstring(value));
}

} // namespace

AppInfo ReadAppInfo(const std::wstring& exePath) {
    AppInfo info;
    if (exePath.empty()) {
        return info;
    }

    info.fileSizeBytes = ReadFileSize(exePath);

    DWORD handle = 0;
    DWORD versionSize = GetFileVersionInfoSizeW(exePath.c_str(), &handle);
    if (versionSize == 0) {
        return info; // no VERSIONINFO block -- version/description stay empty
    }

    std::vector<BYTE> versionData(versionSize);
    if (!GetFileVersionInfoW(exePath.c_str(), handle, versionSize, versionData.data())) {
        return info;
    }

    info.productVersion = ReadVersionString(versionData, L"ProductVersion");
    info.fileDescription = ReadVersionString(versionData, L"FileDescription");
    return info;
}

} // namespace startup
```

- [ ] **Step 3: Revisión manual**

```bash
grep -c "VerQueryValueW\|GetFileVersionInfo" /repos/MkPCApp/src/startup/AppInfoReader.cpp
```

Confirma leyendo el archivo que ninguna ruta puede desreferenciar un puntero nulo: `ReadVersionString` comprueba `translationsSize < sizeof(LangAndCodepage)` antes de indexar `translations[0]`, y comprueba `valueSize == 0 || !value` antes de construir el `wstring`.

- [ ] **Step 4: Commit**

```bash
git add src/startup/AppInfoReader.h src/startup/AppInfoReader.cpp
git commit -m "Añadir AppInfoReader: tamaño de archivo y metadatos VERSIONINFO"
```

---

### Task 6: Actualizar `StartupScanner` — eliminar universal + info de firma

**Files:**
- Modify: `src/startup/StartupScanner.h`
- Modify: `src/startup/StartupScanner.cpp`

**Interfaces:**
- Consumes: `RegistryStartupControl::DeleteRunEntry` (Task 2), `ShortcutStartupControl::DeleteToRecycleBin` (Task 3), `SignatureVerifier::GetSignatureInfo`/`SignatureInfo` (Task 4).
- Produces: `StartupScanner::DeleteEntry(const StartupEntry&) -> bool` (reemplaza a `DeleteManualEntry`), `StartupScanner::GetSignatureInfo(const std::wstring&) -> SignatureInfo`. Usados por la Task 8.

- [ ] **Step 1: Reescribir el header completo**

Sustituye todo el contenido de `src/startup/StartupScanner.h` por:

```cpp
#pragma once
#include "StartupTypes.h"
#include "SignatureVerifier.h"
#include "RegistryStartupControl.h"
#include <mutex>
#include <string>
#include <vector>

namespace startup {

struct ScanResult {
    std::vector<StartupEntry> entries;
};

// Orchestrator: enumerates all four sources (HKCU/HKLM Run, both Startup
// folders), applies the Microsoft-signature filter, and dispatches
// enable/disable/add/delete to the right per-source control module. Owns no
// D3D/ImGui state -- icon texture caching lives entirely in
// platform::IconTextureCache, owned by the UI layer instead.
//
// Thread-safe: Scan() is expected to run on StartupTab's OnTick (background
// data-tick thread) while SetEnabled/AddManualEntry/DeleteEntry/
// GetSignatureInfo are called from OnRender (main thread) in response to
// user clicks -- signatureVerifier_'s cache is the only state shared
// between those calls, guarded by mutex_ the same way AutomationEngine
// guards rules_.
class StartupScanner {
public:
    ScanResult Scan();

    // Dispatches to RegistryStartupControl or ShortcutStartupControl based
    // on entry.source. Updates entry in place on success.
    bool SetEnabled(StartupEntry& entry, bool enabled);

    // Always creates a HKCU Run value (never HKLM), per the approved
    // "manual add always goes to the current user" design.
    RegistryStartupControl::AddResult AddManualEntry(const std::wstring& displayName,
                                                       const std::wstring& exePath);

    // Valid for ANY entry, not just ones added through this app --
    // Registry entries have their Run value deleted (never the .exe);
    // Startup-folder shortcuts are sent to the Recycle Bin (never the
    // target .exe). A "not found" outcome (already deleted by something
    // else) counts as success: the desired end state already holds.
    bool DeleteEntry(const StartupEntry& entry);

    // Used by the "info" popup. Thread-safe wrapper around
    // SignatureVerifier::GetSignatureInfo -- shares the same cache Scan()
    // populates via IsMicrosoftSigned.
    SignatureInfo GetSignatureInfo(const std::wstring& exePath);

private:
    std::mutex mutex_; // guards signatureVerifier_ only
    SignatureVerifier signatureVerifier_;
};

} // namespace startup
```

- [ ] **Step 2: Reescribir el `.cpp` completo**

Sustituye todo el contenido de `src/startup/StartupScanner.cpp` por:

```cpp
#include "StartupScanner.h"
#include "ShortcutStartupControl.h"
#include "../platform/ComScope.h"
#include "../platform/StringConvert.h"

namespace startup {

namespace {

using platform::ComScope;
using platform::Utf8ToWide;

std::wstring QuoteIfNeeded(const std::wstring& path) {
    if (path.empty() || path.front() == L'"') {
        return path;
    }
    if (path.find(L' ') != std::wstring::npos) {
        return L"\"" + path + L"\"";
    }
    return path;
}

} // namespace

ScanResult StartupScanner::Scan() {
    ScanResult result;
    // ShortcutStartupControl needs COM (IShellLink) for the whole scan, not
    // per-call -- bracket it once here rather than per shortcut resolved.
    ComScope comScope;

    std::vector<StartupEntry> all = RegistryStartupControl::EnumerateHkcuRun();
    std::vector<StartupEntry> hklm = RegistryStartupControl::EnumerateHklmRun();
    std::vector<StartupEntry> userFolder = ShortcutStartupControl::EnumerateUserStartupFolder();
    std::vector<StartupEntry> commonFolder = ShortcutStartupControl::EnumerateCommonStartupFolder();
    all.insert(all.end(), hklm.begin(), hklm.end());
    all.insert(all.end(), userFolder.begin(), userFolder.end());
    all.insert(all.end(), commonFolder.begin(), commonFolder.end());

    // signatureVerifier_ is the only state shared with the main-thread
    // AddManualEntry/DeleteEntry/GetSignatureInfo calls -- lock only around
    // touching it, not around the registry/filesystem enumeration itself
    // (this function's slow part, which doesn't touch shared state).
    std::lock_guard<std::mutex> lock(mutex_);
    for (auto& entry : all) {
        bool isMicrosoft = false;
        if (!entry.targetMissing && !entry.resolvedExePath.empty()) {
            isMicrosoft = signatureVerifier_.IsMicrosoftSigned(Utf8ToWide(entry.resolvedExePath));
        }
        if (isMicrosoft) {
            continue; // excluded entirely, never just greyed out
        }
        result.entries.push_back(std::move(entry));
    }

    return result;
}

bool StartupScanner::SetEnabled(StartupEntry& entry, bool enabled) {
    bool succeeded = false;
    switch (entry.source) {
        case StartupSource::RegistryHkcuRun:
            succeeded = RegistryStartupControl::SetApprovedEnabled(HKEY_CURRENT_USER, entry.registryValueName,
                                                                      entry.isWow6432, enabled);
            break;
        case StartupSource::RegistryHklmRun:
            succeeded = RegistryStartupControl::SetApprovedEnabled(HKEY_LOCAL_MACHINE, entry.registryValueName,
                                                                      entry.isWow6432, enabled);
            break;
        case StartupSource::StartupFolderUser:
        case StartupSource::StartupFolderCommon:
            // SetShortcutEnabled updates entry.enabled itself on success.
            return ShortcutStartupControl::SetShortcutEnabled(entry, enabled);
    }
    if (succeeded) {
        entry.enabled = enabled;
    }
    return succeeded;
}

RegistryStartupControl::AddResult StartupScanner::AddManualEntry(const std::wstring& displayName,
                                                                    const std::wstring& exePath) {
    return RegistryStartupControl::AddUserRunEntry(displayName, QuoteIfNeeded(exePath));
}

bool StartupScanner::DeleteEntry(const StartupEntry& entry) {
    switch (entry.source) {
        case StartupSource::RegistryHkcuRun:
            return RegistryStartupControl::DeleteRunEntry(HKEY_CURRENT_USER, entry.registryValueName,
                                                             entry.isWow6432);
        case StartupSource::RegistryHklmRun:
            return RegistryStartupControl::DeleteRunEntry(HKEY_LOCAL_MACHINE, entry.registryValueName,
                                                             entry.isWow6432);
        case StartupSource::StartupFolderUser:
        case StartupSource::StartupFolderCommon: {
            // Recycle-bin deletion needs COM (IFileOperation), bracketed
            // locally since this is a rare, user-triggered one-off action,
            // not part of Scan()'s per-cycle ComScope pairing.
            ComScope comScope;
            return ShortcutStartupControl::DeleteToRecycleBin(entry);
        }
    }
    return false;
}

SignatureInfo StartupScanner::GetSignatureInfo(const std::wstring& exePath) {
    std::lock_guard<std::mutex> lock(mutex_);
    return signatureVerifier_.GetSignatureInfo(exePath);
}

} // namespace startup
```

- [ ] **Step 3: Revisión manual**

```bash
grep -n "manuallyAddedValueNames_\|DeleteManualEntry\|deletable" /repos/MkPCApp/src/startup/StartupScanner.h /repos/MkPCApp/src/startup/StartupScanner.cpp
```

Esperado: ningún resultado (todo lo relativo a "añadido manualmente" ha desaparecido).

- [ ] **Step 4: Commit**

```bash
git add src/startup/StartupScanner.h src/startup/StartupScanner.cpp
git commit -m "StartupScanner: eliminar es universal, añadir GetSignatureInfo"
```

---

### Task 7: Diálogo de confirmación de borrado (`ConfirmDeleteDialog`)

**Files:**
- Create: `src/ui/ConfirmDeleteDialog.h`
- Create: `src/ui/ConfirmDeleteDialog.cpp`

**Interfaces:**
- Consumes: `startup::StartupEntry` (Task 1), `startup::StartupScanner::DeleteEntry` (Task 6).
- Produces: `ui::ConfirmDeleteDialog` con `OpenForEntry(const startup::StartupEntry&)`, `Render(startup::StartupScanner&)`, `ConsumeJustDeleted() -> bool`. Usado por la Task 8.

- [ ] **Step 1: Crear el header**

`src/ui/ConfirmDeleteDialog.h`:

```cpp
#pragma once
#include "../startup/StartupTypes.h"
#include "../startup/StartupScanner.h"
#include <string>

namespace ui {

// Confirmation modal shown before deleting ANY startup entry (registry
// value or shortcut) -- deleting is irreversible from within the app for
// registry values (shortcut deletes land in the Recycle Bin, but still
// worth the same confirmation for consistency). Same idiom as
// AddStartupEntryDialog: OpenForEntry() captures its own copy of the
// entry (not a live reference -- the underlying list can mutate between
// opening this dialog and the user confirming), Render() is called
// unconditionally every frame.
class ConfirmDeleteDialog {
public:
    void OpenForEntry(const startup::StartupEntry& entry);
    void Render(startup::StartupScanner& scanner);

    // Returns true exactly once, right after a successful delete, then
    // resets -- lets StartupTab trigger an immediate rescan.
    bool ConsumeJustDeleted();

private:
    bool isOpen_ = false;
    bool openRequested_ = false;
    bool justDeleted_ = false;
    startup::StartupEntry pendingEntry_;
    std::string errorMessage_;
};

} // namespace ui
```

- [ ] **Step 2: Crear la implementación**

`src/ui/ConfirmDeleteDialog.cpp`:

```cpp
#include "ConfirmDeleteDialog.h"
#include <imgui.h>

namespace ui {

namespace {
constexpr const char* kPopupId = "Eliminar del inicio###ConfirmDeleteDialog";
} // namespace

void ConfirmDeleteDialog::OpenForEntry(const startup::StartupEntry& entry) {
    pendingEntry_ = entry;
    errorMessage_.clear();
    isOpen_ = true;
    openRequested_ = true;
}

void ConfirmDeleteDialog::Render(startup::StartupScanner& scanner) {
    if (openRequested_) {
        openRequested_ = false;
        ImGui::OpenPopup(kPopupId);
    }

    if (!isOpen_) {
        return;
    }

    ImGui::SetNextWindowSize(ImVec2(380.0f, 0.0f), ImGuiCond_Appearing);
    if (!ImGui::BeginPopupModal(kPopupId, nullptr, ImGuiWindowFlags_AlwaysAutoResize)) {
        return;
    }

    ImGui::TextWrapped("Seguro que quieres eliminar \"%s\" del inicio de Windows?",
                        pendingEntry_.displayName.c_str());
    ImGui::TextWrapped("Esta accion no se puede deshacer desde la app.");

    if (!errorMessage_.empty()) {
        ImGui::Dummy(ImVec2(0.0f, 8.0f));
        ImGui::TextColored(ImVec4(0.9f, 0.4f, 0.4f, 1.0f), "%s", errorMessage_.c_str());
    }

    ImGui::Dummy(ImVec2(0.0f, 8.0f));
    ImGui::Separator();
    ImGui::Dummy(ImVec2(0.0f, 4.0f));

    if (ImGui::Button("Eliminar")) {
        if (scanner.DeleteEntry(pendingEntry_)) {
            isOpen_ = false;
            justDeleted_ = true;
            ImGui::CloseCurrentPopup();
        } else {
            errorMessage_ = "No se pudo eliminar \"" + pendingEntry_.displayName + "\".";
        }
    }
    ImGui::SameLine();
    if (ImGui::Button("Cancelar")) {
        isOpen_ = false;
        ImGui::CloseCurrentPopup();
    }

    ImGui::EndPopup();
}

bool ConfirmDeleteDialog::ConsumeJustDeleted() {
    bool result = justDeleted_;
    justDeleted_ = false;
    return result;
}

} // namespace ui
```

- [ ] **Step 3: Revisión manual**

```bash
grep -n "pendingEntry_\|DeleteEntry" /repos/MkPCApp/src/ui/ConfirmDeleteDialog.cpp
```

Confirma que `pendingEntry_` es una copia por valor (campo del struct, no puntero/referencia) fijada en `OpenForEntry`, y que el error no cierra el popup (el usuario puede reintentar o cancelar tras ver el mensaje).

- [ ] **Step 4: Commit**

```bash
git add src/ui/ConfirmDeleteDialog.h src/ui/ConfirmDeleteDialog.cpp
git commit -m "Añadir dialogo de confirmacion antes de eliminar una entrada de inicio"
```

---

### Task 8: Rediseñar `StartupTab` — tabla + info + abrir ubicación + eliminar

**Files:**
- Modify: `src/ui/StartupTab.h`
- Modify: `src/ui/StartupTab.cpp`

**Interfaces:**
- Consumes: todo lo de las tareas 1-7 (`StartupScanner::DeleteEntry`/`GetSignatureInfo`, `SignatureInfo`/`SignatureStatus`, `AppInfo`/`ReadAppInfo`, `ConfirmDeleteDialog`, `platform::WideToUtf8`/`Utf8ToWide`).
- Produces: nada nuevo consumido por otras tareas (es la hoja del árbol de dependencias).

- [ ] **Step 1: Reescribir el header completo**

Sustituye todo el contenido de `src/ui/StartupTab.h` por:

```cpp
#pragma once
#include "ITab.h"
#include "AddStartupEntryDialog.h"
#include "ConfirmDeleteDialog.h"
#include "../startup/StartupScanner.h"
#include "../startup/AppInfoReader.h"
#include "../platform/IconTextureCache.h"
#include "../platform/DX11Renderer.h"
#include <windows.h>
#include <atomic>
#include <cstdint>
#include <mutex>
#include <string>
#include <unordered_map>

namespace ui {

// The "Inicio" section: lists third-party apps registered to launch with
// Windows (Registry Run under HKCU/HKLM, plus both Startup-folder shortcut
// locations) as a horizontal list, one row per entry. Lets the user
// enable/disable them (reversible), see extra info (path/signer/size/
// version) via a popup, open the executable's containing folder, add a new
// one manually, and delete ANY entry (registry value or shortcut -- never
// the real app, always behind a confirmation dialog). Apps signed by
// Microsoft are excluded entirely by startup::StartupScanner before this
// tab ever sees them.
class StartupTab : public ITab {
public:
    StartupTab(HWND hwnd, platform::DX11Renderer& renderer);

    const char* GetTitle() const override { return "Inicio"; }
    const char* GetIcon() const override { return "I"; }
    void OnRender(float deltaTimeSeconds) override;
    void OnTick(uint64_t tickMs) override;

private:
    struct EntryInfoCache {
        startup::AppInfo appInfo;
        startup::SignatureInfo signatureInfo;
    };

    void RenderEntryRow(startup::StartupEntry& entry);
    void RenderInfoPopup(const startup::StartupEntry& entry);
    void RenderInfoContent(const startup::StartupEntry& entry);
    void OpenContainingFolder(const startup::StartupEntry& entry);

    HWND hwnd_;
    startup::StartupScanner scanner_; // internally thread-safe, see StartupScanner.h
    platform::IconTextureCache iconCache_; // only ever touched from OnRender (render thread)

    std::mutex scanMutex_; // guards lastScan_ between OnTick (background) and OnRender (main thread)
    startup::ScanResult lastScan_;
    std::atomic<bool> hasScannedOnce_{false};
    uint64_t tickCounter_ = 0;

    std::string lastErrorMessage_;
    ui::AddStartupEntryDialog addDialog_;
    ui::ConfirmDeleteDialog confirmDeleteDialog_;

    // Info popup state: which entry's popup is open (empty = none), and a
    // cache of already-read app info/signature data keyed by resolved exe
    // path -- computed once when the "i" button is clicked, not every
    // frame, since VERSIONINFO/Authenticode reads are real file I/O.
    std::string infoPopupEntryId_;
    std::unordered_map<std::string, EntryInfoCache> infoCache_;
};

} // namespace ui
```

- [ ] **Step 2: Reescribir el `.cpp` completo**

Sustituye todo el contenido de `src/ui/StartupTab.cpp` por:

```cpp
#include "StartupTab.h"
#include "Theme.h"
#include "CardWidgets.h"
#include "../platform/StringConvert.h"
#include <imgui.h>
#include <shellapi.h>
#include <mutex>

namespace ui {

namespace {

constexpr float kIconSize = 32.0f;
// Full rescan every 10 ticks (~10s at the app's 1Hz data-tick rate) rather
// than every tick -- registry/filesystem enumeration plus signature checks
// don't need to run every second. Tune if this is noticeable on real
// hardware with many startup entries.
constexpr uint64_t kRescanEveryNTicks = 10;

const char* SourceBadgeText(startup::StartupSource source) {
    switch (source) {
        case startup::StartupSource::RegistryHkcuRun:
            return "Registro (tu usuario)";
        case startup::StartupSource::RegistryHklmRun:
            return "Registro (todos los usuarios)";
        case startup::StartupSource::StartupFolderUser:
            return "Carpeta Inicio";
        case startup::StartupSource::StartupFolderCommon:
            return "Carpeta Inicio (todos)";
    }
    return "";
}

// Drawn instead of ImGui::Image() when icon extraction failed or the
// entry's target is missing -- degrade visibly rather than showing nothing
// or a broken image, same principle the hardware monitor uses for missing
// sensors.
void RenderIconPlaceholder() {
    ImGui::Dummy(ImVec2(kIconSize, kIconSize));
    ImVec2 boxMin = ImGui::GetItemRectMin();
    ImVec2 boxMax = ImGui::GetItemRectMax();
    ImDrawList* drawList = ImGui::GetWindowDrawList();
    drawList->AddRect(boxMin, boxMax, ImGui::GetColorU32(ImGuiCol_Border));
    ImVec2 textSize = ImGui::CalcTextSize("?");
    ImVec2 textPos(boxMin.x + (kIconSize - textSize.x) * 0.5f, boxMin.y + (kIconSize - textSize.y) * 0.5f);
    drawList->AddText(textPos, ImGui::GetColorU32(ImGuiCol_TextDisabled), "?");
}

} // namespace

StartupTab::StartupTab(HWND hwnd, platform::DX11Renderer& renderer) : hwnd_(hwnd), iconCache_(renderer) {}

void StartupTab::OnTick(uint64_t /*tickMs*/) {
    ++tickCounter_;
    if (tickCounter_ % kRescanEveryNTicks != 1) {
        return;
    }

    // Registry/filesystem enumeration + signature checks (no D3D calls) --
    // safe to run on this background thread. Texture creation stays in
    // OnRender only.
    startup::ScanResult freshScan = scanner_.Scan();
    {
        std::lock_guard<std::mutex> lock(scanMutex_);
        lastScan_ = std::move(freshScan);
    }
    hasScannedOnce_.store(true);
}

void StartupTab::OnRender(float /*deltaTimeSeconds*/) {
    if (RenderSectionHeaderAddButton("INICIO")) {
        addDialog_.OpenForCreate();
    }
    ImGui::Dummy(ImVec2(0.0f, 8.0f));

    {
        // Held for the whole table loop rather than copied out and back:
        // OnTick only needs this lock briefly, once every ~10 ticks, to
        // swap in a freshly scanned result, so a render-thread hold for one
        // frame is cheap contention, not a bottleneck.
        std::lock_guard<std::mutex> lock(scanMutex_);

        if (!hasScannedOnce_.load()) {
            ImGui::TextDisabled("Buscando programas de inicio...");
        } else if (lastScan_.entries.empty()) {
            ImGui::TextDisabled("No se ha encontrado ningun programa de terceros en el inicio de Windows.");
        } else {
            constexpr ImGuiTableFlags kTableFlags = ImGuiTableFlags_Borders | ImGuiTableFlags_RowBg |
                                                     ImGuiTableFlags_SizingStretchProp;
            if (ImGui::BeginTable("StartupEntriesTable", 5, kTableFlags)) {
                ImGui::TableSetupColumn("", ImGuiTableColumnFlags_WidthFixed, kIconSize + 8.0f);
                ImGui::TableSetupColumn("Nombre");
                ImGui::TableSetupColumn("Origen", ImGuiTableColumnFlags_WidthFixed, 170.0f);
                ImGui::TableSetupColumn("Activado", ImGuiTableColumnFlags_WidthFixed, 70.0f);
                ImGui::TableSetupColumn("Acciones", ImGuiTableColumnFlags_WidthFixed, 240.0f);
                ImGui::TableHeadersRow();

                for (auto& entry : lastScan_.entries) {
                    RenderEntryRow(entry);
                }

                ImGui::EndTable();
            }
        }
    }

    if (!lastErrorMessage_.empty()) {
        ImGui::Dummy(ImVec2(0.0f, 8.0f));
        ImGui::TextDisabled("%s", lastErrorMessage_.c_str());
    }

    addDialog_.Render(scanner_);
    confirmDeleteDialog_.Render(scanner_);

    if (addDialog_.ConsumeJustSaved() || confirmDeleteDialog_.ConsumeJustDeleted()) {
        // Immediate rescan (one-off, user-triggered, off the hot path) so
        // an add/delete shows up right away instead of waiting up to
        // kRescanEveryNTicks ticks for the next background rescan.
        startup::ScanResult freshScan = scanner_.Scan();
        std::lock_guard<std::mutex> lock(scanMutex_);
        lastScan_ = std::move(freshScan);
        hasScannedOnce_.store(true);
    }
}

void StartupTab::RenderEntryRow(startup::StartupEntry& entry) {
    ImGui::PushID(entry.id.c_str());
    ImGui::TableNextRow();

    bool hasTarget = !entry.targetMissing && !entry.resolvedExePath.empty();

    ImGui::TableSetColumnIndex(0);
    uint64_t textureId = hasTarget ? iconCache_.GetOrCreateTexture(entry.resolvedExePath) : 0;
    if (textureId != 0) {
        ImGui::Image(textureId, ImVec2(kIconSize, kIconSize));
    } else {
        RenderIconPlaceholder();
    }

    ImGui::TableSetColumnIndex(1);
    ImGui::AlignTextToFramePadding();
    ImGui::TextUnformatted(entry.displayName.c_str());
    if (!hasTarget) {
        ImGui::SameLine();
        ImGui::TextDisabled("(archivo no encontrado)");
    }

    ImGui::TableSetColumnIndex(2);
    ImGui::AlignTextToFramePadding();
    ImGui::TextDisabled("%s", SourceBadgeText(entry.source));

    ImGui::TableSetColumnIndex(3);
    bool enabledState = entry.enabled;
    if (ImGui::Checkbox("##enabled", &enabledState)) {
        if (scanner_.SetEnabled(entry, enabledState)) {
            lastErrorMessage_.clear();
        } else {
            lastErrorMessage_ = "No se pudo cambiar el estado de \"" + entry.displayName + "\".";
        }
    }

    ImGui::TableSetColumnIndex(4);

    if (ImGui::SmallButton("i")) {
        infoPopupEntryId_ = entry.id;
        ImGui::OpenPopup("EntryInfoPopup");
        if (hasTarget && infoCache_.find(entry.resolvedExePath) == infoCache_.end()) {
            std::wstring widePath = platform::Utf8ToWide(entry.resolvedExePath);
            EntryInfoCache cacheValue;
            cacheValue.appInfo = startup::ReadAppInfo(widePath);
            cacheValue.signatureInfo = scanner_.GetSignatureInfo(widePath);
            infoCache_[entry.resolvedExePath] = std::move(cacheValue);
        }
    }
    if (infoPopupEntryId_ == entry.id) {
        RenderInfoPopup(entry);
    }

    ImGui::SameLine();
    ImGui::BeginDisabled(!hasTarget);
    if (ImGui::SmallButton("Abrir ubicacion")) {
        OpenContainingFolder(entry);
    }
    ImGui::EndDisabled();
    if (!hasTarget && ImGui::IsItemHovered(ImGuiHoveredFlags_AllowWhenDisabled)) {
        ImGui::SetTooltip("El archivo ya no existe.");
    }

    ImGui::SameLine();
    if (ImGui::SmallButton("Eliminar")) {
        confirmDeleteDialog_.OpenForEntry(entry);
    }

    ImGui::PopID();
}

void StartupTab::RenderInfoPopup(const startup::StartupEntry& entry) {
    if (ImGui::BeginPopup("EntryInfoPopup")) {
        RenderInfoContent(entry);
        ImGui::EndPopup();
    } else {
        infoPopupEntryId_.clear(); // closed (e.g. clicked outside) -- stop tracking
    }
}

void StartupTab::RenderInfoContent(const startup::StartupEntry& entry) {
    bool hasTarget = !entry.targetMissing && !entry.resolvedExePath.empty();

    ImGui::TextUnformatted("Ruta:");
    ImGui::SameLine();
    ImGui::TextWrapped("%s", hasTarget ? entry.resolvedExePath.c_str() : "Archivo no encontrado");

    if (!hasTarget) {
        return;
    }

    auto it = infoCache_.find(entry.resolvedExePath);
    if (it == infoCache_.end()) {
        ImGui::TextDisabled("No disponible");
        return;
    }
    const startup::AppInfo& appInfo = it->second.appInfo;
    const startup::SignatureInfo& sigInfo = it->second.signatureInfo;

    std::string signerText;
    switch (sigInfo.status) {
        case startup::SignatureStatus::Trusted:
            signerText = sigInfo.signerName.has_value() ? platform::WideToUtf8(*sigInfo.signerName)
                                                          : "(nombre no disponible)";
            break;
        case startup::SignatureStatus::NotSigned:
            signerText = "Sin firmar";
            break;
        case startup::SignatureStatus::VerificationFailed:
            signerText = "No se pudo comprobar la firma";
            break;
    }
    ImGui::Text("Editor: %s", signerText.c_str());

    if (appInfo.fileSizeBytes.has_value()) {
        double megabytes = static_cast<double>(*appInfo.fileSizeBytes) / (1024.0 * 1024.0);
        ImGui::Text("Tamano: %.2f MB", megabytes);
    } else {
        ImGui::TextUnformatted("Tamano: No disponible");
    }

    ImGui::Text("Version: %s", appInfo.productVersion.value_or("No disponible").c_str());
    ImGui::Text("Descripcion: %s", appInfo.fileDescription.value_or("No disponible").c_str());
}

void StartupTab::OpenContainingFolder(const startup::StartupEntry& entry) {
    std::wstring path = platform::Utf8ToWide(entry.resolvedExePath);
    std::wstring args = L"/select,\"" + path + L"\"";
    HINSTANCE result = ShellExecuteW(nullptr, L"open", L"explorer.exe", args.c_str(), nullptr, SW_SHOWNORMAL);
    if (reinterpret_cast<intptr_t>(result) <= 32) {
        lastErrorMessage_ = "No se pudo abrir la ubicacion de \"" + entry.displayName + "\".";
    } else {
        lastErrorMessage_.clear();
    }
}

} // namespace ui
```

Nota: `SignatureInfo::signerName` es `std::wstring`; se convierte a UTF-8 con `platform::WideToUtf8` justo antes de mostrarlo (ImGui trabaja en UTF-8/ASCII, igual que el resto de la app).

- [ ] **Step 3: Revisión manual**

```bash
grep -n "kCardWidth\|kCardHeight\|RenderEntryCard\|BeginChild(entry" /repos/MkPCApp/src/ui/StartupTab.cpp
```

Esperado: ningún resultado (ya no quedan restos de la cuadrícula de tarjetas). Confirma también:
- `iconCache_.GetOrCreateTexture` solo se llama dentro de `RenderEntryRow`, que solo se invoca desde dentro del bloque bajo `scanMutex_` en `OnRender` (nunca desde `OnTick`).
- `infoPopupEntryId_`/`infoCache_` se leen/escriben solo desde el hilo de render (no hay acceso desde `OnTick`), así que no necesitan mutex propio.

- [ ] **Step 4: Commit**

```bash
git add src/ui/StartupTab.h src/ui/StartupTab.cpp
git commit -m "Rediseñar StartupTab: lista horizontal + info/abrir ubicacion/eliminar"
```

---

### Task 9: `CMakeLists.txt` — nuevos archivos + `version.lib`

**Files:**
- Modify: `CMakeLists.txt`

**Interfaces:**
- Consumes: rutas de archivo creadas en las tareas 5 y 7.
- Produces: build actualizado (verificable solo en la máquina Windows del usuario).

- [ ] **Step 1: Añadir los nuevos `.cpp` a `APP_SOURCES`**

En `CMakeLists.txt`, dentro de `set(APP_SOURCES ...)`, la sección `src/startup/...` queda así (añadida `AppInfoReader.cpp`):

```
    src/startup/RegistryStartupControl.cpp
    src/startup/ShortcutStartupControl.cpp
    src/startup/SignatureVerifier.cpp
    src/startup/IconExtractor.cpp
    src/startup/AppInfoReader.cpp
    src/startup/StartupScanner.cpp
```

Y la sección `src/ui/...` queda así (añadido `ConfirmDeleteDialog.cpp`):

```
    src/ui/CardWidgets.cpp
    src/ui/StartupTab.cpp
    src/ui/AddStartupEntryDialog.cpp
    src/ui/ConfirmDeleteDialog.cpp
```

- [ ] **Step 2: Enlazar `version.lib`**

La línea `target_link_libraries(MkPCApp PRIVATE ...)` pasa de:

```cmake
target_link_libraries(MkPCApp PRIVATE imgui implot d3d11 dxgi shell32 iphlpapi advapi32
                                        PowrProf dxva2 ole32 oleaut32 wbemuuid
                                        wintrust crypt32 shlwapi gdi32 uuid)
```

a (añadido `version` al final, necesario para `GetFileVersionInfoSizeW`/`GetFileVersionInfoW`/`VerQueryValueW`):

```cmake
target_link_libraries(MkPCApp PRIVATE imgui implot d3d11 dxgi shell32 iphlpapi advapi32
                                        PowrProf dxva2 ole32 oleaut32 wbemuuid
                                        wintrust crypt32 shlwapi gdi32 uuid version)
```

- [ ] **Step 3: Revisión manual**

```bash
grep -n "AppInfoReader\|ConfirmDeleteDialog\|version" /repos/MkPCApp/CMakeLists.txt
```

Confirma que ambos `.cpp` nuevos aparecen en `APP_SOURCES` y que `version` aparece en `target_link_libraries`.

- [ ] **Step 4: Commit**

```bash
git add CMakeLists.txt
git commit -m "CMakeLists: añadir AppInfoReader/ConfirmDeleteDialog y enlazar version.lib"
```

---

### Task 10: Actualizar documentación

**Files:**
- Modify: `docs/PROJECT_STATUS.md`
- Modify: `docs/ARCHITECTURE.md`

**Interfaces:**
- Consumes: el estado final de las tareas 1-9.
- Produces: documentación coherente con el nuevo diseño (sin narrativa de "antes vs. ahora", solo el estado actual, por la regla de mantenimiento del propio `PROJECT_STATUS.md`).

- [ ] **Step 1: Reescribir la sección "Iteración 3" de `docs/PROJECT_STATUS.md`**

Busca la sección `## Iteración 3 — Sección "Inicio": gestor de programas de arranque` (añadida en la iteración anterior) y sustituye su contenido completo por:

```markdown
## Iteración 3 — Sección "Inicio": gestor de programas de arranque (en desarrollo, rama `feature/gestor-inicio`)

Añade una tercera sección (`ui::StartupTab`, registrada junto a
`HardwareMonitorTab` y `PerfilesTab` sin tocar el resto del shell) para ver,
activar/desactivar, eliminar y añadir manualmente programas de terceros que
arrancan con Windows.

### Qué hace

- **Fuentes enumeradas**: `Run` del Registro en HKCU y HKLM (incluyendo el
  espejo `WOW6432Node` en Windows de 64 bits), más accesos directos en la
  carpeta "Inicio" del usuario actual y en la de todos los usuarios. Fuera de
  alcance deliberadamente: Tareas Programadas.
- **Lista horizontal**: una fila por entrada (icono, nombre, origen,
  activar/desactivar, acciones), en vez de una cuadrícula de tarjetas.
- **Activar/desactivar** reversible: para el Registro, se marca el flag de
  estado en `...\Explorer\StartupApproved\Run` (mismo formato de bytes que
  usa el propio Explorer/Task Manager) sin tocar el valor `Run`; para
  accesos directos, se mueve el `.lnk` a una subcarpeta `Disabled` junto a
  la carpeta "Inicio" en vez de borrarlo.
- **Eliminar, para cualquier entrada** (no solo las añadidas desde esta
  app), siempre tras un diálogo de confirmación: nunca borra la app real,
  solo el registro que la hace arrancar. Para el Registro, borra el valor
  `Run` (+ limpieza best-effort del `StartupApproved` a juego). Para
  accesos directos, envía el `.lnk` a la Papelera de reciclaje de Windows
  (recuperable), nunca al ejecutable al que apunta.
- **Info de la app** (botón "i"): ruta completa, editor/firmante (o "Sin
  firmar"/"No se pudo comprobar la firma" si no aplica), tamaño del
  archivo, y versión/descripción leídas del bloque `VERSIONINFO` del
  `.exe`. Cualquier dato no disponible se muestra explícitamente como "No
  disponible" en vez de dejarse en blanco.
- **Abrir ubicación**: abre el Explorador de Windows con el ejecutable ya
  seleccionado; el botón se deshabilita (con explicación al pasar el
  ratón) si el archivo ya no existe, en vez de fallar tras el clic.
- **Filtro de apps de Microsoft**: cualquier ejecutable firmado por
  Microsoft (comprobado vía `WinVerifyTrust`/Authenticode) se excluye por
  completo de la lista. Cualquier fallo de verificación se trata como "no
  es de Microsoft" y la entrada se muestra, para no arriesgarse a ocultar
  algo por error.
- **Icono real por tarjeta**, extraído del propio `.exe` (o del destino
  resuelto del acceso directo); si la extracción falla o el archivo de
  destino ya no existe, se muestra un placeholder.
- **Añadir manualmente**: botón "+" abre un selector de archivo nativo
  filtrado a `.exe`; la entrada nueva siempre se crea en
  `HKCU\...\Run` (nunca en HKLM), con nombre editable.
- Rescan completo cada ~10 segundos (no cada segundo); un alta o baja
  manual dispara un rescan inmediato en vez de esperar al siguiente ciclo.

### Limitaciones conocidas / mejor esfuerzo

- **Sin persistencia JSON nueva**: el Registro/sistema de archivos ya es la
  fuente de verdad de qué arranca con Windows; las cachés de firma
  (`SignatureVerifier`), de info de app (`AppInfoReader`, cacheada en la UI
  por ruta) y de textura de icono (`platform::IconTextureCache`) viven solo
  en memoria y se reconstruyen en cada arranque de la app.

### Arquitectura (resumen)

Módulo `src/startup/` (tipos puros en `StartupTypes.h`, control de Registro
en `RegistryStartupControl`, control de accesos directos en
`ShortcutStartupControl`, verificación de firma en `SignatureVerifier`,
extracción de icono en `IconExtractor`, lectura de metadatos de app en
`AppInfoReader`, orquestación en `StartupScanner`) más `src/ui/StartupTab`
(+ `AddStartupEntryDialog`, `ConfirmDeleteDialog`) y
`src/platform/IconTextureCache`, siguiendo el mismo patrón de extensión por
`ITab` que ya usan Hardware Monitor y Perfiles. El escaneo completo (Registro
+ carpetas + verificación de firma) corre en el hilo de datos a 1 Hz ya
existente, cada ~10 ticks; la creación de texturas de icono, y la lectura de
info de app/firma para el popup, ocurren solo en el hilo de render.

### Verificación pendiente (requiere máquina Windows real)

Aún sin probar en la máquina real del usuario: compilación y enlazado con
las nuevas librerías COM/`wintrust`/`crypt32`/`gdi32`/`version`; que
`WinVerifyTrust` identifica correctamente binarios reales firmados por
Microsoft y los excluye; ida y vuelta con el Administrador de tareas de
Windows al deshabilitar/habilitar una entrada; que eliminar un acceso
directo lo manda a la Papelera de reciclaje (verificable abriéndola) y que
eliminar un valor de Registro de HKLM funciona estando elevado; que "Abrir
ubicación" selecciona el archivo correcto en el Explorador; y que el popup
de info muestra datos correctos (incluyendo casos sin versión/sin firma).
```

- [ ] **Step 2: Actualizar `docs/ARCHITECTURE.md`**

Busca la sección `## Startup module (\`src/startup/\`)` y, dentro de ella,
sustituye el párrafo que empieza por "**Icon rendering.**" añadiendo un
párrafo nuevo justo después (antes del cierre de la sección/inicio de la
siguiente sección de nivel `##`):

```markdown
**App info / eliminar universal.** `startup::AppInfoReader::ReadAppInfo`
lee el tamaño de archivo y el bloque `VERSIONINFO` (versión de producto,
descripción) de un `.exe`, igual de puro/sin estado que `IconExtractor`.
`SignatureVerifier::GetSignatureInfo` comparte la misma caché por
`(ruta, fecha de modificación)` que `IsMicrosoftSigned`, pero además expone
el nombre real del firmante y distingue "sin firmar" (`TRUST_E_NOSIGNATURE`)
de "no se pudo comprobar la firma" (cualquier otro fallo), para que el popup
de info de `StartupTab` pueda mostrar el mensaje correcto en cada caso.
Eliminar una entrada es válido para cualquier fuente, no solo las añadidas
por la app: `RegistryStartupControl::DeleteRunEntry` borra el valor `Run`
(nunca el `.exe`), y `ShortcutStartupControl::DeleteToRecycleBin` envía el
`.lnk` a la Papelera de reciclaje vía `IFileOperation` (nunca el ejecutable
al que apunta) -- ambos, en `StartupTab`, están siempre detrás de
`ui::ConfirmDeleteDialog`, que captura su propia copia de la entrada en vez
de una referencia viva, para que un rescan concurrente no la invalide.
```

- [ ] **Step 3: Revisión manual**

```bash
grep -n "deletable\|solo las entradas añadidas\|manuallyAdded" /repos/MkPCApp/docs/PROJECT_STATUS.md /repos/MkPCApp/docs/ARCHITECTURE.md
```

Esperado: ningún resultado (ninguna referencia residual a la limitación antigua de "solo borrar lo añadido por la app").

- [ ] **Step 4: Commit**

```bash
git add docs/PROJECT_STATUS.md docs/ARCHITECTURE.md
git commit -m "Actualizar documentacion: Inicio como lista horizontal, eliminar universal"
```

---

### Task 11: Revisión final + prueba en máquina Windows

**Files:** ninguno nuevo — revisión transversal de todo lo anterior.

- [ ] **Step 1: Revisión de gestión de recursos en los archivos tocados**

```bash
grep -n "Release()\|CoCreateInstance\|RegOpenKeyEx\|RegCreateKeyEx\|RegCloseKey" \
  /repos/MkPCApp/src/startup/RegistryStartupControl.cpp \
  /repos/MkPCApp/src/startup/ShortcutStartupControl.cpp \
  /repos/MkPCApp/src/startup/SignatureVerifier.cpp
```

Para cada `RegOpenKeyEx`/`RegCreateKeyEx`/`CoCreateInstance` que tenga éxito, confirma leyendo la función completa que existe un `RegCloseKey`/`Release()` correspondiente en **toda** ruta de salida (incluidas las de error intermedio).

- [ ] **Step 2: Confirmar que ningún nuevo camino toca D3D fuera del hilo de render**

```bash
grep -n "iconCache_\.\|renderer_\." /repos/MkPCApp/src/ui/StartupTab.cpp
```

Confirma que todas las apariciones están dentro de `RenderEntryRow` (llamada solo desde `OnRender`), ninguna dentro de `OnTick`.

- [ ] **Step 3: Confirmar que la matriz de errores del spec está implementada**

Lee `docs/superpowers/specs/2026-07-13-inicio-lista-horizontal-design.md`, sección "Gestión de errores", y para cada fila confirma el código correspondiente:
- VERSIONINFO ausente/fallo → `AppInfoReader::ReadAppInfo` deja `productVersion`/`fileDescription` en `std::nullopt`, y `RenderInfoContent` usa `.value_or("No disponible")`.
- Tamaño ilegible → `ReadFileSize` devuelve `std::nullopt`, `RenderInfoContent` muestra "No disponible".
- Firma no comprobable vs. sin firmar → `SignatureStatus::VerificationFailed` vs. `NotSigned`, ambos con texto distinto en `RenderInfoContent`.
- Acceso roto (`targetMissing`) → popup muestra "Archivo no encontrado" sin intentar leer nada más; el botón "Eliminar" sigue disponible.
- "Abrir ubicación" con archivo inexistente → botón deshabilitado con tooltip.
- `ShellExecuteW` falla → `lastErrorMessage_` se rellena.
- Borrado a papelera/registro falla → `ConfirmDeleteDialog` muestra `errorMessage_` sin cerrar el popup.
- Confirmación cancelada → no-op.
- Entrada desaparece entre abrir confirmación y confirmar → `DeleteRunEntry`/`DeleteToRecycleBin` tratan "ya no existe" como éxito.

- [ ] **Step 4: Compilar y probar en la máquina Windows del usuario**

Esto no puede hacerse desde este entorno (ver Global Constraints). En la
máquina Windows del usuario:

```powershell
git pull
Remove-Item -Recurse -Force build/bin -ErrorAction SilentlyContinue
./scripts/build.ps1
```

Comprobar manualmente: la lista se ve como filas (no tarjetas); el popup
"i" muestra datos correctos en al menos una app con versión/firma y otra
sin ellas; "Abrir ubicación" selecciona el archivo correcto; eliminar un
acceso directo aparece en la Papelera de reciclaje; eliminar una entrada de
Registro de HKLM funciona (la app corre elevada); cancelar la confirmación
no cambia nada; el botón "Abrir ubicación" aparece deshabilitado para una
entrada con archivo ya no encontrado.

- [ ] **Step 5: Commit final (si la prueba en Windows requirió ajustes)**

```bash
git add -A
git commit -m "Ajustes tras prueba en Windows del rediseño de Inicio"
```

(Omitir este paso si no hizo falta ningún ajuste.)
