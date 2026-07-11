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

## Iteración 1 — Base de la app + Monitor de Hardware (estable)

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
  son accesibles con permisos de administrador. Esto se probó una vez antes
  (con LibreHardwareMonitorLib en 0.9.6) y pareció romper esos mismos
  sensores — pero la causa real era la versión de la librería (ver sección de
  PawnIO arriba), no el manifiesto; pendiente de confirmar en la máquina del
  usuario que la elevación forzada funciona bien ahora con la 0.9.4.
- **Aviso sobre el autoarranque**: como la app exige admin siempre, si se
  activa "Iniciar con Windows" es probable que Windows muestre el UAC (o,
  según la configuración del sistema, no la lance en absoluto) en cada inicio
  de sesión en vez de arrancar en silencio — fricción conocida, no resuelta
  (requeriría una tarea programada con "ejecutar con los privilegios más
  altos" en vez del registro `HKCU\...\Run` actual).

### Verificación pendiente (requiere máquina Windows real)

El desarrollo se hace desde un entorno Linux (sin MSVC/Windows SDK), así que
la build nativa nunca se compila ni se ejecuta desde aquí — todas las pruebas
reales las hace el usuario en su propio Windows. Falta confirmar todavía:

- Que el manifiesto `requireAdministrator` fuerza el UAC correctamente con la
  0.9.4 y que la temperatura de CPU/ventiladores siguen funcionando (la vez
  anterior que se probó esto fue con la 0.9.6, ya descartada como causa del
  problema de entonces).
- Que la instancia única localiza y enfoca la ventana existente correctamente
  en todos los casos (minimizada en bandeja, minimizada en la barra de tareas),
  y que no interfiere con relanzar la app ya elevada tras cerrar una instancia
  previa sin elevar.
- Huella de CPU/RAM en reposo (minimizada) durante un periodo prolongado.

(Lo único verificado desde Linux en cada cambio: que `sensor-bridge` compila y
publica sin errores para `win-x64` con `dotnet publish`; el lado nativo C++ se
revisa manualmente línea a línea ya que no se puede compilar aquí.)

### Revisión de seguridad/estabilidad del sistema

La app no realiza operaciones destructivas: no hay formateo de discos, borrado
de archivos, instalación de servicios/tareas programadas, ni escritura fuera de
`HKCU` (autoarranque, reversible). La elevación es **obligatoria en cada
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
