# WallpaperColor: plan de implementación

Servicio que pinta los componentes RGB del PC con el color del fondo de pantalla que se está viendo,
incluida la diapositiva actual de una presentación de Plasma. Se controla desde una TUI en la terminal;
el servicio funciona solo, sin depender de ella. En OpenLinkHub se elige desde la WebUI con el
interruptor **"OpenRGB Integration"** de cada dispositivo, usando el paquete oficial (sin versión propia).

---

## 1. Lo que encontramos al investigar

### OpenLinkHub (0.9.4, el que tienes instalado)

| Hallazgo | Consecuencia |
| --- | --- |
| OpenLinkHub trae un **servidor OpenRGB SDK** (protocolo v4, puerto 6743, `enableOpenRGBTargetServer`). En la WebUI cada dispositivo tiene un interruptor **"OpenRGB Integration"**: al activarlo, OpenLinkHub deja de aplicar sus efectos a ese dispositivo y acepta colores de un cliente externo. | **Decidido (opción 1): así se elige desde la WebUI**, con el paquete oficial. El servicio será un **cliente OpenRGB SDK**. Es por dispositivo, no por canal. |
| El servidor SDK solo expone los dispositivos con la integración activada: nombre, zonas y número de LEDs (`REQUEST_CONTROLLER_COUNT/DATA`). Los colores se envían con `UPDATELEDS` (`data_size u32`, `count u16`, `count × RGBX`). | **Leer dispositivos** = preguntar al servidor SDK. La lista completa (también los no activados) sale de la API REST `/api/devices/`. |
| Los perfiles RGB (`static`, `rainbow`…) son efectos escritos en Go dentro de OpenLinkHub; un perfil `wallpaper` en el desplegable exige modificarlo. | Hay un parche que compila en `upstream/openlinkhub-wallpaper-profile/`. **No se instala**: queda como propuesta de *pull request* (fase 6). |
| La API REST `/api/color/all` no cambió los LEDs en la prueba real. | Descartada para el color. |

### Plasma

| Hallazgo | Consecuencia |
| --- | --- |
| `org.kde.PlasmaShell.wallpaper(0)` por D-Bus devuelve el campo `Image` con **la diapositiva que se ve ahora mismo**, también en modo presentación (probado: 3 imágenes distintas en 3 cambios). | **Fuente de verdad sin heurísticas.** |
| La señal `wallpaperChanged` **no** se emite al pasar de diapositiva, solo al cambiar la configuración. | Hace falta un **disparador** para los cambios de diapositiva: inotify `IN_OPEN` sobre las carpetas de la presentación (plasmashell abre la imagen nueva). Es un evento del kernel: el servicio duerme en `poll()` sin gastar CPU y, al despertar, pregunta a D-Bus cuál es la imagen. |

### Proyectos afines

- **pywal + OpenRGB** (gist de 4Ply, scripts `pywal-openrgb.py`): generan una paleta del fondo y la mandan a OpenRGB. Son scripts sueltos, no un servicio, y no detectan la diapositiva actual de una presentación.
- **Crates OpenRGB para Rust** (`openrgb`, `openrgb2`, `openrgb-rs2`): asíncronos y atados a tokio. Útiles más adelante para el sink de OpenRGB.
- **tui-rs está archivado desde agosto de 2023.** Su sucesor oficial es **ratatui** (misma API, mantenido). Usaremos ratatui; si se quiere, se importa con el nombre `tui` (`tui = { package = "ratatui" }`).

---

## 2. Arquitectura

Dos programas que se comunican por un socket local. El servicio no sabe nada de terminales.

```text
                 ┌──────────────────────────── wallpapercolord (servicio) ────────────────────────────┐
  Plasma D-Bus ──┤ Source (trait)         Extractor (trait)        Sink (trait)                        │
  inotify ───────┤  └ KDE Plasma    ──▶   └ Vivid (prototipo)  ──▶  └ OpenRGB SDK ──▶ OpenLinkHub      │──▶ LEDs
                 │  (futuro: GNOME,       (futuro: paleta            client         :6743            │   (dispositivos
                 │   Hyprland, swww)       multi-zona, k-means)     (futuro: OpenRGB real :6742)       │    con integración)
                 │                                                                                   │
                 │  bucle de eventos: un solo hilo, poll() sobre inotify + D-Bus + socket IPC         │
                 └───────────────────────────────▲───────────────────────────────────────────────────┘
                                                 │ socket Unix $XDG_RUNTIME_DIR/wallpapercolor.sock
                                                 │ (JSON por líneas: status, devices, set, pause…)
                 ┌───────────────────────────────┴───────────┐
                 │ wallpapercolor (TUI ratatui + subcomandos)│  opcional: el servicio funciona sin ella
                 └───────────────────────────────────────────┘
```

