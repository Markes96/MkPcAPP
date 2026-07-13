# Diseño: rediseño de "Inicio" a lista horizontal + info/ubicación/eliminar universal

## Contexto

La sección "Inicio" (rama `feature/gestor-inicio`) ya existe: enumera apps de
terceros que arrancan con Windows (Registro HKCU/HKLM + carpeta Inicio de
usuario y de todos los usuarios), permite activar/desactivar de forma
reversible, y añadir una nueva a mano. El diseño actual usa una cuadrícula de
tarjetas (mismo lenguaje visual que "Perfiles") y solo permite **borrar**
(no solo deshabilitar) las entradas añadidas por el propio usuario en la
sesión actual — el resto solo se puede deshabilitar, nunca borrar.

Tras usarlo, se pide:
1. Cambiar la cuadrícula de tarjetas por una **lista horizontal** (una fila
   por entrada).
2. Tres utilidades nuevas por fila: **info de la app**, **abrir ubicación
   del ejecutable**, y **eliminar** — esta última ahora disponible para
   *cualquier* entrada, no solo las añadidas por la app.
3. Que la gestión de errores sea explícita en todo momento: si algo falta o
   no se puede leer, debe quedar claramente indicado en la propia UI (nunca
   fallar en silencio ni mostrar un dato falso), para que el usuario pueda
   detectarlo y pedir un ajuste después.

Aclaración importante del usuario: "eliminar" nunca borra la app real ni su
ejecutable — solo quita el registro que hace que arranque (el valor de
Registro `Run`, o el propio acceso directo `.lnk`, que no es más que un
puntero de arranque, no la aplicación en sí).

## Qué cambia respecto al diseño actual

- **Ya no existe el concepto "solo deshabilitar salvo que lo añadieras tú"**:
  toda entrada es deshabilitable y **eliminable** por igual. Esto simplifica
  el modelo: `StartupEntry.deletable` y el tracking de
  `manuallyAddedValueNames_` en `StartupScanner` desaparecen —
  `DeleteManualEntry` se generaliza a `DeleteEntry`, válido para cualquier
  fuente.
- **Confirmación antes de borrar** (acordado con el usuario): un diálogo
  modal "¿Seguro que quieres eliminar «X» del inicio? Esta acción no se
  puede deshacer desde la app." antes de ejecutar el borrado, para cualquier
  entrada.
- **Borrado real, alcance por tipo de fuente**:
  - Registro (HKCU/HKLM Run): `RegDeleteValueW` sobre el valor `Run` (ya
    implementado en `RegistryStartupControl::DeleteUserRunEntry`, que se
    generaliza para aceptar también HKLM, no solo HKCU) + limpieza
    best-effort del `StartupApproved` a juego. Nunca toca el `.exe`.
  - Carpeta Inicio (accesos directos): se borra el `.lnk` — enviado a la
    **Papelera de reciclaje de Windows** (`IFileOperation` con
    `FOF_ALLOWUNDO`), no un borrado permanente, como red de seguridad
    adicional coherente con que ahora esto se puede hacer con
    *cualquier* entrada, no solo las que añadiste tú. Nunca toca el
    ejecutable al que apunta el acceso directo.

## Layout: de tarjetas a lista horizontal

Se sustituye la cuadrícula de tarjetas por una tabla (`ImGui::BeginTable`),
seleccionado explícitamente por reutilizar un patrón que ya existe en la app
en vez de inventar uno nuevo: `HardwareMonitorTab`'s tabla de ventiladores
usa exactamente `ImGuiTableFlags_Borders | ImGuiTableFlags_RowBg |
ImGuiTableFlags_SizingStretchProp` con cabecera de columnas — la lista de
"Inicio" sigue el mismo idiom.

Columnas: **Icono** (32px, o placeholder) · **Nombre** · **Origen** (badge
de texto: "HKCU"/"HKLM"/"Carpeta Inicio"/"Carpeta Inicio (todos)") ·
**Activado** (checkbox) · **Acciones** (tres botones pequeños: "i" info,
"Abrir ubicación", "Eliminar").

`RenderEntryCard` se reescribe como `RenderEntryRow` (una fila de la tabla en
vez de una tarjeta con `BeginChild`); `kCardWidth`/`kCardHeight` dejan de
usarse en este archivo (siguen existiendo en `ui::CardWidgets` para
`PerfilesTab`, que no cambia).

