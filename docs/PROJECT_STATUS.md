# Estado del Proyecto — MkPCApp

> Este documento es la fuente única de verdad sobre el estado **actual** de la app.
> No es un registro histórico: refleja cómo es la app ahora mismo, no cómo llegó a serlo.

## Regla de mantenimiento (obligatoria en cada tarea/iteración)

1. Al finalizar cualquier tarea o iteración, **elimina cualquier documento de proceso**
   generado durante el trabajo (notas intermedias, planes de diseño ya ejecutados,
   borradores, resúmenes de decisiones ya tomadas, etc.). No deben quedar residuos
   del proceso que llevó al resultado — solo el resultado.
2. **Actualiza este archivo** (`docs/PROJECT_STATUS.md`) para que refleje el estado
   final de la app tras esa tarea/iteración: nuevas funcionalidades, cambios de
   arquitectura, nuevos requisitos, o verificaciones pendientes que hayan cambiado.
3. Si una sección deja de ser cierta, corrígela o bórrala — no añadas una sección
   nueva "para la próxima vez" que contradiga la actual. Esto incluye narrativas de
   "bug encontrado y arreglado": una vez corregido, describe el comportamiento
   actual, no la historia de cómo estaba roto.

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
- **Icono de aplicación propio**: `resources/AppIcon.ico` (+ `AppIcon.rc`,
  compilado como recurso Win32 vía CMake) sustituye el icono genérico de
  Windows en la barra de título, la barra de tareas y el icono de la
  bandeja del sistema — antes de esto la app no tenía icono propio.

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

El icono de aplicación (`resources/AppIcon.rc`) se compila como recurso
Win32 aparte, referenciado por `src/main.cpp` (clase de ventana) y
`src/app/Application.cpp` (icono de bandeja) vía el mismo ID
(`IDI_APPICON`, `resources/resource.h`) — no forma parte del módulo
`src/startup/`, es transversal a toda la app.

### Verificación pendiente (requiere máquina Windows real)

Confirmado en la máquina real del usuario: compilación y enlazado con las
librerías COM/`wintrust`/`crypt32`/`gdi32`/`version`, y con el nuevo recurso
de icono (`resources/AppIcon.rc`) — el `.exe` resultante lleva el icono
embebido y la app arranca correctamente con él en la barra de tareas y la
bandeja del sistema.

Aún sin probar en la máquina real del usuario: que `WinVerifyTrust`
identifica correctamente binarios reales firmados por Microsoft y los
excluye; ida y vuelta con el Administrador de tareas de Windows al
deshabilitar/habilitar una entrada; que eliminar un acceso directo lo manda
a la Papelera de reciclaje (verificable abriéndola) y que eliminar un valor
de Registro de HKLM funciona estando elevado; que "Abrir ubicación"
selecciona el archivo correcto en el Explorador; y que el popup de info
muestra datos correctos (incluyendo casos sin versión/sin firma).

## Iteración 2 — Sección "Perfiles": perfiles de rendimiento + automatización (en PR, pendiente de fusionar a main)

Añade una segunda sección (`ui::PerfilesTab`, registrada junto a
`HardwareMonitorTab` sin tocar el resto del shell) que agrupa varios ajustes
de Windows en "perfiles" aplicables de un clic, más automatización simple por
horario/batería/fuente de alimentación. Desarrollada en la rama
`feature/perfiles-rendimiento` (PR #1 contra `main`) y probada en la máquina
real del usuario, que es donde se compila y ejecuta la app.

### Qué hace

- **Variables que un perfil puede fijar**: plan de energía de Windows
  (Ahorro/Equilibrado/Alto rendimiento/Máximo rendimiento), tiempos de
  apagado de pantalla, de suspensión y de hibernación (con corriente y con
  batería, editados en minutos en el editor de perfil aunque se guardan como
  segundos internamente — API de Windows), brillo
  (una sola variable, unificada: se aplica automáticamente al panel interno
  vía WMI y/o a los monitores externos compatibles con DDC/CI que se
  detecten, sin que el usuario tenga que distinguir el caso). Volumen es
  variable **anulable**: lleva su propia casilla "¿modificar?", desmarcada
  por defecto, para que un perfil de rendimiento no toque de rebote algo que
  no tiene que ver.
- **5 perfiles predefinidos** (Rendimiento, Equilibrado, Ahorro de batería,
  Silencio/Noche, Presentación/Multimedia) — fijos, no editables ni
  borrables, marcados con `[bloqueado]` en su tarjeta.
- **Perfiles personalizados** ilimitados, con editor modal (todas las
  variables prerellenadas con valores por defecto), editar/borrar por
  tarjeta.
- **Automatización**: lista ordenada de reglas (franja horaria con opción
  "solo entre semana", batería por debajo de un umbral, cambio de fuente de
  alimentación AC↔batería), cada una con checkbox de activar/desactivar y
  reordenable arrastrando con el ratón — el orden de arriba a abajo decide
  qué regla gana si varias coinciden a la vez (primera que aplica, gana).
- **Botón "Apagar pantalla ahora"**, siempre visible, independiente del
  perfil activo (`SC_MONITORPOWER`, no bloquea sesión ni suspende).
- **Reconciliación al arrancar**: primero evalúa las reglas de
  automatización contra el momento actual; si ninguna aplica, compara el
  estado real de Windows contra cada perfil definido y marca como activo el
  primero que coincide exactamente (brillo con tolerancia ±2%; variables
  anulables desmarcadas o ilegibles se excluyen de la comparación). Si no
  coincide con ninguno, muestra "Sin perfil activo (config. personalizada
  detectada)" — nunca aplica nada de forma no solicitada.
