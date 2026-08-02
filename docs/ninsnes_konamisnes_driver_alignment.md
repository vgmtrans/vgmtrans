# NinSnes and KonamiSnes Driver Alignment

This file tracks known playback differences between the value-oriented formats
and the original SPC700 drivers. It is intentionally limited to behavior that
has been confirmed against a driver disassembly; unidentified opcodes and
speculative differences are not listed.

## Remaining work

| Format | Driver behavior | Current behavior | Notes |
| --- | --- | --- | --- |
| NinSnes (Intelligent Systems FE3) | `F5 00` can stall the driver until CPU/APU port 2 changes, `F6` writes port 0, and the CPU can subsequently change the condition byte read by `F7`. | The captured `$00b9` condition selects the initial `F7` path; port handshakes remain zero-time source annotations. | Exact later branch choices require a contemporaneous SNES CPU/APU I/O trace, which an SPC sequence or RSN archive does not contain. |
| Other NinSnes profiles | Sequence commands replace the current DSP ADSR/GAIN envelope. | Outside the Konami profile, operands are parsed but the active articulation is unchanged. | Quintet, Intelligent Systems, and other profile-specific ADSR/GAIN forms remain deferred. |
| KonamiSnes pitch | Later `F0` uses a proportional curve; late `F1` uses its explicit delta; V2 `FC` retargets an active envelope; `E5` adds table-driven random pitch. | Linear `F0`/`F1` timing is represented; these additional curve and modulation details remain source-only or approximated. | Deferred until a small shared curve/modulation representation is agreed. |

## Addressed in the current work

- NinSnes Konami-profile `E6` applies and clears its loop volume/pitch deltas.
- Standard NinSnes `F5`-`F8` apply global EON/EVOL, preserve DSP parameters, and advance signed stereo fades.
- FE3 `F7` branches from the captured `$00b9` condition; later CPU/APU changes remain unavailable offline.
- Early KonamiSnes vibrato uses its tempo-quantized phase step and original direction.
- Konami `F3`, early `F0`, and versioned `F1` use shared pitch transitions; MIDI lowering supplies the per-tick bends.
- Konami `F4`/`F5` apply global EON/EVOL and retain the detected fixed or indexed DSP echo state.
- KonamiSnes `FA`, late `ED`/`FB`, and versioned release commands update the active DSP envelope.

## Driver references

- N-SPC echo: `snes/NSPC/Nintendo/Koji Kondo/Legend of Zelda - A Link to the Past.s`
- N-SPC Konami loop deltas: `snes/NSPC/Konami/Gradius III.s`
- Intelligent Systems conditional jump: `snes/NSPC/Intelligent Systems/Fire Emblem - Monshou no Nazo.s`
- Proprietary Konami vibrato and effects: `snes/Konami/Axelay.s`

The reference files are maintained in
[loveemu/vgm-disasm](https://github.com/loveemu/vgm-disasm).
