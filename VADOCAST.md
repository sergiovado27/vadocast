# vadocast

vadocast es una app de streaming **horizontal y vertical en una sola aplicación**, sin
plugins adicionales. Es un fork de [OBS Studio](https://github.com/obsproject/obs-studio)
32.2.2 que integra, como módulos propios, el trabajo de Aitum:

- [obs-vertical-canvas](https://github.com/Aitum/obs-vertical-canvas) 1.6.6 → `plugins/vadocast-vertical`
- [obs-aitum-multistream](https://github.com/Aitum/obs-aitum-multistream) 1.0.9 → `plugins/vadocast-multistream`

Licencia: GPL-2.0 (igual que OBS y los plugins de Aitum). Ver `COPYING` y los `NOTICE`
de cada módulo.

## Qué cambia respecto a OBS

| Área | vadocast |
| --- | --- |
| Nombre | `vadocast.exe`, título de ventana, textos de la interfaz, ícono y logo propios |
| Configuración | `%APPDATA%\vadocast` (no se mezcla con un OBS instalado) |
| Formatos | Canvas horizontal (principal) + canvas vertical integrado, multistream a varias plataformas |
| Resoluciones | 1280x720 y 1920x1080 horizontal; 720x1280 y 1080x1920 vertical |
| 2K (1440p) | Preparado, apagado por defecto (ver abajo) |
| Plugins de terceros | No se cargan; solo los módulos incluidos con la app |
| Cámara virtual | Eliminada |
| Scripts Lua/Python | Eliminados |
| Modo estudio | Eliminado |
| Actualizador / "Novedades" de OBS | Desactivados (descargarían OBS encima de vadocast) |
| Tema | `vadocast` (rojo, negro y blanco), por defecto |

Las reglas de resolución están en un solo archivo: `shared/vadocast/vadocast-limits.h`.

## Activar 2K (1440p) en el futuro

Compilar con:

```
cmake --preset windows-x64 -DVADOCAST_ENABLE_1440P=ON
```

Esto añade 2560x1440 (horizontal) y 1440x2560 (vertical) a todas las listas. Ningún otro
cambio de código es necesario.

## Compilar (GitHub Actions)

No hace falta instalar nada en tu PC. Cada `push` a `main` (o "Run workflow" en la pestaña
**Actions** del repositorio) compila vadocast para Windows x64 en los servidores de GitHub
(`.github/workflows/vadocast-windows.yaml`, con Visual Studio 2026).

Al terminar, en la ejecución del workflow aparece el artefacto
`vadocast-windows-x64-<commit>`: descárgalo, descomprímelo y abre `bin/64bit/vadocast.exe`.

La versión base de OBS se fija en `CMakePresets.json` (`OBS_VERSION_OVERRIDE`), porque el
repositorio de vadocast no lleva las etiquetas de versión de OBS.

### Compilar en local (opcional)

Requiere Visual Studio 2026 ("Desarrollo de escritorio con C++", Windows SDK 10.0.26100),
CMake 3.28+ y unos 25–40 GB libres. Si el código está en OneDrive, usa una carpeta de
compilación fuera de OneDrive:

```
cmake --preset windows-x64 -B C:/vadocast-build/build_x64
cmake --build C:/vadocast-build/build_x64 --config RelWithDebInfo
```

## Logo

`shared/vadocast/branding/` contiene el logo (SVG, PNG 1024, ICO) y el script que lo genera
(`generate-logo.py`, sin dependencias externas).
