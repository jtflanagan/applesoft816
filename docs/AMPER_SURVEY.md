# & libraries and interrupts: a survey

The question: do machine-language clients of AppleSoft keep a variable's
address (VARPNT, or A/Y after PTRGET) and write through it from an
interrupt handler, asynchronously to the interpreter?  If they do, a shadow
copy of AppleSoft's variable memory, synced around each & call, cannot
preserve their behaviour.

## Corpus

71 disk images from the Asimov mirror (list: `~/repos/amper-corpus/manifest.txt`,
images and extracted files beside it, not in this repo), 1,904 files:

- & libraries and extended BASICs: Beagle Bros (Beagle BASIC, Tip Disk 1,
  The Big U, Silicon Salad, Utility City, DOS Boss), Roger Wagner's
  toolboxes (Wizard's, Video, Invisible Tricks, Database, Chart 'n Graph,
  Trial-Size), Apple's Applesoft Toolkit, Peersoft 1.5 (with sources),
  NuInput 2.0, BASIC Booster Library, Basic Enhanced Software Tools,
  Ampermagic, Macrosoft, Enhanced BASIC+, Shortcuts, Take 1, Synergistic.
- Hardware that interrupts: Thunderclock / Thunderclock Plus, Timemaster
  II H.O. (DOS and ProDOS), ProClock, Time II, Mountain clock, AppleTime,
  Clockworks, TimeTrax, NSC, the IIc system clock; Mockingboard (developer
  toolkits, speech) and Phasor; the CPS multifunction card; the graphics
  tablet; a light pen; the Daemon multitasking utility.

## Method

- `tools/a2img.py` extracts every file (DOS 3.3 or ProDOS, either sector
  order).
- `tools/ampscan.py` traces each binary from its load address and from
  every vector it installs (&, USR, $3FE, $FFFE, ProDOS ALLOC_INTERRUPT).
  It records the AppleSoft entry points called and the page zero touched,
  flags variable pointers stored outside page zero, and, for each
  interrupt handler, what the handler reaches.
- `tools/ampgrep.py` is a byte-pattern pass for code the tracer cannot
  follow (relocated images).
- Every candidate was then read by hand: the disassembly, and the source
  where the disk has it.

## Results

943 binaries; 57 install an interrupt handler.  **No interrupt handler
writes, or even reads, an AppleSoft variable.**  Variable pointers kept
past the call turned up in five files; none of them has an interrupt
handler.

How interrupt code and BASIC meet instead:

| Pattern | Example | Under applesoft816 |
|---|---|---|
| Mailbox: the handler sets a byte or counter at a fixed address; BASIC polls it with PEEK | Timemaster BASIC.INTERRUPT (flag at 779) | works: bank-0 memory, as before |
| Deferred event: the handler sets a flag; the interpreter dispatches a BASIC handler at the next statement boundary | Peersoft ON MOUSE/TIMER (MIRQST, COMINT2) | Peersoft replaces the interpreter, so it is out of scope either way; the pattern itself would map to a VM check at OP_STMT |
| The handler owns its data: the & call consumes its arguments before returning | Timemaster/Phasor TTS: `& T,A$` turns the whole string into phonemes, then FRESTR; the interrupt plays its own queue | works with the shadow sync |
| Background tasks with no BASIC contact | Daemon tasks (soft clock, screen clock, keyboard buffer), clock drivers, Mockingboard players | works |
| AppleSoft's page-zero bytes used as the handler's scratch | Mockingboard drivers ($F9 ROT, $D6) | the same clobbering as under the ROM; our VM's state is in its own direct page |

CHRGET wedges appear only in whole-interpreter extensions (Enhanced
BASIC+), which a compiler cannot host. The other wedge hits are FILER and
EDASM using $B1-$C8 as their own page zero.

## Gaps

- No AppleMouse II BASIC disk, no Super Serial Card software, no GS
  toolbox-from-BASIC libraries, no Nibble disks (their archives are
  ShrinkIt; a NuFX reader is needed).
- Static: a relocating image is traced only where the byte patterns or a
  hand reading reach it.
