# Design Language: Organic Nordic Minimal

A warm, calm, and approachable design language for round displays.

## Philosophy

- **Organic over geometric** — soft curves, no sharp corners
- **Breathing room** — generous whitespace, nothing cramped
- **Warm neutrals** — charcoal with amber/sun accents
- **Subtle hierarchy** — selected items glow, others recede
- **No hard borders** — everything bleeds softly

## Colour Palette

### Background
| Name | Hex | Usage |
|------|-----|-------|
| Nordic Charcoal | `#2C2F33` | Primary background |
| Warm Charcoal | `#3A3835` | Centre hub, cards |
| Deep Charcoal | `#3D4147` | Gradient highlight |

### Accent (Selection)
| Name | Hex | Usage |
|------|-----|-------|
| Sun Gold | `#F5C06A` | Selected icon, primary accent |
| Amber | `#E8A64C` | Gradient mid-point |
| Deep Amber | `#D4923A` | Gradient end, shadows |

### Text & Icons
| Name | Hex | Usage |
|------|-----|-------|
| Cream | `#F5E6D3` | Primary text, selected icons |
| Warm Cream | `#E8D5BC` | Secondary text |
| Muted Stone | `#B5B0A8` | Unselected icons (40% opacity) |
| Soft Grey | `#9A958D` | Hint text, disabled |

### UI Elements
| Name | Hex | Usage |
|------|-----|-------|
| Inactive Dot | `#5C5852` | Page indicators (inactive) |
| Active Dot | `#E8A64C` | Page indicators (active) |

## Typography

- **Primary font:** System sans-serif (Avenir Next, Segoe UI, or fallback)
- **Weight:** Light (300) to Regular (400)
- **Letter-spacing:** 1-2px for labels
- **Case:** Sentence case for labels, no ALL CAPS

## Iconography

- **Style:** Organic, hand-drawn feel
- **Stroke:** 2-2.5px, rounded caps
- **Corners:** Always rounded, never sharp
- **Fill:** Minimal — prefer outlines

## Selection Indicator

- **Shape:** Organic blob/amoeba behind selected icon
- **Opacity:** 30-40%
- **Glow:** Soft gaussian blur (8px), warm-tinted
- **Animation:** Morphs smoothly between positions

## Layout

- **Icons:** 6 positions at 60° intervals, r=140 from centre
- **Centre hub:** r=55, contains label + hint text
- **Page dots:** Bottom centre, organic ellipses
- **Margins:** Generous — icons should not feel crowded

## States

| State | Visual Treatment |
|-------|------------------|
| **Idle** | Default layout, selected item highlighted |
| **Listening** | Centre pulses amber, mic icon animates |
| **Thinking** | Subtle rotation animation on centre |
| **Speaking** | Waveform or speaker icon animation |
| **Error** | Brief red flash, returns to idle |

## Animation Principles

- **Duration:** 200-400ms for transitions
- **Easing:** Ease-out for selections, ease-in-out for morphs
- **Haptics:** Light tick on selection change