## Info de la app (botón "i" → popup)

Un popup (`ImGui::OpenPopup`/`BeginPopup`, no modal — se cierra al hacer clic
fuera, como un tooltip enriquecido) con:
- Ruta completa del ejecutable (o del acceso directo si el destino no se
  resolvió).
- Editor/firmante: reutiliza `SignatureVerifier`, que se amplía para
  exponer también el **nombre del firmante** (no solo el booleano
  "es Microsoft"), cacheado igual que el veredicto, por `(ruta,
  fecha de modificación)`.
- Tamaño del archivo (`GetFileAttributesExW`).
- Versión de producto y descripción del archivo, leídas del bloque
  `VERSIONINFO` del `.exe` vía `GetFileVersionInfoSizeW`/
  `GetFileVersionInfoW`/`VerQueryValueW` — nuevo, no se lee en ningún otro
  sitio de la app hoy. Nuevo módulo `src/startup/AppInfoReader.h/.cpp`
  (mismo patrón que `IconExtractor`: función pura, sin estado, sin
  conocimiento de ImGui/D3D), con caché en `StartupTab` por ruta (igual que
  `IconTextureCache`, pero solo se calcula bajo demanda, la primera vez que
  se abre el popup para esa ruta, no en cada rescan).

## Abrir ubicación del ejecutable

`ShellExecuteW(nullptr, L"open", L"explorer.exe", L"/select,\"" + ruta +
L"\"", nullptr, SW_SHOWNORMAL)` — abre el Explorador con el archivo ya
seleccionado. Si `resolvedExePath`/`shortcutFilePath` está vacío o el
archivo ya no existe, el botón se muestra deshabilitado (greyed out) con un
tooltip "El archivo ya no existe", en vez de intentar la acción y fallar en
silencio.

## Gestión de errores — matriz completa (foco explícito del usuario)

Principio: **todo fallo se ve**, nunca un dato en blanco sin explicación ni
una acción que falla sin avisar. Se reutiliza el patrón ya existente
`lastErrorMessage_` (mensaje transitorio bajo la lista) para acciones, y
literales "No disponible"/"Sin firmar"/"Archivo no encontrado" en vez de
campos vacíos, para los datos de solo lectura.

| Situación | Comportamiento |
|---|---|
| Bloque `VERSIONINFO` ausente o `GetFileVersionInfoW` falla | El popup de info muestra "No disponible" en versión y descripción; el resto de campos (ruta, firmante, tamaño) se muestran igual. |
| `GetFileAttributesExW` falla (archivo borrado entre el escaneo y abrir el popup) | Tamaño = "No disponible"; el resto de campos que sí se pudieron leer se muestran. |
| Verificación de firma falla/error | Firmante = "No se pudo comprobar la firma" (distinto de "Sin firmar", que es el caso "comprobado, no está firmado"). |
| `resolvedExePath` vacío o `targetMissing` (acceso directo roto) | El popup de info muestra "Archivo no encontrado" en vez de intentar leer atributos/versión/firma; sigue permitiendo eliminar la entrada de arranque (no depende de que el destino exista). |
| Botón "Abrir ubicación" con archivo inexistente | Botón deshabilitado (no clicable) con tooltip explicando por qué, en vez de fallar tras el clic. |
| `ShellExecuteW` devuelve error (Explorer no disponible, permisos) | Mensaje transitorio de error bajo la lista ("No se pudo abrir la ubicación de «X»."). |
| Borrado de acceso directo a Papelera falla (`IFileOperation`, archivo bloqueado, sin permisos) | Mensaje transitorio de error; la entrada permanece en la lista (no se borra optimistamente antes de confirmar éxito). |
| Borrado de valor de Registro falla (raro, dado que la app corre elevada) | Mismo tratamiento: mensaje transitorio, entrada permanece. |
| El usuario cancela el diálogo de confirmación | No ocurre nada, se cierra el diálogo sin tocar nada. |
| La entrada a confirmar desaparece de la lista entre abrir el diálogo y confirmar (p. ej. un rescan en curso la quitó) | El diálogo de confirmación captura una copia propia de los datos necesarios para borrar (no una referencia viva a la fila) — si el borrado subyacente ya no encuentra el valor/archivo, se trata como éxito silencioso (ya no está, que es justo el estado deseado), no como error. |

