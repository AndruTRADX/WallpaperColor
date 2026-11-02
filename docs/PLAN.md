# WallpaperColor: plan de implementación

Servicio que pinta los componentes RGB del PC con el color del fondo de pantalla que se está viendo,
incluida la diapositiva actual de una presentación de Plasma. Se controla desde una TUI en la terminal;
el servicio funciona solo, sin depender de ella.

---

## 1. Lo que encontramos al investigar

### OpenLinkHub (0.9.4, el que tienes instalado)

| Hallazgo | Consecuencia |
| --- | --- |
| Los perfiles RGB (`static`, `rainbow`, `nebula`…) son **efectos escritos en Go** dentro de OpenLinkHub (`src/rgb/*.go`). La WebUI solo lista esos nombres. | **No se puede añadir un perfil "Wallpaper" al desplegable sin modificar OpenLinkHub.** Queda como fase opcional: una contribución upstream (fase 6). |
| OpenLinkHub trae un **servidor OpenRGB SDK** (protocolo v4, puerto 6743, `enableOpenRGBTargetServer`). En la WebUI cada dispositivo tiene un interruptor **"OpenRGB Integration"**: al activarlo, OpenLinkHub deja de aplicar sus efectos a ese dispositivo y acepta colores de un cliente externo. | **Esta es la forma de "elegirlo desde la WebUI".** Activas el interruptor en los dispositivos que quieras que sigan al fondo; lo desactivas y vuelven a sus perfiles. El servicio será un **cliente OpenRGB SDK**. |
| El servidor SDK solo expone los dispositivos con la integración activada, con nombre, zonas y número de LEDs (`REQUEST_CONTROLLER_COUNT/DATA`). | **Leer dispositivos** = preguntarle al servidor SDK. La lista completa (también los no activados) sale de la API REST `/api/devices/`, que la TUI puede mostrar. |
| La API REST `/api/color/all` cambia el perfil `static` guardado y, según el código, debería enviarlo al hardware, pero en la prueba real los LEDs no cambiaron. | Se descarta la REST para el color. Además, el SDK es mejor: no reescribe los JSON de OpenLinkHub en cada cambio y permite colores por LED. |
| Tu Commander Core tiene el canal 3 en `nebula` y los demás en `static`. | Hay que probar el comportamiento con canales mezclados en la fase 0. |

### Plasma

| Hallazgo | Consecuencia |
| --- | --- |
| `org.kde.PlasmaShell.wallpaper(0)` por D-Bus devuelve el campo `Image` con **la diapositiva que se ve ahora mismo**, también en modo presentación (probado: devolvió 3 imágenes distintas en 3 cambios). | **Fuente de verdad sin heurísticas.** No hay que adivinar qué imagen se ve. |
| La señal `wallpaperChanged` **no** se emite al pasar de diapositiva, solo al cambiar la configuración. | Hace falta un **disparador** para los cambios de diapositiva: inotify `IN_OPEN` sobre las carpetas de la presentación (plasmashell abre la imagen nueva). Es un evento del kernel, así que el servicio duerme en `poll()` sin gastar CPU, y al despertar le pregunta a D-Bus cuál es la imagen. |

### Proyectos afines

- **pywal + OpenRGB** (gist de 4Ply, scripts `pywal-openrgb.py`): generan una paleta del fondo y la mandan a OpenRGB. Son scripts sueltos, no un servicio, y no detectan la diapositiva actual de una presentación.
- **Crates OpenRGB para Rust** (`openrgb`, `openrgb2`, `openrgb-rs2`): todos asíncronos y atados a tokio, y usan protocolo v5/v6 (OpenLinkHub habla v4).
- **tui-rs está archivado desde agosto de 2023.** Su sucesor oficial es **ratatui** (misma API, mantenido). Usaremos ratatui; si quieres, se puede importar con el nombre `tui` (`tui = { package = "ratatui" }`).

---

## 2. Arquitectura

Dos programas que se comunican por un socket local. El servicio no sabe nada de terminales.

```
                 ┌──────────────────────────── wallpapercolord (servicio) ─────────────────────────────┐
  Plasma D-Bus ──┤ Source (trait)         Extractor (trait)        Sink (trait)                         │
  inotify ───────┤  └ KDE Plasma    ──▶   └ Vivid (prototipo)  ──▶  └ OpenRGB SDK  ──▶ OpenLinkHub :6743 │──▶ LEDs
                 │  (futuro: GNOME,       (futuro: paleta          (futuro: OpenRGB real :6742,         │
                 │   Hyprland, swww)       multi-zona, k-means)      p. ej. ARGB de la placa)            │
                 │                                                                                    │
                 │  bucle de eventos: un solo hilo, poll() sobre inotify + D-Bus + socket IPC          │
                 └───────────────────────────────▲────────────────────────────────────────────────────┘
                                                 │ socket Unix $XDG_RUNTIME_DIR/wallpapercolor.sock
                                                 │ (JSON por líneas: status, devices, set, pause…)
                 ┌───────────────────────────────┴───────────┐
                 │ wallpapercolor (TUI ratatui + subcomandos)│  opcional: el servicio funciona sin ella
                 └───────────────────────────────────────────┘
```

### Workspace de Cargo

```
WallpaperColor/
├── Cargo.toml                 workspace
├── crates/
│   ├── wpc-core/              tipos, Config (TOML), extracción de color, protocolo IPC (serde). Sin E/S.
│   ├── wpc-openrgb/           cliente OpenRGB SDK mínimo y síncrono (std::net), protocolo v4
│   ├── wpc-kde/               fuente Plasma: D-Bus (zbus, API bloqueante) + disparador inotify
│   ├── wallpapercolord/       binario del servicio: bucle de eventos, sin dependencias de terminal
│   └── wallpapercolor/        binario de la TUI (ratatui + crossterm) y subcomandos de línea
├── systemd/wallpapercolord.service
└── docs/
```