### Estructura del repo

```text
WallpaperColor/
├── Cargo.toml                 workspace
├── crates/
│   ├── wpc-core/              tipos, Config (TOML), extracción de color, protocolo IPC (serde). Sin E/S.
│   ├── wpc-openrgb/           cliente OpenRGB SDK mínimo y síncrono (std::net), protocolo v4
│   ├── wpc-kde/               fuente Plasma: D-Bus (zbus, API bloqueante) + disparador inotify
│   ├── wallpapercolord/       binario del servicio: bucle de eventos, sin dependencias de terminal
│   └── wallpapercolor/        binario de la TUI (ratatui + crossterm) y subcomandos de línea
├── tools/orgb-probe.py        prueba de la fase 0 (Python, desechable)
├── upstream/                  propuestas para otros proyectos (parche del perfil wallpaper)
├── systemd/wallpapercolord.service
└── docs/
```

**Por qué es escalable:** añadir un escritorio nuevo (GNOME, Hyprland) es implementar `Source`. Añadir hardware de otra marca es implementar `Sink` (por ejemplo, el OpenRGB real para la placa). Añadir un estilo de color es implementar `Extractor`. El servicio y la TUI no cambian.

### Decisiones técnicas

| Decisión | Motivo |
| --- | --- |
| **Servidor OpenRGB SDK de OpenLinkHub** en vez de una versión propia | OpenLinkHub sigue actualizándose con pacman; el mismo cliente servirá para el OpenRGB real. |
| **Sin tokio en el servicio**: un hilo y `poll()` | Objetivo de menos de 5 MB de RAM y 0 % de CPU en reposo. Ninguna de nuestras E/S necesita concurrencia. |
| **Cliente OpenRGB SDK propio** (unas 300 líneas) | Los crates existentes obligan a usar tokio y apuntan a v5/v6; OpenLinkHub habla v4. El subconjunto necesario es pequeño. |
| **zbus** con la API bloqueante | D-Bus en Rust puro, sin libdbus. |
| **`image` + extracción propia** | Se porta el algoritmo del prototipo (el más vivo de los colores principales, ajustado para LEDs), con tests. |
| **Config TOML** en `~/.config/wallpapercolor/config.toml` | El servicio la recarga al cambiar o por IPC. |
| **ratatui** en lugar de tui-rs | tui-rs está archivado; ratatui es su continuación oficial. |

---

## 3. Fases

Cada fase termina en algo que se puede usar y probar. No se pasa a la siguiente sin cumplir el criterio de salida.

### Fase 0: validar el hardware (sin Rust) ✅

El prototipo falló porque los LEDs no cambiaban, así que esto va primero.

1. Activar el servidor SDK de OpenLinkHub (`enableOpenRGBTargetServer: true`, requiere sudo y reiniciar el servicio).
2. Activar "OpenRGB Integration" en la RAM y el Commander Core desde la WebUI.
3. `tools/orgb-probe.py list` muestra los dispositivos; `tools/orgb-probe.py set ff0000` los pinta. **Tú confirmas mirando los LEDs.**
4. Toolchain de Rust. ✅ Instalado.

**Criterio de salida:** ves cambiar físicamente la RAM y los ventiladores desde un cliente externo. ✅ Cumplido: rojo en la RAM (2 × 10 LEDs), el AIO y los ventiladores (Commander Core, 61 LEDs).

### Fase 1: núcleo y extracción de color ✅

- Workspace de Cargo, `wpc-core` (color, configuración TOML, extracción) y `wallpapercolor extract <imagen> [-p]`.
- Extracción: muestra de 96×96, paleta de 10 colores por *range cut* (corte a la mitad del rango, no de la población), descarte de grises por **croma** (no saturación, que se dispara en colores muy oscuros) y elección del más vivo.
- El formato de la imagen se detecta por contenido: 37 de los 255 fondos son PNG con extensión `.jpg`.
- 17 tests (colores, conversión HSV, config, casos límite).

**Resultado frente al prototipo (24 fondos al azar):** mediana de 5° de diferencia de tono; 22/24 prácticamente iguales; en los 2 restantes Rust acierta y el prototipo no (comprobado viendo las imágenes). 3,4 veces más rápido (0,9 s frente a 3,1 s) y lee los 255 fondos sin fallos.

### Fase 2: cliente OpenRGB ✅

