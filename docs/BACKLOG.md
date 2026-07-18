# Backlog — MkPCApp

> Lista de mejoras y utilidades candidatas para futuras iteraciones. A
> diferencia de `docs/PROJECT_STATUS.md` (que describe lo que la app **es**
> ahora mismo), este documento es una lista de lo que **podría** construirse
> después — nada aquí está implementado ni planificado en firme. Al empezar a
> trabajar en un ítem, muévelo de aquí a `PROJECT_STATUS.md` como nueva
> iteración; si se descarta, bórralo de aquí en vez de dejarlo marcado como
> "descartado".

## Nuevas secciones (utilidades)

Candidatas a nueva pestaña (`ui::ITab`), en el orden en que se evaluaron:

### 1. Limpiador de archivos temporales/caché

Escanea ubicaciones conocidas (`%TEMP%` de usuario, `C:\Windows\Temp`,
prefetch, caché de miniaturas, logs de Windows Update, Papelera de
reciclaje) y muestra cuánto espacio ocupa cada categoría, con checkboxes
para elegir qué limpiar antes de confirmar.

- Encaja bien: mismo patrón que "Inicio" (módulo `src/cleanup/` con
  escaneo + acción, lista horizontal, y podría reutilizar
  `ui::ConfirmDeleteDialog` tal cual).
- Riesgo principal: archivos en uso (locks) hay que saltarlos sin romper
  nada, y algunas carpetas "temp" en realidad las siguen usando procesos
  vivos. Limitarse a categorías bien conocidas y no tocar cachés de apps de
  terceros sin investigar cada una.
- Requiere admin para algunas rutas del sistema — ya cubierto por el
  manifiesto `requireAdministrator` existente.

### 2. Gestor de servicios de Windows

Lista de servicios (Service Control Manager): estado (en ejecución/
detenido), tipo de arranque (automático/manual/deshabilitado), con
iniciar/detener/cambiar tipo de arranque.

- Encaja bien: casi un calco de `StartupTab` pero contra
  `OpenSCManager`/`EnumServicesStatus`/`ChangeServiceConfig` en vez del
  Registro `Run`.
- Riesgo principal: deshabilitar un servicio del sistema puede romper algo
  (red, audio...). Necesita una lista de servicios "protegidos" que no se
  puedan tocar, o al menos una advertencia fuerte para servicios con
  dependencias del sistema.

### 3. Desinstalador de programas

Lee `HKLM\...\Uninstall` y `HKCU\...\Uninstall` (igual que el Panel de
Control), muestra nombre/editor/tamaño/fecha, y lanza el
`UninstallString` de cada programa.

- Encaja mejor que ningún otro: reutiliza casi directamente
  `RegistryStartupControl` (lectura de Registro) y `AppInfoReader`
  (metadatos de `.exe`) ya existentes.
- Riesgo bajo: la app solo *lanza* el desinstalador oficial, no borra
  archivos por su cuenta. Cuidado con parsear bien `UninstallString`
  (puede traer flags como `/quiet` que no conviene forzar) y con
  desinstaladores rotos/ausentes.

### 4. Benchmark rápido de CPU/GPU

Prueba corta (30s–1min) de carga en CPU (multi-hilo) y opcionalmente GPU,
con una puntuación simple y gráfica de temperatura/frecuencia durante la
prueba vía ImPlot.

- Encaja bien en la parte de lectura: reutiliza `NativeSensors`/
  sensor-bridge tal cual para temperatura y frecuencia durante la carga.
- Es el que menos utilidad "de sistema" aporta de los cinco (no limpia ni
  arregla nada) — más un feature vistoso que esencial.
- Riesgo: una prueba de estrés mal hecha puede calentar el equipo
  innecesariamente; necesita límites de tiempo/temperatura de seguridad.

### 5. Gestor de procesos (mini Task Manager)

Lista de procesos en ejecución con CPU/RAM/disco por proceso, y acciones
de terminar / cambiar prioridad / afinidad de núcleos.

- Complementa el monitor de hardware (hoy solo agregado del sistema, no
  por proceso), vía `CreateToolhelp32Snapshot`/`NtQuerySystemInformation`.
- Es el más pesado de los cinco: algunos procesos protegidos del sistema
  no se pueden ni leer ni terminar sin fallar con gracia, y se solapa
  mucho con el Task Manager de Windows, que ya hace esto bien. El valor
  diferencial estaría en integrarlo con los perfiles de automatización
  (p. ej. subir prioridad a un proceso al activar un perfil).

## Deuda técnica / fricciones conocidas

Ítems ya documentados como limitación en `docs/PROJECT_STATUS.md`, listados
aquí solo como recordatorio de que son mejoras posibles, no bugs a
corregir de inmediato:

- **Autoarranque con UAC**: si se activa "Iniciar con Windows", como la app
  exige admin siempre, Windows puede mostrar el UAC (o no lanzarla, según
  configuración) en cada inicio de sesión en vez de arrancar en silencio.
  Solución posible: registrar una Tarea Programada con "ejecutar con los
  privilegios más altos" en vez del registro `HKCU\...\Run` actual.
- **Brillo de monitor externo** depende de soporte DDC/CI (poco fiable por
  HDMI) — sin solución mejor conocida por ahora, es una limitación de
  hardware/driver, no de la app.
- **Tareas Programadas** quedaron fuera de alcance del gestor de arranque
  ("Inicio") deliberadamente — si en el futuro se justifica cubrirlas,
  sería una extensión natural de `src/startup/`.
