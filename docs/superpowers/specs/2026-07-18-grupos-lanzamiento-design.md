# Diseño: "Grupos" — abrir y cerrar conjuntos de apps de un clic

## Contexto

MkPCApp ya tiene dos secciones construidas sobre el mismo patrón de
extensión (`ui::ITab` + módulo propio en `src/`): "Perfiles"
(`src/profiles/`) para ajustes de sistema aplicables de un clic, e "Inicio"
(`src/startup/`) para gestionar qué arranca con Windows. Se pide una
tercera sección, "Grupos": el usuario define un conjunto de apps asociadas
(p. ej. League of Legends + Discord + un overlay) y las abre o cierra todas
de un clic, sin tener que hacerlo app por app.

Caso de uso motivador: "arrancar League of Legends" en realidad implica
abrir varias apps a la vez (el juego, Discord, quizá un overlay de FPS);
"terminar de jugar" implica cerrarlas todas. Hoy eso se hace a mano, app
por app.

## Alcance decidido con el usuario

- **Dos botones por grupo, nada de detección automática**: "Abrir grupo" y
  "Cerrar grupo", ambos accionados a mano. No hay lógica de "detectar que
  el juego se cerró y cerrar lo demás solo" — ese comportamiento queda
  fuera de alcance de este diseño.
- **Cualquier ejecutable o acceso directo**, no solo `.exe` de juegos:
  el selector de archivos no debe filtrar a `.exe` (a diferencia del de
  Inicio), porque casos como Minecraft se lanzan vía Java u otros
  launchers con extensión distinta.
- **Todas las apps de un grupo se lanzan a la vez**, en el orden de la
  lista, sin retraso configurable entre una y otra (YAGNI: si hace falta
  más adelante, se añade como iteración separada).
- **Apps compartidas entre grupos, o ya abiertas por el usuario, nunca se
  cierran por sorpresa**: si Discord lo usan tanto el grupo "League of
  Legends" como el grupo "Minecraft" y ambos están abiertos, cerrar uno
  de los dos no debe cerrar Discord — solo se cierra cuando **ningún**
  grupo abierto lo sigue reclamando. Si Discord ya estaba corriendo antes
  de pulsar "Abrir grupo", ese grupo nunca lo toca al cerrarse (se asume
  que el usuario lo gestiona aparte).
- **Cierre amable con margen, no `TerminateProcess` directo**: se le da a
  cada app la oportunidad de cerrarse por su cuenta (como si el usuario
  pulsara la X) antes de forzar.
- **Nueva pestaña "Grupos"** en la barra lateral, no una sub-sección de
  Perfiles.

## Modelo de datos

```
LaunchGroup {
    id: string (uuid)
    name: string                  // "League of Legends"
    entries: [LaunchEntry]
}

LaunchEntry {
    path: string                  // ruta tal cual la eligió el usuario: .exe o .lnk
    resolvedExePath: string       // si path es .lnk, el .exe destino resuelto
    args: string                  // opcional, "" por defecto
}
```

Análogo a `ProfileTypes.h`/`StartupTypes.h`: tipos puros, sin dependencias
de Win32 ni de ImGui, en un nuevo `src/groups/GroupTypes.h`.

## Módulo `src/groups/`

Mismo reparto de responsabilidades que `src/profiles/` y `src/startup/`:

- **`GroupStore`/`GroupJson`** — persistencia de la lista de grupos (solo
  `id`/`name`/`entries`, nunca estado de ejecución) en
  `%LOCALAPPDATA%\MkPCApp\groups.json`. `ProfileJson.cpp` ya trae un
  parser JSON minimalista hecho a mano (`JsonValue`/`JsonParser` en un
  namespace anónimo) — como `groups.json` necesita la misma capacidad de
  anidar objetos/arrays (cada grupo tiene un array `entries`), el plan de
  implementación extrae esa maquinaria genérica a un módulo compartido
  (`platform::MiniJson`) del que tiran tanto `ProfileJson` como
  `GroupJson`, en vez de duplicar ~170 líneas de parser.
- **`ShortcutResolver`** — no es módulo nuevo: `LaunchEntry.resolvedExePath`
  se calcula reutilizando la resolución de `.lnk` que ya existe en
  `startup::ShortcutStartupControl` (vía `IShellLinkW`/`IPersistFile`),
  hoy privada al `.cpp` — el plan la expone como función pública
  (`ResolveShortcutTarget`) y hace que el escaneo de la carpeta Inicio la
  llame también, en vez de mantener dos copias.
- **`GroupProcessTracker`** — nuevo, el único componente sin equivalente
  previo en la app. Mantiene, solo en memoria (nunca persistido):
  - un mapa `resolvedExePath → conjunto de IDs de grupo que lo reclaman
    actualmente abierto`;
  - qué grupos están "abiertos" ahora mismo.
