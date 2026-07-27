# NinSnes and KonamiSnes Driver Alignment

This file tracks known playback differences between the value-oriented formats
and the original SPC700 drivers. It is intentionally limited to behavior that
has been confirmed against a driver disassembly; unidentified opcodes and
speculative differences are not listed.

## Remaining work

| Format | Driver behavior | Current behavior | Notes |
| --- | --- | --- | --- |
| NinSnes (standard N-SPC) | `F7` changes echo delay, feedback, and FIR filter. | Parsed as source-only. | MIDI reverb has no direct representation for these DSP parameters. A richer echo model or an explicit approximation policy is needed. |
| NinSnes (standard N-SPC) | `F8` fades the signed left/right echo volumes. | Parsed as source-only. | Basic `F5` wet level/mask and `F6` disable are implemented. Fade support needs global reverb automation or sampled reverb events. |
| NinSnes (Intelligent Systems FE3) | `F7` jumps only when the driver's per-channel condition bit is clear. | The branch is always taken. | The condition is tied to driver/APU state that is not yet represented by offline sequence playback. Related port wait/write commands also remain source-only or approximate. |
| NinSnes profiles | Sequence commands replace the current DSP ADSR/GAIN envelope. | Operands are parsed, but the active articulation is unchanged. | Deliberately deferred as a larger dynamic-instrument/envelope feature. This includes the Konami, Quintet, Intelligent Systems, and other profile-specific ADSR/GAIN forms. |
| KonamiSnes | `E5` applies a changing pseudo-random pitch mask. | Parsed as source-only. | Requires deterministic emulation of the driver's random state and per-tick pitch updates. |
| KonamiSnes | `F0`, `F1`, and the V2 `FC` form configure portamento or pitch envelopes. | Parsed as source-only. | These are distinct from the already implemented inline `F3` pitch slide. Each version needs its own motion arithmetic. |
| KonamiSnes | `F4`/`F5` change the global echo mask, signed echo levels, delay, feedback, and filter state. | Parsed as source-only. | Requires global cross-track echo state like NinSnes plus an approximation policy for DSP-only parameters. |
| KonamiSnes | Version-specific GAIN and ADSR commands replace the current DSP envelope. | Parsed as source-only. | Deliberately deferred with the NinSnes dynamic ADSR/GAIN work. |

## Addressed in the current work

- NinSnes Konami-profile `E6` now applies its per-replay packed-note volume
  delta and 1/16-semitone pitch delta, then clears both accumulators when the
  loop finishes.
- Standard NinSnes `F5` now applies its EON mask and signed wet-level magnitude,
  and `F6` disables echo globally.
- Early KonamiSnes vibrato now quantizes `rate * tempo` when `E4` executes,
  folds the resulting phase step rather than the raw rate, and preserves the
  triangle's initial direction.

## Driver references

- N-SPC echo: `snes/NSPC/Nintendo/Koji Kondo/Legend of Zelda - A Link to the Past.s`
- N-SPC Konami loop deltas: `snes/NSPC/Konami/Gradius III.s`
- Intelligent Systems conditional jump: `snes/NSPC/Intelligent Systems/Fire Emblem - Monshou no Nazo.s`
- Proprietary Konami vibrato and effects: `snes/Konami/Axelay.s`

The reference files are maintained in
[loveemu/vgm-disasm](https://github.com/loveemu/vgm-disasm).
