# GUIDs de plan de energía de Windows (`powercfg /q`)

Referencia para verificar los GUIDs de subgrupo/ajuste usados en
`src/profiles/SystemControl/PowerTimeouts.cpp` (y cualquier variable de
energía nueva que se añada en el futuro). Los GUIDs de subgrupo y ajuste son
fijos en Windows (no dependen del esquema activo) — el volcado de abajo se
tomó con `powercfg /q` contra el esquema activo `b5bf1979-f197-43c8-a4a6-3921e8314c15`
("MkPCApp Ultimate Performance") el 2026-07-12, y sirve para contrastar
cualquier GUID que se hardcodee en el código nativo, dado que ese código se
escribe desde un entorno Linux sin poder compilar/consultar `powercfg`
directamente.

## Ya usados en el código

- **Pantalla** (`SUB_VIDEO`) — `7516b95f-f776-4464-8c53-06167f40cc99`
  - Apagar la pantalla tras (`VIDEOIDLE`) — `3c0bc021-c8a8-4e07-a973-6b14cbcb2b7e`
    → `PowerTimeouts::SetScreenOffTimeouts`/`GetScreenOffTimeout*`.
- **Suspender** (`SUB_SLEEP`) — `238c9fa8-0aad-41ed-83f4-97be242c8f20`
  - Suspender tras (`STANDBYIDLE`) — `29f6c1db-86da-48c5-9fdb-f2b67b1f44da`
    → `PowerTimeouts::SetSleepTimeouts`/`GetSleepTimeout*`.
  - Hibernar tras (`HIBERNATEIDLE`) — `9d7815a6-7ee4-497e-8888-515a05f02364`
    → `PowerTimeouts::SetHibernateTimeouts`/`GetHibernateTimeout*`. Confirmado
    contra el volcado de abajo — coincide con el valor que ya estaba
    hardcodeado en el código antes de esta verificación.

## Volcado completo (`powercfg /q`)

Guardado íntegro para cualquier variable de energía futura (USB, procesador,
batería, PCI Express, etc.) sin tener que volver a pedirle al usuario que lo
extraiga de su máquina.