- **`GroupLauncher`** — orquesta abrir/cerrar un grupo (ver flujos abajo),
  usando `CreateProcess` para lanzar y `EnumWindows` + `WM_CLOSE` +
  `TerminateProcess` de respaldo para cerrar.
- **`ui::GroupsTab`** (`src/ui/`) — la pestaña, registrada en
  `Application::Init` junto a `HardwareMonitorTab`/`PerfilesTab`/
  `StartupTab`, sin tocar el resto del shell (mismo patrón `ITab`).

## Flujo "Abrir grupo"

**Corrección tras revisar el diseño al escribir el plan de implementación**:
la primera versión de este flujo trataba "el proceso ya está corriendo" como
un único caso ("externa", nunca reclamada) — eso rompía justo el escenario
de Discord compartido entre LoL y Minecraft que se acordó con el usuario: si
LoL abre Discord y luego se abre Minecraft mientras Discord sigue corriendo,
Minecraft nunca se registraba como copropietario, así que cerrar solo LoL
cerraba Discord de golpe aunque Minecraft siguiera "abierto". La versión
corregida distingue **quién** tiene ya el proceso reclamado:

1. Para cada `LaunchEntry` del grupo, primero se consulta
   `GroupProcessTracker`: ¿esta `resolvedExePath` ya está reclamada por
   **algún otro grupo actualmente abierto**?
   - **Sí** → este grupo se une como copropietario (se añade a la lista de
     dueños de esa ruta) **sin relanzar nada** — el proceso ya está vivo,
     lanzado por el otro grupo. Esta entrada cuenta como "abierta" para
     este grupo a todos los efectos (incluido el cierre, ver abajo).
   - **No** → se comprueba a nivel de sistema
     (`CreateToolhelp32Snapshot`) si ya hay un proceso vivo con esa ruta,
     por una razón que el tracker no conoce (normalmente, que el usuario
     ya la tenía abierta a mano). Si es así → **no se lanza nada** y la
     entrada queda marcada "externa" para este grupo (nunca se registra
     como propiedad suya, así que "Cerrar grupo" nunca la tocará). Si no
     está corriendo en absoluto → se lanza (paso 2).
2. `CreateProcess(path, args)`. El PID resultante se guarda
     (para poder cerrarlo más tarde vía `EnumWindows`/`TerminateProcess`),
     y se inserta este grupo en el conjunto de propietarios de esa
     `resolvedExePath` dentro de `GroupProcessTracker` — el conteo de
     "quién reclama esta app" se lleva por ruta, no por PID, precisamente
     porque una entrada compartida entre grupos corresponde a una sola
     app aunque cada lanzamiento tenga su propio PID.
3. El grupo pasa a estado "abierto" en memoria.
4. Si `CreateProcess` falla para una entrada (ruta movida/borrada,
   permisos), esa entrada se marca con error y **el resto del grupo se
   lanza igual** — un fallo no bloquea a los demás.

## Flujo "Cerrar grupo"

1. Para cada `LaunchEntry` del grupo, se pide a `GroupProcessTracker` que
   quite a este grupo del conjunto de propietarios de esa
   `resolvedExePath`. Esto es seguro de llamar incondicionalmente para
   **todas** las entradas, incluidas las que quedaron "externas" al abrir
   (nunca llegaron a registrarse como propiedad de este grupo, así que
   quitarlas es un no-op) — no hace falta que `GroupsTab` recuerde por
   separado cuáles eran externas.
   - Si el conjunto de propietarios de esa ruta queda vacío tras quitar a
     este grupo (ningún otro grupo abierto la sigue reclamando) → cierre
     amable: `EnumWindows` para localizar las ventanas de nivel superior
     cuyo proceso dueño coincide con el PID rastreado, `WM_CLOSE` a cada
     una, esperar unos segundos, y `TerminateProcess` si el proceso sigue
     vivo tras el margen.
   - Si el conjunto no queda vacío (otro grupo abierto todavía la
     reclama) → no se toca el proceso.
   - Si la ruta no estaba registrada en absoluto para este grupo (entrada
     externa, o el lanzamiento había fallado) → no ocurre nada.
2. El grupo pasa a estado "cerrado" en memoria.

**Limitación conocida, aceptada por diseño**: el estado "abierto/cerrado" y
el conteo de propietarios viven solo en memoria. Si `MkPCApp.exe` se
reinicia mientras un grupo está "abierto", ese estado se pierde — tras
reiniciar, todos los grupos aparecen "cerrados" aunque sus apps sigan
corriendo, y habría que pulsar "Abrir" de nuevo para que el conteo vuelva
a ser correcto. Igual que las cachés de `StartupScanner`, no se persiste
porque el coste de mantenerlo sincronizado con la realidad tras un reinicio
no está justificado para esta primera iteración.

## UI (`ui::GroupsTab`)

Pestaña nueva en la barra lateral, mismo lenguaje visual que Inicio/
Perfiles:

