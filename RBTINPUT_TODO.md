# Rabbittus Input TODO (not started)

## Device drivers over Carrottus Input's raw HID (card S68, decided 2026-11-24 with the user)

Carrottus Input gains a raw HID device and stays a translation
(`../carrottus/CRINPUT_TODO.md`). Turning a given pad's reports into a
gamepad (each device family's driver, and a table of mappings) is this
layer's: any program might want it, and Rabbittus may hold a table where
Carrottus may not.

- **The reference:** SDL's HIDAPI drivers (zlib licence) for the report
  layouts, **ported, never linked**, their attribution kept, under
  Rabbittus' rules: no global state, polled from the caller's update, no
  threads of its own. Not SDL3 as a backend (S68's option d), and no SDL3
  stopgap in a caller (option e) unless a caller needs paddles, gyro off
  Apple or the Steam Deck's controls before this exists.
- **Each driver behind its own draft header** for the user's approval
  (root `CLAUDE.md`, card X21), after Carrottus Input's raw HID draft.
- **Next to it:** paddles and extra buttons (card S69), the platform's
  category and a stable identifier (S70), adaptive triggers and played
  haptics (S72), wheels and pedals (S76).

## A pad's family (card S70, decided 2026-11-24 with the user)

Carrottus Input passes through the platform's category string, vendor
and product numbers and a stable identifier
(`../carrottus/CRINPUT_TODO.md`). Turning those into a family (Xbox,
PlayStation, Nintendo, other) for button prompts is a table, so it is
this layer's, with the drivers.

## Wheels' device knowledge (card S76, decided 2026-11-24 with the user)

When wheels are built: which of a wheel's evdev axes is the wheel and
which the pedals (Linux names none), and the vendor command that
switches an Xbox-mode Logitech wheel to HID, are this layer's tables and
drivers, over Carrottus Input's raw device and its evdev force feedback.