## Simplificaciones derivadas de "eliminar es universal"

- `StartupEntry.deletable` se elimina (ya no hace falta distinguir).
- `StartupScanner::manuallyAddedValueNames_` y su lógica en `Scan()` se
  eliminan — ya no hay que "recordar" qué se añadió esta sesión para decidir
  si se puede borrar.
- `StartupScanner::DeleteManualEntry` se renombra a `DeleteEntry`, con
  despacho por `entry.source` (Registro → `RegistryStartupControl`,
  siguiendo el mismo patrón que `SetEnabled`; Carpeta Inicio →
  `ShortcutStartupControl::DeleteToRecycleBin`, nueva función).
- `RegistryStartupControl::DeleteUserRunEntry` se generaliza a
  `DeleteRunEntry(HKEY hive, const std::wstring& valueName, bool
  isWow6432)` (ya no fijo a `HKEY_CURRENT_USER`/la clave `Run` normal), con
  la misma forma de parámetros que `SetApprovedEnabled`, ya que ahora
  también se puede eliminar una entrada de HKLM o de su espejo
  `WOW6432Node`.

## Diálogo de confirmación (`ConfirmDeleteDialog`)

Mismo idiom que `AddStartupEntryDialog`/`ProfileEditorDialog`:
`OpenForEntry(entry)` copia los campos necesarios para borrar (id, fuente,
displayName, registryValueName + isWow6432, o shortcutFilePath) en buffers
propios — nunca una referencia a la entrada viva — y `Render(scanner)` se
llama incondicionalmente cada frame, fuera del bloque que mantiene
`scanMutex_` (igual que `AddStartupEntryDialog` hoy). Al confirmar, llama a
`scanner.DeleteEntry(...)` y dispara un rescan inmediato (mismo patrón que
ya existe para "añadir" vía `ConsumeJustSaved`), para que la entrada
desaparezca de la lista al instante en vez de esperar al siguiente ciclo de
~10s.

## Archivos afectados

- `src/startup/StartupTypes.h` — quitar `deletable`.
- `src/startup/RegistryStartupControl.h/.cpp` — generalizar
  `DeleteUserRunEntry` a cualquier hive.
- `src/startup/ShortcutStartupControl.h/.cpp` — nueva
  `DeleteToRecycleBin(const StartupEntry&)` vía `IFileOperation`.
- `src/startup/SignatureVerifier.h/.cpp` — exponer también el nombre del
  firmante, no solo el booleano.
- `src/startup/AppInfoReader.h/.cpp` — nuevo: tamaño de archivo + versión/
  descripción de `VERSIONINFO`.
- `src/startup/StartupScanner.h/.cpp` — quitar tracking de "añadido
  manualmente"; `DeleteManualEntry` → `DeleteEntry` universal.
- `src/ui/StartupTab.h/.cpp` — tabla en vez de tarjetas; nuevos botones
  info/abrir ubicación/eliminar; caché de `AppInfo` por ruta.
- `src/ui/ConfirmDeleteDialog.h/.cpp` — nuevo diálogo modal de confirmación.
- `CMakeLists.txt` — añadir `AppInfoReader.cpp`, `ConfirmDeleteDialog.cpp`;
  añadir `version.lib` (para `GetFileVersionInfoW`/`VerQueryValueW`).
- `docs/PROJECT_STATUS.md` / `docs/ARCHITECTURE.md` — actualizar la sección
  "Inicio" para reflejar el nuevo diseño (lista horizontal, eliminar
  universal con confirmación, info/ubicación) en vez de una narrativa de
  "antes vs. ahora".

## Verificación

Igual que el resto de la app: revisión manual de código desde Linux
(gestión de handles/COM, que la tabla y los nuevos botones sigan el patrón
existente, que la matriz de errores de arriba esté implementada punto por
punto), y prueba real en la máquina Windows del usuario para: la tabla se
ve bien, el popup de info muestra datos correctos en apps con y sin
versión/firma, "abrir ubicación" selecciona el archivo correcto en el
Explorador, borrar un acceso directo lo manda a la Papelera (verificable
abriéndola), y borrar un valor de Registro de HKLM funciona estando
elevado.