**Por qué es escalable:** añadir un escritorio nuevo (GNOME, Hyprland) es implementar `Source`. Añadir hardware de otra marca es implementar `Sink` (por ejemplo, el OpenRGB real para la placa Gigabyte). Añadir un estilo de color es implementar `Extractor`. El servicio y la TUI no cambian.

### Decisiones técnicas

| Decisión | Motivo |
| --- | --- |
| **Sin tokio en el servicio**: un hilo y `poll()` | Objetivo de menos de 5 MB de RAM y 0 % de CPU en reposo. Ninguna de nuestras E/S necesita concurrencia. |
| **Cliente OpenRGB SDK propio** (unas 300 líneas) | Los crates existentes obligan a usar tokio y apuntan a v5/v6; OpenLinkHub habla v4. El subconjunto que necesitamos es pequeño y está documentado. |
| **zbus** con la API bloqueante | D-Bus en Rust puro, sin libdbus. |
| **`image` + extracción propia** | Se porta el algoritmo del prototipo de Python (el más vivo de los colores principales, ajustado para LEDs), con tests. |
| **Config TOML** en `~/.config/wallpapercolor/config.toml` | El servicio la recarga al cambiar (inotify) o por IPC. |
| **ratatui** en lugar de tui-rs | tui-rs está archivado; ratatui es su continuación oficial. |

---

## 3. Fases

Cada fase termina en algo que se puede usar y probar. No se pasa a la siguiente sin cumplir el criterio de salida.

### Fase 0: validar el hardware (sin Rust)

El prototipo falló porque los LEDs no cambiaban, así que esto va primero.

1. Activar el servidor SDK de OpenLinkHub (`enableOpenRGBTargetServer: true`, requiere sudo y reiniciar el servicio).
2. Activar "OpenRGB Integration" en la RAM desde la WebUI.
3. Enviar un color con un script mínimo de prueba al puerto 6743. **Tú confirmas mirando los LEDs.**
4. Repetir con el Commander Core (incluido el canal en `nebula`).
5. Instalar Rust (`sudo pacman -S rust`).

**Criterio de salida:** ves cambiar físicamente la RAM y los ventiladores desde un cliente externo.

### Fase 1: núcleo y extracción de color

- Workspace de Cargo, `wpc-core`, configuración TOML.
- Portar la extracción de color con tests (imágenes de prueba con color esperado).
- `wallpapercolor extract <imagen>` imprime el color y muestra una muestra en la terminal.

**Criterio de salida:** mismos colores que el prototipo de Python en tus imágenes.

### Fase 2: cliente OpenRGB

- `wpc-openrgb`: conectar, leer controladores (nombre, zonas, LEDs) y enviar colores.
- `wallpapercolor devices` y `wallpapercolor set '#ff0000'`.
- Tests contra un servidor SDK falso dentro del propio test.

**Criterio de salida:** `set` cambia los LEDs reales.

### Fase 3: el servicio (primer resultado completo)

- `wpc-kde`: imagen actual por D-Bus, señal `wallpaperChanged`, disparador inotify sobre las carpetas de la presentación.
- `wallpapercolord`: bucle de eventos con agrupación de 1 s, caché para no reenviar el mismo color, reconexión si OpenLinkHub se reinicia.
- Unidad de systemd de usuario. Se mide la RAM y la CPU reales.

**Criterio de salida:** con la presentación activa, los LEDs siguen cada diapositiva; menos de 5 MB de RAM y 0 % de CPU en reposo.

### Fase 4: IPC y TUI

- Protocolo por socket Unix (JSON por líneas): `status`, `devices`, `set-strategy`, `brightness`, `pause`/`resume`, `apply-now`.
- TUI con ratatui:
  - fondo actual y paleta extraída (muestras de color);
  - dispositivos con su estado (integración activa o no);
  - ajustes de brillo, saturación mínima y estrategia;
  - pausa.
- Cerrar la TUI no afecta al servicio.

**Criterio de salida:** todo se configura desde la TUI y se guarda en el TOML.

### Fase 5: crecer

- **Paleta multizona:** un color distinto por dispositivo (RAM = dominante, ventiladores = secundario…).
- **Transiciones** suaves entre colores (fundido).
- **Más fuentes:** GNOME, Hyprland/hyprpaper, swww.
- **Más hardware:** sink para el OpenRGB real (puerto 6742), que cubriría el ARGB de la placa Gigabyte.

### Fase 6 (opcional): perfil "Wallpaper" en el desplegable de OpenLinkHub

Proponer upstream un perfil "external" controlado por API, para que aparezca como un perfil más en la WebUI en lugar del interruptor de integración.

### Integración con el rice

Cuando la fase 3 esté estable: `install.sh` del repo `dotfiles` compila e instala el servicio, y `doctor.sh` comprueba que está corriendo.

---

## 4. Riesgos

| Riesgo | Mitigación |
| --- | --- |
| El servidor SDK de OpenLinkHub no cambia los LEDs físicamente (como pasó con la REST) | La fase 0 lo comprueba antes de escribir Rust. Si falla, se investiga el envío por dispositivo de la REST (`/api/color/change`) o se abre un issue upstream. |
| Otro programa abre imágenes de la carpeta y dispara inotify | Solo es un disparador: la verdad viene de D-Bus, así que un evento falso no cambia nada (misma imagen → caché). |
| Plasma cambia la API D-Bus en una versión futura | Está aislado en `wpc-kde`; se adapta sin tocar el resto. |
| Con la integración activa y el servicio parado, los LEDs se quedan en el último color | Documentado. Se desactiva desde la WebUI o el servicio restaura un color al salir. |
