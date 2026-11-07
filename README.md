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
