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

The reference manual defines no ADPCM MML command, and the binary contains no
direct loads of the YM2608 secondary address/data ports `018Ch`/`018Eh`. ADPCM
is therefore not considered a missing MUSIC.COM feature. The manual's hidden
`D:` rhythm part is explicitly discouraged and is outside the normal
compatibility target.

## Differential-trace plan

1. Boot the reference program in a PC-98 emulator with I/O tracing enabled.
2. Use one-command MML probes (one channel at a time) and log every `0188h/018Ah` write with timer ticks.
3. Compare those logs against `musiccom::Event` output and YM2608 writes from `Player`.
4. Probe boundaries first: dotted notes, `Q1/Q8`, ties, tempo changes, nested loops, signed `N/U/I`, and tone changes during a held note.
5. Turn every discovered quirk into a small regression test before changing the native sequencer.

## Sequencer state confirmed from Ver. 2.21

The channel work area is 0x93 bytes.  The command handlers and the timer update
routine expose the following behavior:

- `N` stores a signed magnitude and adjusts the chip frequency number by about
  1/256 semitone (`0B00h`--`0C10h`).
- `P` stores an 8-bit step count.  At each 64th-note timer tick it linearly
  interpolates the FM F-number or SSG period, rather than interpolating MIDI
  note numbers (`1470h`--`1563h`, `12E9h`--`1304h`).
  The previous-note fields initialize to O0 C, so portamento also applies to
  the first note of a track (`0A77h`, `0AB0h`, `0AB4h`).
- `I` and `U` each store amplitude, period, and delay bytes.  Their timer state
  alternates a signed offset as a square wave; zero periods are promoted to one
  tick (`105Dh`--`109Ah`, `1280h`--`12E9h`).
- `S` enables the SSG hardware envelope and stores its shape.  `M` writes the
  16-bit period to registers 11/12.  The shape register is written on key-on so
  the envelope restarts (`0FE6h`--`1010h`, `1691h`--`16AAh`).

These offsets are load addresses for the `.COM` image (file offset plus 0100h).

`tools/analyze_musiccom.py` regenerates the annotated 16-bit listing. The reference binary itself is intentionally not copied into this repository.