- Persistencia de perfiles personalizados y reglas en
  `%LOCALAPPDATA%\MkPCApp\profiles.json`, con un lector/escritor JSON propio
  y minimalista (`src/profiles/ProfileJson.*`) en vez de una librería de
  terceros — el formato que necesita este archivo es lo bastante simple
  (objetos planos, sin anidamiento profundo) para no justificar vendorizar
  una dependencia nueva.

### Limitaciones conocidas / mejor esfuerzo

- **Luz nocturna y Asistente de enfoque no están soportados, a propósito**:
  ninguno de los dos tiene API pública de Windows; solo son controlables
  escribiendo un blob de registro no documentado ("CloudStore") cuyo layout
  exacto varía según build de Windows/cuenta de usuario, así que no hay forma
  fiable de implementarlos. No existe ningún código para ellos en la app. Si
  Microsoft publica algún día una API pública para cualquiera de las dos, se
  puede añadir.
- **Brillo de monitor externo** depende de que el monitor soporte DDC/CI (más
  fiable por DisplayPort que por HDMI) — si no responde ningún monitor ni hay
  panel interno, se reporta "no disponible".
- **"Máximo rendimiento"** está oculto por defecto en la mayoría de
  instalaciones de Windows; se intenta duplicar automáticamente
  (`PowerDuplicateScheme`) y, si falla, se aplica "Alto rendimiento" en su
  lugar sin bloquear el resto del perfil.
- Los GUID de subgrupo/ajuste de energía usados para leer/escribir timeouts
  (`src/profiles/SystemControl/PowerTimeouts.cpp`: pantalla, suspensión e
  hibernación) están confirmados contra un volcado real de `powercfg /q` —
  ver `docs/POWER_GUIDS.md`, que también guarda el volcado completo por si
  hace falta añadir alguna otra variable de energía en el futuro.

### Arquitectura (resumen)

Nuevo módulo `src/profiles/` (datos puros en `ProfileTypes.h`, persistencia en
`ProfileStore`/`ProfileJson`, orquestación en `ProfileManager` y
`AutomationEngine`, acceso a Windows en `SystemControl/*`) más
`src/ui/PerfilesTab` (+ `ProfileEditorDialog`/`RuleEditorDialog`), siguiendo
el mismo patrón de extensión por `ITab` que ya usa el monitor de hardware —
ningún otro código del shell cambia salvo el registro de la nueva pestaña en
`Application::Init`. El motor de automatización reutiliza el hilo de 1 Hz ya
existente (`Application::DataTickLoop`) en vez de crear un hilo nuevo, y
reacciona también de inmediato a `WM_POWERBROADCAST` (cambio de fuente de
alimentación) sin esperar al siguiente tick.

### Verificación pendiente (requiere máquina Windows real)

Compila y se ha probado en la máquina real del usuario: los perfiles
predefinidos aplican sus valores, crear/editar perfiles personalizados
funciona, y crear una regla de automatización (incluyendo apuntar a un
perfil personalizado) funciona. Queda por confirmar todavía: que la
detección de perfil activo al arrancar acierta en los tres casos (regla de
automatización activa, coincidencia exacta con un perfil, sin coincidencia
ninguna), y que arrastrar una regla para reordenarla persiste correctamente.

## Iteración 1 — Base de la app + Monitor de Hardware (estable, `v1.0.0` en `main`)

Considerada estable: todas las funcionalidades pedidas para esta iteración están
implementadas y el usuario ha confirmado en su máquina que el monitor de
hardware funciona correctamente (incluida la temperatura de CPU, que requiere
ejecutar elevado). Punto de partida sólido para añadir nuevas secciones en
futuras iteraciones.

### Qué es la app

Aplicación de escritorio para Windows en C++, pensada para ejecutarse siempre en
segundo plano (bandeja del sistema) como centro de utilidades diarias. Esta primera
iteración establece la base extensible (ventana, sistema de secciones, bandeja del
sistema) y entrega una única sección funcional: un monitor de hardware en tiempo real.

