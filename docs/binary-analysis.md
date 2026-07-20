# MUSIC.COM Ver 2.21 compatibility notes

Reference binary: `MML_MAN/MUSIC.COM`, 9,230 bytes, SHA-256
`E08F2AEB09388EFF700B1EB57459EF6C281E40A1E2D458DCA8FFCDD7CC3D9728`.
The copy under `MMLPLAY` is byte-identical.

This is a flat 8086 DOS `.COM`, loaded at offset `0100h`; it is not an EXE wrapper or a visibly packed payload. The entry path calls DOS services directly and contains the string `@Ver 2.21`.

## Initial hardware observations

Linear disassembly identifies the PC-98 OPN/OPNA base ports directly:

- `0172h`, `01B9h`, `021Eh`, `03D3h`, `0458h`, `0DD7h`, `0DEDh`, `0E12h`, `0E59h`, `0E7Bh`, `0EA4h`, `0EB9h`: load `DX=0188h` (address/status port).
- `03E2h`, `0DF4h`, `0E19h`, `0E60h`, `0E84h`: load `DX=018Ah` (data port).
- The executable also touches PC-98 interrupt/timer/keyboard ports `00h`, `02h`, `08h`, `0Ah`, `0Fh`, `60h`, and `64h`.

The cluster at file addresses around `0CD7h`–`0DBCh` is the first target for reconstructing the register-write helpers. Calls into that cluster should be named before relying on the linear disassembly, because embedded tables can otherwise be misidentified as instructions.

## Differential-trace plan

1. Boot the reference program in a PC-98 emulator with I/O tracing enabled.
2. Use one-command MML probes (one channel at a time) and log every `0188h/018Ah` write with timer ticks.
3. Compare those logs against `musiccom::Event` output and YM2608 writes from `Player`.
4. Probe boundaries first: dotted notes, `Q1/Q8`, ties, tempo changes, nested loops, signed `N/U/I`, and tone changes during a held note.
5. Turn every discovered quirk into a small regression test before changing the native sequencer.

`tools/analyze_musiccom.py` regenerates the annotated 16-bit listing. The reference binary itself is intentionally not copied into this repository.