- `wpc-openrgb`: cliente síncrono solo con `std` (sin tokio): negociación de versión (hasta v4), lista de controladores con zonas y colores actuales, `UPDATELEDS`. Ignora las notificaciones `DEVICE_LIST_UPDATED` que mande el servidor.
- `wallpapercolor devices`, `wallpapercolor set '#hex'` y `set --image <foto>`; dirección del servidor en `[openrgb]` del TOML.
- 6 tests contra un servidor falso que codifica igual que OpenLinkHub (incluidos datos truncados: error, nunca pánico).

**Resultado:** lee tus 3 dispositivos con sus zonas (RAM 2 × 10 LEDs; Commander Core 61 = AIO H150i 29 + 4 ventiladores × 8) y `set --image` con el fondo actual pintó todo de azul cielo. Confirmado mirando el hardware.

### Fase 3: el servicio ✅

- `wpc-kde`: imagen en pantalla por D-Bus (`wallpaper(0)`), paquetes de fondo con variante clara/oscura, señales `wallpaperChanged` y reinicio de plasmashell. zbus 5 con API bloqueante (zbus 6 la eliminó).
- `wallpapercolord`: hilo principal dormido en un canal; hilos auxiliares que también duermen (D-Bus e inotify). Disparadores: archivos abiertos en las carpetas de la presentación, cambios en `plasma-org.kde.plasma.desktop-appletsrc` (cambiar el fondo desde los ajustes **no** siempre emite `wallpaperChanged`), `kdeglobals` y el propio TOML (recarga en caliente). Ráfagas agrupadas (800 ms), caché por imagen + ajustes, reintentos con espera exponencial (2–60 s) si OpenLinkHub o Plasma no responden.
- Modo **arco** por defecto (`[led] mode = "arc" | "single"`): 3 colores distintos (≥ 30° de tono) como degradado por zona, anillos sin costura y RAM de arriba abajo. Con menos colores, tonos claro y oscuro del principal (se probaron tonos vecinos y repetir el color: se descartaron por inventar colores o perder la coherencia de la paleta).
- `systemd/wallpapercolord.service` + `install.sh`.

**Resultado:** sigue la presentación, los cambios manuales de fondo y los cambios desde los ajustes en ~1 s. En reposo: **0 % de CPU** (0 ticks en 30 s) y **5,3 MB** de RAM (7 hilos; el extra sobre los 5 MB lo ponen los hilos internos de zbus).

### Fase 4: IPC y TUI

- Protocolo por socket Unix (JSON por líneas): `status`, `devices`, `set-strategy`, `pause`/`resume`, `apply-now`.
- TUI con ratatui:
  - fondo actual y paleta extraída (muestras de color);
  - dispositivos con la integración activa (y los que no, desde la API REST);
  - ajustes de saturación mínima y estrategia;
  - pausa.
- Cerrar la TUI no afecta al servicio.

**Criterio de salida:** todo se configura desde la TUI y se guarda en el TOML.

### Fase 5: crecer

- **Paleta multizona:** un color distinto por dispositivo o zona (el SDK ya lo permite).
- **Transiciones** suaves entre colores (fundido).
- **Más fuentes:** GNOME, Hyprland/hyprpaper, swww.
- **Más hardware:** sink para el OpenRGB real (puerto 6742), que cubriría el ARGB de la placa Gigabyte.

### Fase 6 (opcional): perfil `wallpaper` upstream

Proponer `upstream/openlinkhub-wallpaper-profile/` a OpenLinkHub (generalizado como perfil "external" controlado por API). Si se acepta, el servicio añade un sink que lo use y se podrá elegir por canal en el desplegable.

### Integración con el rice

Cuando la fase 3 esté estable: `system/setup.sh` del repo `dotfiles` activa `enableOpenRGBTargetServer`, `install.sh` compila e instala el servicio y `doctor.sh` comprueba ambos.

---

## 4. Riesgos

| Riesgo | Mitigación |
| --- | --- |
| El servidor SDK no cambia los LEDs físicamente (como pasó con la REST) | La fase 0 lo comprueba antes de escribir Rust, contigo mirando el hardware. |
| Una actualización de OpenLinkHub cambia o rompe el servidor SDK | El cliente está aislado en `wpc-openrgb`; los tests contra un servidor falso marcan qué cambió. |
| Otro programa abre imágenes de la carpeta y dispara inotify | Solo es un disparador: la verdad viene de D-Bus, así que un evento falso no cambia nada (misma imagen → caché). |
| Plasma cambia la API D-Bus en una versión futura | Está aislado en `wpc-kde`; se adapta sin tocar el resto. |
| Con la integración activa y el servicio parado, los LEDs se quedan en el último color | Se desactiva el interruptor en la WebUI y vuelven a su perfil. |