### Funcionalidades implementadas

- **Sección "Hardware Monitor"** con:
  - Uso de CPU, uso de RAM, uso de GPU, VRAM (usada/total), temperatura de CPU,
    temperatura de GPU, velocidad de ventiladores (si el hardware los expone),
    velocidad de red (subida/bajada), espacio libre por disco, tiempo de actividad.
  - Gráficas de los últimos 60 segundos (CPU/RAM, temperaturas, GPU/VRAM, red)
    mediante ImPlot, en cuadrícula 2×2 con margen, eje Y fijado explícitamente
    (0-100 para métricas en %; para temperatura/red, 0 hasta un máximo dinámico
    con margen calculado sobre los datos visibles).
  - Degradación visible cuando un sensor no está disponible (p. ej. "N/A", "No
    fan sensors detected") en lugar de mostrar ceros falsos o datos obsoletos.
- **Instancia única**: si la app ya está abierta (aunque esté minimizada en la
  bandeja) y se intenta abrir de nuevo, no se lanza una segunda instancia —
  simplemente se trae al frente la ventana de la instancia ya en ejecución.
- **Bandeja del sistema**: cerrar la ventana (X) minimiza a la bandeja en lugar de
  cerrar la app; doble clic reabre; menú contextual con "Abrir" / "Iniciar con
  Windows" (autoarranque vía registro `HKCU`) / "Salir".
- **Sistema de secciones extensible** (`ui::ITab` + `ui::TabManager`): añadir una
  futura funcionalidad es escribir una clase y registrarla una vez — sin tocar el
  resto de la app. Navegación mediante barra lateral de iconos.
- **Arquitectura de eficiencia**: sin bucle de renderizado mientras está oculta en
  bandeja, sondeo de datos (1 Hz) desacoplado del hilo de render, renderizado
  limitado por vsync.

### Diseño visual

Estética inspirada en VS Code: gama de grises oscura (fondo `#1e1e1e`, tarjetas
`#252526`, borde `#3c3c3c`, texto `#d4d4d4`/`#8a8a8a`), un único color de acento
azul `#3794ff` para estado activo/barras de progreso/líneas de gráfica, esquinas
con redondeo suave y bordes mínimos (`src/ui/Theme.h/.cpp`). Tipografía Segoe UI
cargada desde `C:\Windows\Fonts` (con fallback silencioso a la fuente por
defecto de ImGui si no existe). Navegación por barra lateral de iconos (estilo
"Activity Bar"). Tarjetas de métricas con jerarquía clara (etiqueta pequeña,
valor grande, barra de progreso para métricas en %). Cada gráfica vive dentro
de su propia tarjeta con borde, para que la separación entre ellas sea clara.
La sección de ventiladores usa el mismo lenguaje visual: tarjeta con borde,
título "Fan Speed" destacado, y tabla con grid completo y franjas alternas.

### Arquitectura (resumen)

Dos procesos:
- `MkPCApp.exe` (C++, nativo): ventana, DirectX 11 + Dear ImGui, bandeja, lee
  CPU/RAM/red/disco/uptime directamente vía Win32 (sin necesidad de
  elevación). Cada bridge lanzado se asigna a un Job Object de Windows con
  `JOB_OBJECT_LIMIT_KILL_ON_JOB_CLOSE`, para que nunca quede huérfano aunque
  `MkPCApp.exe` muera de forma anómala (crash, Task Manager, depurador).
  Instancia única vía mutex con nombre fijo (`Local\MkPCApp_SingleInstance`),
  comprobado antes de crear cualquier ventana; si ya existe, localiza la
  ventana de la instancia existente por su clase y la trae al frente en vez
  de arrancar de nuevo.
- `MkPCApp.SensorBridge.exe` (.NET 8, autocontenido, sin UI, LibreHardwareMonitorLib
  **0.9.4, fijada deliberadamente**: no es la última versión — ver aviso más
  abajo): temperaturas, uso/VRAM de GPU y velocidad de ventiladores; publica
  snapshots a memoria compartida (`Local\MkPCApp_SensorData_v1`,
  `MemoryMappedFile.CreateOrOpen`) una vez por segundo. Tolera fallos de
  `Computer.Open()` y de sensores individuales sin caerse — degrada a "sensor
  no disponible" en vez de morir.

Detalle completo en `docs/ARCHITECTURE.md`.

### LibreHardwareMonitorLib fijada a 0.9.4: no actualizar sin más

Se probó subir a la 0.9.6 (última estable) esperando que reconociera mejor la
CPU del usuario, pero eso hizo que temperatura de CPU y ventiladores dejaran
de funcionar **incluso ejecutando como Administrador**. Investigado y
confirmado desensamblando ambas versiones del paquete NuGet: a partir de la
0.9.5, la librería requiere un driver aparte llamado
[PawnIO](https://github.com/namazso/PawnIO) para los accesos de bajo nivel
(MSR de CPU, controlador embebido para ventiladores) — ese driver **no viene
incluido en el paquete NuGet** y esta app no lo instala, así que esos
sensores fallan en silencio sin él, sin importar los permisos. La GPU sigue
funcionando en cualquier versión porque usa APIs del fabricante (NVAPI/etc.),
no PawnIO. La 0.9.4 usa en cambio un driver embebido propio que solo necesita
permisos de administrador, sin instalación aparte — por eso está fijada a
esa versión deliberadamente. Subir de versión en el futuro requeriría además
empaquetar e instalar PawnIO (fuera del alcance actual).

### Requisitos actuales

**Para compilar (solo en Windows):**
- Visual Studio 2022 (workload "Desktop development with C++") o MSVC Build
  Tools + Windows SDK.
- CMake ≥ 3.21.
- .NET SDK 8.0.
- Git (para los submodules `third_party/imgui` y `third_party/implot`).
- Si se cambia el build de sitio (p. ej. tras mover carpetas o cambiar de
  generador de CMake, o tras actualizar la versión de LibreHardwareMonitorLib),
  conviene borrar `build/bin` y recompilar desde cero — se ha observado que un
  build incremental puede dejar el `.exe` del bridge desactualizado.

**Para ejecutar:**
- Windows (usa APIs Win32/DirectX/registro específicas de Windows).
- Sin dependencias externas del usuario: el bridge de sensores se publica
  autocontenido (no requiere .NET instalado aparte).
- **Requiere ejecutarse como Administrador** — el ejecutable lleva un
  manifiesto (`requireAdministrator`), así que Windows muestra el diálogo de
  UAC en cada arranque; no hay forma de evitarlo sin dejar de requerir
  elevación. Necesario porque la temperatura de CPU y los ventiladores solo
  son accesibles con permisos de administrador.
- **Aviso sobre el autoarranque**: como la app exige admin siempre, si se
  activa "Iniciar con Windows" es probable que Windows muestre el UAC (o,
  según la configuración del sistema, no la lance en absoluto) en cada inicio
  de sesión en vez de arrancar en silencio — fricción conocida, no resuelta
  (requeriría una tarea programada con "ejecutar con los privilegios más
  altos" en vez del registro `HKCU\...\Run` actual).

### Desarrollo desde Linux

El desarrollo se hace desde un entorno Linux (sin MSVC/Windows SDK): la build
nativa nunca se compila ni se ejecuta desde aquí, y el lado C++ se revisa
manualmente línea a línea. Todas las pruebas reales (compilación con MSVC,
ejecución, UAC, sensores) las hace el usuario en su propia máquina Windows.
Lo único que sí se verifica desde Linux en cada cambio es que `sensor-bridge`
compila y publica sin errores para `win-x64` con `dotnet publish`. Aspectos
menores aún sin probar exhaustivamente (no bloquean el estado "estable"):
huella de CPU/RAM en reposo (minimizada) durante un periodo prolongado, y que
la instancia única enfoque correctamente la ventana existente en todas las
combinaciones de estado (minimizada en bandeja vs. en la barra de tareas).

### Revisión de seguridad/estabilidad del sistema

La app no realiza operaciones destructivas: no hay formateo de discos, borrado
de archivos, ni instalación de servicios/tareas programadas. La app escribe
fuera de `HKCU` en un solo caso, introducido por la sección "Inicio": al
deshabilitar una entrada de arranque de todos los usuarios, escribe en
`HKLM\...\Explorer\StartupApproved\Run` — el mismo flag de estado que ya usa
el propio Administrador de tareas de Windows, nunca el valor `Run` en sí, y
siempre reversible con un clic (ver Iteración 3). Fuera de ese caso, toda
otra escritura de la app (autoarranque propio, perfiles) sigue limitada a
`HKCU` y es reversible. La elevación es **obligatoria en cada
arranque** (manifiesto `requireAdministrator`) — Windows sigue mostrando el
diálogo de UAC estándar cada vez, así que sigue siendo un consentimiento
explícito del usuario en el momento, no una elevación silenciosa oculta.

**Riesgo inherente conocido (no es un bug propio):** `LibreHardwareMonitorLib`,
que ahora se ejecuta siempre con privilegios de administrador, puede cargar un
driver en modo kernel para leer temperaturas/sensores de placa (linaje
WinRing0). Esta familia de drivers ha tenido vulnerabilidades históricas
explotadas en ataques "BYOVD"; es ampliamente usada (HWiNFO,
OpenHardwareMonitor, etc.) y se considera segura en uso normal, pero no es
"riesgo cero". No se puede eliminar sin cambiar de librería de sensores. Al
requerir admin siempre, la app se expone a este driver en el 100% de sus
ejecuciones.
