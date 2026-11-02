# WallpaperColor: plan de implementación

Servicio que pinta los componentes RGB del PC con el color del fondo de pantalla que se está viendo,
incluida la diapositiva actual de una presentación de Plasma. Se controla desde una TUI en la terminal;
el servicio funciona solo, sin depender de ella. En OpenLinkHub aparece como un perfil más,
**`wallpaper`**, que se elige por canal desde la WebUI.

---

## 1. Lo que encontramos al investigar

### OpenLinkHub (0.9.4, el que tienes instalado)

| Hallazgo | Consecuencia |
| --- | --- |
| Los perfiles RGB (`static`, `rainbow`, `nebula`…) son **efectos escritos en Go** dentro de OpenLinkHub (`src/rgb/*.go`). La WebUI solo lista los nombres que cada dispositivo declara en `rgbModes`. | **Decidido: parchear OpenLinkHub en local** (carpeta `openlinkhub/`, paquete `openlinkhub-wallpaper`) para añadir el perfil `wallpaper` al desplegable de cada canal. |
| Los bucles de animación de cada dispositivo vuelven a leer el perfil **en cada fotograma**. | El color del perfil `wallpaper` vive en una variable compartida protegida por mutex (`src/rgb/wallpaper.go`). Un color nuevo se ve en el siguiente fotograma, sin reiniciar efectos ni parpadeos, y sin escribir en los mapas de perfiles que las goroutines leen sin bloqueo (eso podría tumbar OpenLinkHub). |
| La API REST `/api/color/all` cambia el perfil `static` guardado; según el código debería enviarlo al hardware, pero en la prueba real los LEDs no cambiaron. | El parche añade su propio endpoint: `POST /api/color/wallpaper` con `{"red","green","blue"}`, y `GET /api/color/wallpaper/current`. El color se guarda en `database/wallpaper.json` y sobrevive a reinicios. |
| El brillo de cada canal sale del deslizador de brillo del dispositivo en la WebUI. | El perfil `wallpaper` lo respeta: el servicio solo manda el tono. |
| OpenLinkHub trae además un servidor OpenRGB SDK (protocolo v4, puerto 6743) con un interruptor "OpenRGB Integration" por dispositivo. | Descartado para OpenLinkHub (el perfil propio es más cómodo). Queda como **sink futuro** para el OpenRGB real (ARGB de la placa Gigabyte). |
| El paquete oficial desactiva el servicio en cada actualización (`pre_upgrade` → `systemctl disable`). | Nuestro `.install` solo lo para y lo vuelve a arrancar si estaba activado. |

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
  inotify ───────┤  └ KDE Plasma    ──▶   └ Vivid (prototipo)  ──▶  └ OpenLinkHub ──▶ POST             │──▶ LEDs
                 │  (futuro: GNOME,       (futuro: paleta          (futuro: OpenRGB   /api/color/      │   (canales en
                 │   Hyprland, swww)       multi-zona, k-means)      SDK :6742)        wallpaper       │    perfil
                 │                                                                                   │    wallpaper)
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
│   ├── wpc-sink/              sinks: OpenLinkHub (HTTP local sobre std::net); futuro: OpenRGB SDK
│   ├── wpc-kde/               fuente Plasma: D-Bus (zbus, API bloqueante) + disparador inotify
│   ├── wallpapercolord/       binario del servicio: bucle de eventos, sin dependencias de terminal
│   └── wallpapercolor/        binario de la TUI (ratatui + crossterm) y subcomandos de línea
├── openlinkhub/               PKGBUILD + parche del perfil wallpaper (paquete openlinkhub-wallpaper)
├── systemd/wallpapercolord.service
└── docs/
```

**Por qué es escalable:** añadir un escritorio nuevo (GNOME, Hyprland) es implementar `Source`. Añadir hardware de otra marca es implementar `Sink` (por ejemplo, el OpenRGB real para la placa). Añadir un estilo de color es implementar `Extractor`. El servicio y la TUI no cambian.

### Decisiones técnicas

| Decisión | Motivo |
| --- | --- |
| **Perfil `wallpaper` parcheado en OpenLinkHub** | Se elige por canal en la WebUI como cualquier otro perfil; el servicio solo manda un POST local con el color. |
| **Sin tokio en el servicio**: un hilo y `poll()` | Objetivo de menos de 5 MB de RAM y 0 % de CPU en reposo. Ninguna de nuestras E/S necesita concurrencia. |
| **HTTP mínimo propio** sobre `std::net::TcpStream` | Una petición POST a 127.0.0.1 por cambio de fondo; no compensa un cliente HTTP completo. |
| **zbus** con la API bloqueante | D-Bus en Rust puro, sin libdbus. |
| **`image` + extracción propia** | Se porta el algoritmo del prototipo (el más vivo de los colores principales, ajustado para LEDs), con tests. |
| **Config TOML** en `~/.config/wallpapercolor/config.toml` | El servicio la recarga al cambiar o por IPC. |
| **ratatui** en lugar de tui-rs | tui-rs está archivado; ratatui es su continuación oficial. |

---

## 3. Fases

Cada fase termina en algo que se puede usar y probar. No se pasa a la siguiente sin cumplir el criterio de salida.

### Fase 0: perfil `wallpaper` en OpenLinkHub

El prototipo falló porque los LEDs no cambiaban, así que esto va primero.

1. Parche sobre OpenLinkHub 0.9.4 (`openlinkhub/0001-wallpaper-profile.patch`): perfil `wallpaper` para Commander Core y RAM, endpoint `POST /api/color/wallpaper`. ✅ Escrito; aplica limpio.
2. Compilar e instalar `openlinkhub-wallpaper` (`makepkg -si`; requiere `go` y sudo). Sustituye al paquete `openlinkhub`.
3. Elegir el perfil `wallpaper` en la WebUI para la RAM y los canales del Commander Core.
4. `curl -X POST http://127.0.0.1:27003/api/color/wallpaper -d '{"red":255,"green":0,"blue":0}'`: **tú confirmas mirando los LEDs**.
5. Toolchain de Rust (`rustup default stable`).

