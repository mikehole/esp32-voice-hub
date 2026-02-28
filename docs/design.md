# Design Language: Blue Mono

A cool, cohesive design language that complements the blue aluminium bezel of the Waveshare Knob.

## Philosophy

- **Monochrome blue** — deep navy to cyan gradient, techy and cohesive
- **Hardware harmony** — UI flows seamlessly into the blue physical bezel
- **Clear hierarchy** — selected wedge glows cyan, others recede
- **Wedge segments** — Trivial Pursuit pie-slice layout, 8 per page

## Colour Palette

### Background
| Name | Hex | Usage |
|------|-----|-------|
| Deep Navy | `#0F2744` | Primary background, darkest |
| Navy | `#1E3A5F` | Gradient highlight |
| Muted Navy | `#1A3550` | Unselected wedges |

### Accent (Selection)
| Name | Hex | Usage |
|------|-----|-------|
| Cyan | `#5DADE2` | Selected wedge, active icons |
| Deep Cyan | `#2E86AB` | Selection gradient end |

### Text & Icons
| Name | Hex | Usage |
|------|-----|-------|
| Ice White | `#ECEFF4` | Primary text |
| Cyan | `#5DADE2` | Hint text, unselected icons |
| Dark Navy | `#0F2744` | Icons on selected wedge |

### UI Elements
| Name | Hex | Usage |
|------|-----|-------|
| Active Dot | `#5DADE2` | Page indicator (active) |
| Inactive Dot | `#1E3A5F` | Page indicator (inactive) |
| Gap Line | `#0F2744` | Wedge dividers |

## Typography

- **Primary font:** System sans-serif
- **Weight:** Regular (400)
- **Letter-spacing:** 2px for labels
- **Colour:** Ice white for labels, cyan for hints

## Layout

- **Wedges:** 8 pie-slice segments at 45° each (Trivial Pursuit style)
- **Inner radius:** 75px (centre hub boundary)
- **Outer radius:** 155px (wedge outer edge)
- **Centre hub:** r=65, contains label + hint text
- **Gap lines:** 3px deep navy dividers between wedges
- **Icons:** Positioned at midpoint of each wedge arc (~r=115)
- **Page dots:** Bottom centre

## Selection Indicator

- **Shape:** Full wedge fill with cyan gradient
- **Glow:** Soft cyan blur (5px), blue-tinted
- **Opacity:** 95% selected, 60% unselected

## States

| State | Visual Treatment |
|-------|------------------|
| **Idle** | Default layout, selected wedge cyan |
| **Listening** | Centre pulses cyan, mic icon animates |
| **Thinking** | Subtle rotation or pulse animation |
| **Speaking** | Waveform or ripple effect |
| **Error** | Brief red flash, returns to idle |

## Animation Principles

- **Duration:** 200-400ms for transitions
- **Easing:** Ease-out for selections
- **Haptics:** Light tick on selection change
