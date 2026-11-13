
 ____  ___ ____  _  __     __     __
|  _ \|_ _/ ___|| |/ /     \ \   / /
| |_) || | \___ \| ' /_____ \ \ / /
|  _ < | |  ___) | . \_____| \ V /
|_| \_\___||____/|_|\_\      \_/




# RISK-V — an RV32I Instruction Set Simulator

A modular, bounds-checked simulator for the RISC-V **RV32I** base integer ISA,
written in strict **C99** (`-std=c99 -Wall -Wextra -Wpedantic`), built with
**GCC + Make**, developed on **Linux** with **Git**.

It emulates the CPU core (fetch → decode → execute → write-back), the 32-entry
register file, and byte-addressable little-endian main memory, and can run real
`riscv32` ELF binaries as well as flat binaries and hand-written hex programs.

```
$ make && ./build/riskv examples/hello.hex
Hello, RISC-V!

$ ./build/riskv -s examples/sum.hex ; echo $?
...
instructions retired : 35
branches taken       : 9 / 10
55

$ ./build/riskv examples/fault.hex
[riskv] TRAP: load access fault at pc=0x80000004 (value=0x00000000) after 1 instructions
[riskv]   faulting instruction: 00002503  lw a0, 0(zero)
```

## Features

* **Complete RV32I** user-level base ISA: LUI/AUIPC, JAL/JALR, all six
  branches, LB/LH/LW/LBU/LHU, SB/SH/SW, all register and immediate ALU ops,
  FENCE, ECALL, EBREAK (40 instructions).
* **Memory safety by construction** — a single bounds-checked access layer:
  overflow-safe range checks (`len <= size - offset`, never `addr + len`),
  strict alignment checking, wrap-around handling, and guest-pointer
  validation inside syscalls (a guest cannot make the host read out of range).
* **Precise traps** — illegal instruction, fetch/load/store access faults,
  misaligned access, misaligned branch target, `ebreak`. The faulting
  instruction commits nothing; `pc` and `trap_value` identify it exactly.
* **Loaders** — ELF32/RISC-V (`PT_LOAD` segments, entry point), flat binary,
  and commented hex text. Segments outside simulated RAM are rejected.
* **Linux-style syscalls** via `ecall` (`a7` = number): `exit` (93),
  `write` (64) with `-EBADF` / `-EFAULT` / `-ENOSYS` error returns.
* **Tooling** — instruction tracer (`-t`), disassembler with pseudo-instructions
  (`li`, `mv`, `ret`, `j`, `nop`), execution statistics (`-s`), register dump
  (`-d`), instruction limit (`-n`), and a gdb-style interactive debugger (`-i`).
* **Verification** — unit tests built on an in-C mini assembler, plus a
  300,000-iteration fuzzer checking invariants (x0 == 0, traps are precise,
  pc stays aligned). `make asan-test` runs everything under ASan + UBSan.

## Architecture

```
            +-----------+     +-----------+     +------------+
 program -->|  loader   |---->|  memory   |<----|  debugger  |
 (ELF/bin/  +-----------+     | (bounds-  |     +------+-----+
  hex)                        |  checked) |            |
                              +-----^-----+            v
                                    |            +-----------+
   +---------+  +---------+   +-----+-----+      |    cpu    |-> trace / stats
   | disasm  |<-| decode  |<--|   cpu     |<-----|  main.c   |
   +---------+  +---------+   | fetch/    |      +-----------+
                              | execute   |
                              +-----------+
```

| File            | Responsibility                                                       |
|-----------------|----------------------------------------------------------------------|
| `src/memory.c`  | Guest RAM; the *only* code that touches the backing store            |
| `src/decode.c`  | Pure `uint32_t -> instr_t` decoder; X-macro opcode table             |
| `src/cpu.c`     | Hart state, execute stage, traps, syscalls, statistics               |
| `src/loader.c`  | ELF32 / binary / hex image loading with validation                   |
| `src/disasm.c`  | Instruction → assembly text                                          |
| `src/debugger.c`| Interactive step/continue/breakpoint/inspect shell                   |
| `src/main.c`    | CLI parsing and exit-status policy                                   |
| `tests/test_cpu.c` | Unit tests + fuzzer                                               |

### Design notes

* **Pure decode.** `decode()` never fails; bad encodings become `OP_ILLEGAL`, and
  the execute stage converts that into a trap. Immediate extraction for
  I/S/B/U/J formats is tested at the extreme offsets (±4 KiB, ±1 MiB).
* **Single source of truth.** One X-macro table defines each instruction's enum
  value, mnemonic, assembly format and statistics class.
* **Portable C99.** No reliance on implementation-defined behaviour: arithmetic
  right shift and signed/unsigned reinterpretation are implemented explicitly,
  and guest memory is assembled byte-by-byte so the host's endianness is irrelevant.
* **Commit-on-success.** Register write-back, `pc` update and counters happen
  only after the execute stage completes without trapping.

## Build & run

```
make              # build/riskv
make test         # unit tests + fuzzer
make asan-test    # same under AddressSanitizer + UBSan
make examples     # run bundled programs
```

```
riskv [options] <program>
  -t  trace every instruction (stderr)     -d  dump registers at exit
  -s  statistics                           -i  interactive debugger
  -n N  stop after N instructions          -m MiB  memory size (default 16)
  -b ADDR  memory base (default 0x80000000)  -f bin|hex|elf  force format
```