- **Lista de tarjetas**, una por grupo: nombre, iconos en miniatura de
  cada app (reutilizando `startup::IconExtractor` +
  `platform::IconTextureCache`, ya genéricos y no atados a "Inicio"), y
  dos botones — **Abrir grupo** / **Cerrar grupo**. El botón que no aplica
  al estado actual del grupo se muestra deshabilitado.
- **Indicador de estado** por tarjeta: "Abierto"/"Cerrado". Si alguna
  entrada quedó marcada "externa" en el último "Abrir" (corriendo por una
  razón ajena a esta app — normalmente, que el usuario ya la tenía
  abierta), una nota discreta (p. ej. "Discord ya estaba abierto — no se
  cerrará con este grupo"). Una entrada que se unió como copropietaria de
  un grupo ya abierto (p. ej. Minecraft abriéndose mientras LoL ya tiene
  Discord abierto) no lleva esa nota — sí se cerrará cuando corresponda,
  como cualquier entrada que este grupo reclama.
- **Botón "+"** para crear un grupo → editor modal (nombre + lista de
  entradas), reutilizando el selector de archivo nativo que ya usa
  `AddStartupEntryDialog`, pero **sin** el filtro a `.exe` (acepta
  cualquier ejecutable o `.lnk`).
- **Editar/eliminar grupo** por tarjeta; eliminar siempre tras un diálogo
  de confirmación (mismo idioma visual que `ui::ConfirmDeleteDialog`,
  aunque esa clase concreta está acoplada a `startup::StartupEntry` — este
  módulo tiene su propia versión pequeña siguiendo el mismo patrón) — borra
  el grupo de `groups.json`, nunca ningún ejecutable ni acceso directo
  real. Si el grupo estaba "abierto" al eliminarlo, se comporta como si se
  hubiera pulsado "Cerrar grupo" primero (mismo flujo de arriba), para no
  dejar procesos huérfanos en el `GroupProcessTracker`.
- **Añadir/quitar entradas** dentro del editor de un grupo existente.

## Manejo de errores

Mismo principio que el resto de la app: todo fallo se ve, nunca un dato en
blanco sin explicación ni una acción que falla en silencio.

| Situación | Comportamiento |
|---|---|
| `CreateProcess` falla para una entrada (ruta movida/borrada, permisos) | Esa entrada se marca con icono de error + tooltip; el resto del grupo se lanza igual, no se aborta todo por una entrada rota. |
| Acceso directo (`.lnk`) no resuelve (destino borrado) | Mismo tratamiento que ya usa `ShortcutStartupControl` en Inicio: se muestra como entrada rota; "Abrir grupo" la salta con el mismo error de arriba. |
| Ejecutable ya no existe al pulsar "Abrir" | La entrada se deshabilita en el editor/tarjeta con explicación al pasar el ratón, igual que "Abrir ubicación" en Inicio cuando el archivo no existe. |
| Cierre amable no responde tras el margen de espera | Se fuerza `TerminateProcess`; si tampoco responde (proceso protegido/zombi), aviso transitorio "No se pudo cerrar «X»" en vez de bloquear el cierre del resto del grupo. |
| El usuario cancela el diálogo de "eliminar grupo" | No ocurre nada. |
| Grupo eliminado mientras estaba "abierto" | Se ejecuta el flujo de "Cerrar grupo" antes de borrarlo del store, para no dejar propietarios huérfanos en `GroupProcessTracker`. |

## Archivos afectados (nuevos, salvo que se indique lo contrario)

- `src/groups/GroupTypes.h`
- `src/groups/GroupStore.h/.cpp`, `src/groups/GroupJson.h/.cpp`
- `src/groups/GroupProcessTracker.h/.cpp`
- `src/groups/GroupLauncher.h/.cpp`
- `src/ui/GroupsTab.h/.cpp`
- `src/ui/AddGroupEntryDialog.h/.cpp` (o generalizar
  `AddStartupEntryDialog` si el diseño de implementación lo justifica —
  decisión de la fase de plan, no de este spec)
- `src/app/Application.cpp` — registrar `GroupsTab` junto a las demás.
- `CMakeLists.txt` — añadir las nuevas fuentes.
- `docs/PROJECT_STATUS.md` / `docs/ARCHITECTURE.md` — nueva sección
  "Grupos" al implementarse.

## Verificación

Igual que el resto de la app: revisión manual de código desde Linux
(gestión de handles de proceso/`EnumWindows`, que seguir el patrón
`ITab`/módulo existente sea consistente), y prueba real en la máquina
Windows del usuario para: abrir un grupo con 2+ apps las lanza todas;
cerrar un grupo cierra solo lo que él abrió; una app compartida entre dos
grupos abiertos sobrevive al cierre de uno de ellos y se cierra al cerrar
el segundo; una app que el usuario ya tenía abierta antes de "Abrir grupo"
nunca se cierra por la app; el cierre amable le da tiempo a una app real a
cerrarse sola antes de forzar.
