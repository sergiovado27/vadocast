# vadocast UI identity

vadocast is a simple "live studio", not a reduced OBS. Every screen follows these rules.

## Principles

- One panel per task: everything needed to set something up is on one screen (preview + settings + action button).
- Few options. Advanced OBS settings are not shown; sensible defaults are applied instead.
- Plain language in Spanish (default) and English. No OBS jargon ("output", "encoder index", "canvas" → "salida de vídeo", "lienzo" only where unavoidable).
- Never show the words "OBS" or "Aitum" in the interface.

## Colors

| Token | Value | Use |
| --- | --- | --- |
| background | `#0B0B0B` | windows, dialogs |
| panel | `#121212` | side panels, navigation |
| card | `#1A1A1A` | rows, groups (radius 10px, border `#222222`) |
| input | `#1A1A1A` | fields (border `#2C2C2C`, focus `#E50914`) |
| red | `#E50914` | primary actions, selection, live state |
| red light | `#F0303A` | hover |
| red dark | `#B20710` | pressed / danger |
| text | `#FFFFFF` | main text |
| muted | `#969696` | section titles, captions |

## Components (Qt classes, set with `setClasses(widget, "...")` from `qt-wrappers.hpp`)

Defined in `frontend/data/themes/Yami_Vadocast.ovt`:

- `vc-panel`, `vc-card`, `vc-card-selected`, `vc-preview`
- `vc-title`, `vc-section-title` (small grey section headers, e.g. "Micrófonos"), `vc-label`, `vc-caption`, `vc-value`
- `vc-primary` (red button), `vc-secondary`, `vc-live` (button while live), `vc-icon-button`
- `vc-nav` (left navigation lists), `vc-input`, `vc-slider`, `vc-tabs`

## Layout

- 16px outer margins in dialogs, 8–12px spacing between cards.
- Dialog footer: actions aligned right, primary action last (e.g. "Cancelar" · **"Añadir fuente"**).
- Settings-like dialogs: navigation list on the left (`vc-nav`, ~200px), content on the right in cards.
- Sliders show their value as a percentage on the right (`vc-value`).