Running a real program (needs a RISC-V cross-compiler, e.g. `riscv64-unknown-elf-gcc`):

```
riscv64-unknown-elf-gcc -march=rv32i -mabi=ilp32 -nostdlib -static \
    -Ttext=0x80000000 -o prog.elf prog.S
./build/riskv prog.elf
```
<img width="1261" height="466" alt="image" src="https://github.com/user-attachments/assets/aeaaf8ac-f597-4b44-8924-80714f7aa07e" />
<img width="849" height="335" alt="image" src="https://github.com/user-attachments/assets/7046fe78-0c04-44b9-8119-b0e693c116df" />
<img width="669" height="282" alt="image" src="https://github.com/user-attachments/assets/e8837568-cc6f-4b04-8f90-99acf70f01db" />
<img width="945" height="221" alt="image" src="https://github.com/user-attachments/assets/f8e372aa-2c4c-4adf-a270-2292934d7a4b" />
## Unit Tests and fuzzer
<img width="733" height="311" alt="image" src="https://github.com/user-attachments/assets/e8d4c406-b346-4049-a5af-1411292368ed" />
## : Same tests under AddressSanitizer and UBSan
<img width="540" height="317" alt="image" src="https://github.com/user-attachments/assets/3786260f-cac1-43a8-9175-0527e96cdd98" />
## Running programs, with execution statistics
<img width="450" height="337" alt="image" src="https://github.com/user-attachments/assets/a6d0a382-2e9a-4f8f-adc5-b3a411f90656" />
## Instruction trace with disassembly
<img width="526" height="256" alt="image" src="https://github.com/user-attachments/assets/b1074ec2-598b-4ffb-9e69-3164b5536f2f" />
## A precise trap (memory safety in action)
<img width="481" height="254" alt="image" src="https://github.com/user-attachments/assets/876e1fc4-5073-4af9-9a45-6d19a530d849" />
## Interactive debugger session
<img width="734" height="465" alt="image" src="https://github.com/user-attachments/assets/62ffd694-c2fc-4411-9cb8-723574314fbb" />
## Infinite-loop guard
<img width="575" height="287" alt="image" src="https://github.com/user-attachments/assets/ca443502-2299-4463-abe0-2dd48574011a" />
## Project size and layout
<img width="471" height="405" alt="image" src="https://github.com/user-attachments/assets/fd7d49f8-b931-49ae-8992-d6dbb9a349f4" />

Exit status: the guest's exit code; `134` on a trap; `124` if `-n` limit hit; `1` on usage/load error.

### Debugger

```
(riskv) b 0x80000010     set breakpoint
(riskv) c                continue to breakpoint
(riskv) s 3              step 3 instructions
(riskv) r                registers      (riskv) x 0x80000000 8   memory words
(riskv) stats            (riskv) q
```

## Roadmap

* `M` extension (mul/div), `Zicsr` + machine-mode traps (`mtvec`, `mepc`, `mcause`)
* `C` (compressed) instructions
* Memory-mapped UART and timer; GDB remote serial protocol stub
* Pipeline model (5-stage, hazards, branch predictor) for cycle-accurate statistics
* Run the official `riscv-tests` rv32ui suite

## To explain in simpler terms(If i were to explain to a naiver-person):
Think of a video game console emulator, the kind that lets you play old Nintendo games on your laptop. This project does the same thing, but for a processor chip instead of a console.

The problem

Every computer chip understands its own language. Your laptop’s chip speaks one language, and a chip called RISC-V speaks a different one. A program written for RISC-V can’t run on your laptop, just as a French book is no use to someone who only reads English.

What this project does

RISK-V is a translator that pretends to be a RISC-V chip. You give it a RISC-V program, and it acts out what the real chip would do, step by step, on your normal laptop.

A processor’s job is a simple loop:

Fetch: read the next instruction from memory, like reading the next line of a recipe.
Decode: work out what it means, for example “add these two numbers”.
Execute: do it, and write the result into one of the chip’s tiny notepads called registers.
Repeat until the program is done.

The project recreates each part of that loop in C code. It has the 32 notepads (registers), a block of pretend memory, and the logic that carries out each instruction.

## Why it’s safe

Real chips can crash if a program does something silly, like reading memory that doesn’t exist. This simulator checks every memory access first. If a program tries something illegal, it stops and says exactly what went wrong and where, like a teacher saying “you broke the rule on line 4”. It doesn’t just crash.

A real example

You run the included demo program and it prints Hello, RISC-V!. That message wasn’t produced by a RISC-V chip. Your laptop produced it by pretending to be one, following the program’s instructions one at a time.

## Why I built this

I didn't want to build another web app. Don't get me wrong, I like them, but everyone's portfolio has the same HTML/CSS/JS project, and I wanted something that made me work for it.

So I went the other way and asked what a computer actually does when it runs a program. RISK-V is my answer: a small simulator of a RISC-V processor, written in plain C. It reads instructions one at a time, works out what each one means, and carries it out, the way a real chip would.

It was harder than I expected. I had to learn how instructions are packed into 32 bits, why one wrong bounds check can break everything, and how to test code that has no screen or buttons. Getting "Hello, RISC-V!" to print for the first time was one of the best moments I've had while coding.

Also, I built this to 

To test software before the real chip exists.
To learn how processors work inside.
To debug low-level programs, because you can pause, step through, and inspect everything.