```
GUID de subgrupo: 0012ee47-9041-4b5d-9b77-535fba8b1442  (Disco duro)
  Alias de GUID: SUB_DISK
  GUID de configuración de energía: 6738e2c4-e8a5-4a42-b16a-e040e769756e  (Apagar disco duro tras)
    Alias de GUID: DISKIDLE
    Mínima configuración posible: 0x00000000
    Máxima configuración posible: 0xffffffff
    Incremento de configuración posible: 0x00000001
    Unidades de configuración posibles: Segundos:
  Índice de configuración de corriente alterna actual: 0x00000000
  Índice de configuración de corriente continua actual: 0x00000000

GUID de subgrupo: 02f815b5-a5cf-4c84-bf20-649d1f75d3d8  (Internet Explorer)
  GUID de configuración de energía: 4c793e7d-a264-42e1-87d3-7a0d2f523ccd  (Frecuencia de temporizador de JavaScript)
    Índice de configuración posible: 000
    Nombre descriptivo de configuración posible: Ahorro de energía máximo
    Índice de configuración posible: 001
    Nombre descriptivo de configuración posible: Rendimiento máximo
  Índice de configuración de corriente alterna actual: 0x00000001
  Índice de configuración de corriente continua actual: 0x00000000

GUID de subgrupo: 0d7dbae2-4294-402a-ba8e-26777e8488cd  (Configuración del fondo de escritorio)
  GUID de configuración de energía: 309dce9b-bef4-4119-9921-a851fb12f0f4  (Presentación)
    Índice de configuración posible: 000
    Nombre descriptivo de configuración posible: Disponible
    Índice de configuración posible: 001
    Nombre descriptivo de configuración posible: En pausa
  Índice de configuración de corriente alterna actual: 0x00000000
  Índice de configuración de corriente continua actual: 0x00000001

GUID de subgrupo: 19cbb8fa-5279-450e-9fac-8a3d5fedd0c1  (Configuración de adaptador inalámbrico)
  GUID de configuración de energía: 12bbebe6-58d6-4636-95bb-3217ef867c1a  (Modo de ahorro de energía)
    Índice de configuración posible: 000
    Nombre descriptivo de configuración posible: Rendimiento máximo
    Índice de configuración posible: 001
    Nombre descriptivo de configuración posible: Ahorro de energía bajo
    Índice de configuración posible: 002
    Nombre descriptivo de configuración posible: Ahorro de energía medio
    Índice de configuración posible: 003
    Nombre descriptivo de configuración posible: Ahorro de energía máximo
  Índice de configuración de corriente alterna actual: 0x00000000
  Índice de configuración de corriente continua actual: 0x00000002

GUID de subgrupo: 238c9fa8-0aad-41ed-83f4-97be242c8f20  (Suspender)
  Alias de GUID: SUB_SLEEP
  GUID de configuración de energía: 29f6c1db-86da-48c5-9fdb-f2b67b1f44da  (Suspender tras)
    Alias de GUID: STANDBYIDLE
    Mínima configuración posible: 0x00000000
    Máxima configuración posible: 0xffffffff
    Incremento de configuración posible: 0x00000001
    Unidades de configuración posibles: Segundos:
  Índice de configuración de corriente alterna actual: 0x00000258
  Índice de configuración de corriente continua actual: 0x00000258

  GUID de configuración de energía: 94ac6d29-73ce-41a6-809f-6363ba21b47e  (Permitir suspensión híbrida)
    Alias de GUID: HYBRIDSLEEP
    Índice de configuración posible: 000
    Nombre descriptivo de configuración posible: Desactivar
    Índice de configuración posible: 001
    Nombre descriptivo de configuración posible: Activar
  Índice de configuración de corriente alterna actual: 0x00000001
  Índice de configuración de corriente continua actual: 0x00000001

  GUID de configuración de energía: 9d7815a6-7ee4-497e-8888-515a05f02364  (Hibernar tras)
    Alias de GUID: HIBERNATEIDLE
    Mínima configuración posible: 0x00000000
    Máxima configuración posible: 0xffffffff
    Incremento de configuración posible: 0x00000001
    Unidades de configuración posibles: Segundos:
  Índice de configuración de corriente alterna actual: 0x00000000
  Índice de configuración de corriente continua actual: 0x00000000

  GUID de configuración de energía: bd3b718a-0680-4d9d-8ab2-e1d2b4ac806d  (Permitir temporizadores de reactivación)
    Alias de GUID: RTCWAKE
    Índice de configuración posible: 000
    Nombre descriptivo de configuración posible: Deshabilitar
    Índice de configuración posible: 001
    Nombre descriptivo de configuración posible: Habilitar
    Índice de configuración posible: 002
    Nombre descriptivo de configuración posible: Solo temporizadores de activación importantes
  Índice de configuración de corriente alterna actual: 0x00000001
  Índice de configuración de corriente continua actual: 0x00000000

GUID de subgrupo: 2a737441-1930-4402-8d77-b2bebba308a3  (Configuración de USB)
  GUID de configuración de energía: 48e6b7a6-50f5-4782-a5d4-53bb8f07e226  (Configuración de suspensión selectiva de USB)
    Índice de configuración posible: 000
    Nombre descriptivo de configuración posible: Deshabilitado
    Índice de configuración posible: 001
    Nombre descriptivo de configuración posible: Habilitado
  Índice de configuración de corriente alterna actual: 0x00000001
  Índice de configuración de corriente continua actual: 0x00000001

GUID de subgrupo: 4f971e89-eebd-4455-a8de-9e59040e7347  (Botones de inicio/apagado y tapa)
  Alias de GUID: SUB_BUTTONS
  GUID de configuración de energía: a7066653-8d6c-40a8-910e-a1f54b84c7e5  (Botón de inicio/apagado del menú Inicio)
    Alias de GUID: UIBUTTON_ACTION
    Índice de configuración posible: 000
    Nombre descriptivo de configuración posible: Suspender
    Índice de configuración posible: 001
    Nombre descriptivo de configuración posible: Hibernar
    Índice de configuración posible: 002
    Nombre descriptivo de configuración posible: Apagar
  Índice de configuración de corriente alterna actual: 0x00000000
  Índice de configuración de corriente continua actual: 0x00000000

GUID de subgrupo: 501a4d13-42af-4429-9fd1-a8218c268e20  (PCI Express)
  Alias de GUID: SUB_PCIEXPRESS
  GUID de configuración de energía: ee12f906-d277-404b-b6da-e5fa1a576df5  (Administración de energía del estado de vínculos)
    Alias de GUID: ASPM
    Índice de configuración posible: 000
    Nombre descriptivo de configuración posible: Desactivar
    Índice de configuración posible: 001
    Nombre descriptivo de configuración posible: Ahorro moderado de energía
    Índice de configuración posible: 002
    Nombre descriptivo de configuración posible: Máximo ahorro de energía
  Índice de configuración de corriente alterna actual: 0x00000000
  Índice de configuración de corriente continua actual: 0x00000002

GUID de subgrupo: 54533251-82be-4824-96c1-47b60b740d00  (Administración de energía del procesador)
  Alias de GUID: SUB_PROCESSOR
  GUID de configuración de energía: 893dee8e-2bef-41e0-89c6-b55d0929964c  (Estado mínimo del procesador)
    Alias de GUID: PROCTHROTTLEMIN
    Mínima configuración posible: 0x00000000
    Máxima configuración posible: 0x00000064
    Incremento de configuración posible: 0x00000001
    Unidades de configuración posibles: %
  Índice de configuración de corriente alterna actual: 0x00000064
  Índice de configuración de corriente continua actual: 0x00000005

  GUID de configuración de energía: bc5038f7-23e0-4960-96da-33abaf5935ec  (Estado máximo del procesador)
    Alias de GUID: PROCTHROTTLEMAX
    Mínima configuración posible: 0x00000000
    Máxima configuración posible: 0x00000064
    Incremento de configuración posible: 0x00000001
    Unidades de configuración posibles: %
  Índice de configuración de corriente alterna actual: 0x00000064
  Índice de configuración de corriente continua actual: 0x00000064

GUID de subgrupo: 7516b95f-f776-4464-8c53-06167f40cc99  (Pantalla)
  Alias de GUID: SUB_VIDEO
  GUID de configuración de energía: 3c0bc021-c8a8-4e07-a973-6b14cbcb2b7e  (Apagar la pantalla tras)
    Alias de GUID: VIDEOIDLE
    Mínima configuración posible: 0x00000000
    Máxima configuración posible: 0xffffffff
    Incremento de configuración posible: 0x00000001
    Unidades de configuración posibles: Segundos:
  Índice de configuración de corriente alterna actual: 0x00000708
  Índice de configuración de corriente continua actual: 0x00000384

  GUID de configuración de energía: aded5e82-b909-4619-9949-f5d71dac0bcb  (Brillo de pantalla)
    Alias de GUID: VIDEONORMALLEVEL
    Mínima configuración posible: 0x00000000
    Máxima configuración posible: 0x00000064
    Incremento de configuración posible: 0x00000001
    Unidades de configuración posibles: %
  Índice de configuración de corriente alterna actual: 0x00000064
  Índice de configuración de corriente continua actual: 0x0000004b

  GUID de configuración de energía: f1fbfde2-a960-4165-9f88-50667911ce96  (Brillo de la pantalla atenuada)
    Mínima configuración posible: 0x00000000
    Máxima configuración posible: 0x00000064
    Incremento de configuración posible: 0x00000001
    Unidades de configuración posibles: %
  Índice de configuración de corriente alterna actual: 0x00000032
  Índice de configuración de corriente continua actual: 0x00000032

  GUID de configuración de energía: fbd9aa66-9553-4097-ba44-ed6e9d65eab8  (Habilitar brillo adaptable)
    Alias de GUID: ADAPTBRIGHT
    Índice de configuración posible: 000
    Nombre descriptivo de configuración posible: Desactivar
    Índice de configuración posible: 001
    Nombre descriptivo de configuración posible: Activar
  Índice de configuración de corriente alterna actual: 0x00000000
  Índice de configuración de corriente continua actual: 0x00000000

GUID de subgrupo: 9596fb26-9850-41fd-ac3e-f7c3c00afd4b  (Configuración multimedia)
  GUID de configuración de energía: 03680956-93bc-4294-bba6-4e0f09bb717f  (Al compartir multimedia)
    Índice de configuración posible: 000
    Nombre descriptivo de configuración posible: Permitir que el equipo entre en modo de suspensión
    Índice de configuración posible: 001
    Nombre descriptivo de configuración posible: Prevenir la suspensión debida a inactividad
    Índice de configuración posible: 002
    Nombre descriptivo de configuración posible: Permitir que el equipo entre en modo Ausente
  Índice de configuración de corriente alterna actual: 0x00000001
  Índice de configuración de corriente continua actual: 0x00000000

  GUID de configuración de energía: 10778347-1370-4ee0-8bbd-33bdacaade49  (Compensación de calidad de reproducción de vídeo)
    Índice de configuración posible: 000
    Nombre descriptivo de configuración posible: Compensación de ahorro de energía de reproducción de vídeo
    Índice de configuración posible: 001
    Nombre descriptivo de configuración posible: Compensación de rendimiento de reproducción de vídeo
  Índice de configuración de corriente alterna actual: 0x00000001
  Índice de configuración de corriente continua actual: 0x00000000

  GUID de configuración de energía: 34c7b99f-9a6d-4b3c-8dc7-b6693b78cef4  (Al reproducir vídeo)
    Índice de configuración posible: 000
    Nombre descriptivo de configuración posible: Optimizar calidad de vídeo
    Índice de configuración posible: 001
    Nombre descriptivo de configuración posible: Equilibrado
    Índice de configuración posible: 002
    Nombre descriptivo de configuración posible: Optimizar ahorro de energía
  Índice de configuración de corriente alterna actual: 0x00000000
  Índice de configuración de corriente continua actual: 0x00000001

GUID de subgrupo: e73a048d-bf27-4f12-9731-8b2076e8891f  (Batería)
  Alias de GUID: SUB_BATTERY
  GUID de configuración de energía: 5dbb7c9f-38e9-40d2-9749-4f8a0e9f640f  (Notificación de nivel crítico de batería)
    Alias de GUID: BATFLAGSCRIT
    Índice de configuración posible: 000
    Nombre descriptivo de configuración posible: Desactivar
    Índice de configuración posible: 001
    Nombre descriptivo de configuración posible: Activar
  Índice de configuración de corriente alterna actual: 0x00000001
  Índice de configuración de corriente continua actual: 0x00000001

  GUID de configuración de energía: 637ea02f-bbcb-4015-8e2c-a1c7b9c0b546  (Acción de nivel crítico de batería)
    Alias de GUID: BATACTIONCRIT
    Índice de configuración posible: 000
    Nombre descriptivo de configuración posible: No hacer nada
    Índice de configuración posible: 001
    Nombre descriptivo de configuración posible: Suspender
    Índice de configuración posible: 002
    Nombre descriptivo de configuración posible: Hibernar
    Índice de configuración posible: 003
    Nombre descriptivo de configuración posible: Apagar
  Índice de configuración de corriente alterna actual: 0x00000002
  Índice de configuración de corriente continua actual: 0x00000002

  GUID de configuración de energía: 8183ba9a-e910-48da-8769-14ae6dc1170a  (Nivel de batería baja)
    Alias de GUID: BATLEVELLOW
    Mínima configuración posible: 0x00000000
    Máxima configuración posible: 0x00000064
    Incremento de configuración posible: 0x00000001
    Unidades de configuración posibles: %
  Índice de configuración de corriente alterna actual: 0x0000000a
  Índice de configuración de corriente continua actual: 0x0000000a

  GUID de configuración de energía: 9a66d8d7-4ff7-4ef9-b5a2-5a326ca2a469  (Nivel crítico de batería)
    Alias de GUID: BATLEVELCRIT
    Mínima configuración posible: 0x00000000
    Máxima configuración posible: 0x00000064
    Incremento de configuración posible: 0x00000001
    Unidades de configuración posibles: %
  Índice de configuración de corriente alterna actual: 0x00000005
  Índice de configuración de corriente continua actual: 0x00000005

  GUID de configuración de energía: bcded951-187b-4d05-bccc-f7e51960c258  (Notificación de batería baja)
    Alias de GUID: BATFLAGSLOW
    Índice de configuración posible: 000
    Nombre descriptivo de configuración posible: Desactivar
    Índice de configuración posible: 001
    Nombre descriptivo de configuración posible: Activar
  Índice de configuración de corriente alterna actual: 0x00000001
  Índice de configuración de corriente continua actual: 0x00000001

  GUID de configuración de energía: d8742dcb-3e6a-4b3c-b3fe-374623cdcf06  (Acción de batería baja)
    Alias de GUID: BATACTIONLOW
    Índice de configuración posible: 000
    Nombre descriptivo de configuración posible: No hacer nada
    Índice de configuración posible: 001
    Nombre descriptivo de configuración posible: Suspender
    Índice de configuración posible: 002
    Nombre descriptivo de configuración posible: Hibernar
    Índice de configuración posible: 003
    Nombre descriptivo de configuración posible: Apagar
  Índice de configuración de corriente alterna actual: 0x00000000
  Índice de configuración de corriente continua actual: 0x00000000

  GUID de configuración de energía: f3c5027d-cd16-4930-aa6b-90db844a8f00  (Nivel de batería de reserva)
    Mínima configuración posible: 0x00000000
    Máxima configuración posible: 0x00000064
    Incremento de configuración posible: 0x00000001
    Unidades de configuración posibles: %
  Índice de configuración de corriente alterna actual: 0x00000007
  Índice de configuración de corriente continua actual: 0x00000007
```