**Criterio de salida:** ves cambiar físicamente la RAM y los ventiladores con cada POST, sin parpadeo.

### Fase 1: núcleo y extracción de color

- Workspace de Cargo, `wpc-core`, configuración TOML.
- Portar la extracción de color con tests (imágenes de prueba con color esperado).
- `wallpapercolor extract <imagen>` imprime el color y muestra una muestra en la terminal.

**Criterio de salida:** mismos colores que el prototipo de Python en tus imágenes.

### Fase 2: sink de OpenLinkHub

- `wpc-sink`: leer dispositivos y qué canales usan el perfil `wallpaper` (`GET /api/devices/`), y enviar el color (`POST /api/color/wallpaper`).
- `wallpapercolor devices` y `wallpapercolor set '#ff0000'`.
- Tests contra un servidor HTTP falso dentro del propio test.

**Criterio de salida:** `set` cambia los LEDs reales.

### Fase 3: el servicio (primer resultado completo)

- `wpc-kde`: imagen actual por D-Bus, señal `wallpaperChanged`, disparador inotify sobre las carpetas de la presentación.
- `wallpapercolord`: bucle de eventos con agrupación de 1 s, caché para no reenviar el mismo color, reintentos si OpenLinkHub se reinicia.
- Unidad de systemd de usuario. Se mide la RAM y la CPU reales.

**Criterio de salida:** con la presentación activa, los LEDs siguen cada diapositiva; menos de 5 MB de RAM y 0 % de CPU en reposo.

### Fase 4: IPC y TUI

- Protocolo por socket Unix (JSON por líneas): `status`, `devices`, `set-strategy`, `pause`/`resume`, `apply-now`.
- TUI con ratatui:
  - fondo actual y paleta extraída (muestras de color);
  - dispositivos y canales con el perfil `wallpaper`;
  - ajustes de saturación mínima y estrategia;
  - pausa.
- Cerrar la TUI no afecta al servicio.

**Criterio de salida:** todo se configura desde la TUI y se guarda en el TOML.

### Fase 5: crecer

- **Paleta multizona:** un color distinto por dispositivo (requiere ampliar el parche: color por dispositivo o canal).
- **Transiciones** suaves entre colores (fundido).
- **Más fuentes:** GNOME, Hyprland/hyprpaper, swww.
- **Más hardware:** sink para el OpenRGB real (puerto 6742), que cubriría el ARGB de la placa Gigabyte.

### Fase 6 (opcional): llevar el perfil upstream

Proponer el parche a OpenLinkHub (generalizado como perfil "external" controlado por API) para dejar de mantener un paquete propio.

### Integración con el rice

Cuando la fase 3 esté estable: `install.sh` del repo `dotfiles` instala `openlinkhub-wallpaper` y el servicio, y `doctor.sh` comprueba ambos. El pacman hook del rice para `openlinkhub` deja de hacer falta con nuestro `.install`.

---

## 4. Riesgos

| Riesgo | Mitigación |
| --- | --- |
| El parche no cambia los LEDs físicamente | La fase 0 lo comprueba antes de escribir Rust, contigo mirando el hardware. |
| Con un paquete propio, OpenLinkHub deja de actualizarse con `pacman -Syu` | En cada versión nueva: subir `_tag` en el PKGBUILD, comprobar que el parche aplica y recompilar. La fase 6 lo elimina. |
| Otro programa abre imágenes de la carpeta y dispara inotify | Solo es un disparador: la verdad viene de D-Bus, así que un evento falso no cambia nada (misma imagen → caché). |
| Plasma cambia la API D-Bus en una versión futura | Está aislado en `wpc-kde`; se adapta sin tocar el resto. |
| Con el servicio parado, los canales en `wallpaper` se quedan en el último color | Comportamiento esperado (como `static`). Se cambia de perfil desde la WebUI. |
