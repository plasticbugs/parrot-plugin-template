#!/usr/bin/env python3
"""armcount.py BENCH.elf [--lists N] [--from N] [--load SYMBOL=FILE ...]
                [--each] [--profile] [--hot FUNCTION]

How long the cart's Cortex-M33 takes over each list of a plugin's work.
The simulator and the models run on the computer, far faster than the
cart, so they can't say whether a plugin keeps up. This runs the plugin's
own code, built for the M33 (tools/armcount/build.sh, from a bench like
tools/armcount/bench.c), in Unicorn, and charges each instruction the
M33's time for it from Arm's technical reference manual: most 1 cycle, a
load 2, a load or store of several registers 1 more each, a taken branch
or a call 3, a divide 6, the FPU's divide and square root 14. Everything
is in SRAM at 150 MHz. What it leaves out is the bus when core 1 or the
DMA uses the same RAM bank, so the plugin's own count of late lists on
the cart is the last word.

A list is 16.77 ms of the console's time, 2.52 million cycles. The
plugin makes each list as soon as the last is published, and the SDK
holds one more, so a slow list is fine if those before it were quick.
The console runs a list again (its sound once more) only when the lists
so far took more than their time plus one list's. "late" counts those.

  --lists N        run N lists (default 600, ten seconds)
  --from N         count from list N on (default 1); the ones before run fast
  --load SYM=FILE  write FILE into the global SYM before the start (a ROM set)
  --each           print every list's count
  --profile        the cycles by function
  --hot FUNCTION   that function's costliest blocks, disassembled

Needs unicorn, capstone and pyelftools (tools/armcount/requirements.txt,
in a venv; docs/testing.md). Two things Unicorn gets wrong unless told:
it must be the ARMv8 model with the FPU's v8 instructions
(UC_CPU_ARM_MAX: the M33's vsel and vminnm fault otherwise), and code it
has already translated keeps running without a hook added later, so the
hooks are added before the counted lists and the cache is flushed
(ctl_flush_tb) each time they change. Without the flush the counts come
out far too low.
"""
import bisect
import re
import sys

try:
    from capstone import CS_ARCH_ARM, CS_MODE_MCLASS, CS_MODE_THUMB, Cs
    from elftools.elf.elffile import ELFFile
    from unicorn import UC_ARCH_ARM, UC_HOOK_BLOCK, UC_MODE_THUMB, Uc
    from unicorn.arm_const import UC_ARM_REG_C1_C0_2, UC_ARM_REG_FPEXC, UC_ARM_REG_LR, UC_CPU_ARM_MAX
except ImportError as e:
    sys.exit(f"armcount.py: {e}. It needs unicorn, capstone and pyelftools: "
             "python3 -m venv ~/armcount-venv && ~/armcount-venv/bin/pip install -r tools/armcount/requirements.txt")

LIST_CYCLES = 2_516_000         # 16.77 ms at 150 MHz
MHZ = 150


def options(argv):
    o = {"lists": 600, "from": 1, "load": [], "each": False, "profile": False, "hot": None}
    args = argv[1:]
    if not args or args[0] in ("-h", "--help"):
        sys.exit(__doc__)
    o["elf"] = args.pop(0)
    while args:
        a = args.pop(0)
        if a in ("--lists", "--from"):
            o[a[2:]] = int(args.pop(0))
        elif a == "--load":
            sym, _, path = args.pop(0).partition("=")
            o["load"].append((sym, path))
        elif a in ("--each", "--profile"):
            o[a[2:]] = True
        elif a == "--hot":
            o["hot"] = args.pop(0)
        else:
            sys.exit(f"armcount.py: {a}? (--help)")
    return o


# ---- the M33's times --------------------------------------------------------------------
BRANCH = {"b", "bl", "blx", "bx", "cbz", "cbnz", "tbb", "tbh"}
COND = r"(eq|ne|cs|hs|cc|lo|mi|pl|vs|vc|hi|ls|ge|lt|gt|le|al)$"
MULTI = ("push", "pop", "ldm", "ldmia", "ldmdb", "stm", "stmia", "stmdb",
         "vpush", "vpop", "vldmia", "vstmia", "vldmdb", "vstmdb")


def cost(insn):
    m = insn.mnemonic.split(".")[0]
    base = m if m in BRANCH else re.sub(COND, "", m)
    ops = insn.op_str
    if m.startswith("it"):
        return 0                                # folded into the next
    if base in ("b", "cbz", "cbnz"):
        return 1                                # 2 more when taken, charged at the next block
    if base in ("bl", "blx", "bx"):
        return 3
    if base in ("tbb", "tbh"):
        return 4
    if base in MULTI:
        n = len(re.findall(r"[rsdl][0-9]+|lr|pc|sb|sl|fp|ip", ops[ops.find("{"):]))
        return 1 + n + (2 if "pc" in ops and base in ("pop", "ldm", "ldmia") else 0)
    if base == "ldrd":
        return 3
    if base == "strd":
        return 2
    if base.startswith("ldr"):
        return 2 + (2 if "pc" in ops.split(",")[0] else 0)
    if base.startswith("str"):
        return 1
    if base in ("sdiv", "udiv"):
        return 6
    if base in ("vdiv", "vsqrt"):
        return 14
    if base in ("vmla", "vmls", "vnmla", "vnmls", "vfma", "vfms", "vfnma", "vfnms"):
        return 3
    if base in ("vldr", "vstr"):
        return 2
    if base == "vmov" and ops.count(",") == 2:
        return 2                                # to or from two core registers
    return 1


def main():
    o = options(sys.argv)
    uc = Uc(UC_ARCH_ARM, UC_MODE_THUMB)
    uc.ctl_set_cpu_model(UC_CPU_ARM_MAX)        # ARMv8's FP (vsel, vminnm): the M33 has it, Unicorn's default doesn't
    uc.mem_map(0x20000000, 0x80000)
    sym, funcs = {}, []
    with open(o["elf"], "rb") as f:
        elf = ELFFile(f)
        for seg in elf.iter_segments():
            if seg["p_type"] == "PT_LOAD" and seg["p_filesz"]:
                uc.mem_write(seg["p_paddr"], seg.data())
        for s in elf.get_section_by_name(".symtab").iter_symbols():
            sym[s.name] = s["st_value"]
            if s["st_info"]["type"] == "STT_FUNC":
                funcs.append((s["st_value"] & ~1, s["st_size"], s.name))
        sec = elf.get_section_by_name(".text")
        code_base, code = sec["sh_addr"], sec.data()
    for need in ("_start", "bench_mark", "__bss_start", "__bss_end"):
        if need not in sym:
            sys.exit(f"armcount.py: {o['elf']} has no {need}: build it with tools/armcount/build.sh from a bench like bench.c")
    uc.mem_write(sym["__bss_start"], bytes(sym["__bss_end"] - sym["__bss_start"]))
    for name, path in o["load"]:
        if name not in sym:
            sys.exit(f"armcount.py: no global {name} in {o['elf']}")
        uc.mem_write(sym[name], open(path, "rb").read())
    uc.reg_write(UC_ARM_REG_C1_C0_2, uc.reg_read(UC_ARM_REG_C1_C0_2) | (0xF << 20))   # the FPU on
    uc.reg_write(UC_ARM_REG_FPEXC, 0x40000000)

    cs = Cs(CS_ARCH_ARM, CS_MODE_THUMB | CS_MODE_MCLASS)
    blocks = {}

    def block_cost(addr, size):
        c = blocks.get((addr, size))
        if c is None:
            off = addr - code_base
            insns = list(cs.disasm(code[off:off + size], addr))
            last = insns[-1].mnemonic.split(".")[0] if insns else ""
            cond = last in ("b", "cbz", "cbnz") or bool(re.match(r"b" + COND, last) and last != "bl")
            c = blocks[(addr, size)] = (sum(cost(i) for i in insns), addr + size, cond)
        return c

    funcs.sort()
    fstart = [f[0] for f in funcs]

    def func_of(addr):
        i = bisect.bisect_right(fstart, addr) - 1
        return funcs[i][2] if i >= 0 else "?"

    per_func, per_block = {}, {}
    state = {"cycles": 0, "fall": None, "cond": False}

    def on_block(uc, addr, size, _):
        t, end, cond = block_cost(addr, size)
        if state["cond"] and addr != state["fall"]:
            t += 2                              # the branch before was taken
        state["cycles"] += t
        state["fall"], state["cond"] = end, cond
        if o["profile"] or o["hot"]:
            fn = func_of(addr)
            per_func[fn] = per_func.get(fn, 0) + t
            if fn == o["hot"]:
                per_block[(addr, size)] = per_block.get((addr, size), 0) + t

    mark = sym["bench_mark"] & ~1
    pc = sym["_start"] | 1
    hook = None
    res = []
    for n in range(1, o["lists"] + 1):
        counted = n >= o["from"]
        if counted and hook is None:
            hook = uc.hook_add(UC_HOOK_BLOCK, on_block)
            uc.ctl_flush_tb()                   # (code translated before the hook wouldn't call it)
        state.update(cycles=0, fall=None, cond=False)
        uc.emu_start(pc, mark)
        pc = uc.reg_read(UC_ARM_REG_LR) | 1     # where bench_mark() returns to
        if counted:
            res.append((n, state["cycles"]))
            if o["each"]:
                c = state["cycles"]
                print(f"list {n:5d}  {c:9d} cycles  {c / MHZ / 1000:5.2f} ms  {100 * c / LIST_CYCLES:3.0f}%", flush=True)
    if not res:
        sys.exit("armcount.py: no list counted (--from past --lists?)")
    cyc = sorted(c for _, c in res)
    worst_n, worst = max(res, key=lambda r: r[1])
    # late: the lists so far over their time plus one list's (the SDK holds one more)
    late = 0
    owed = 0
    for _, c in res:
        owed = max(0, owed + c - LIST_CYCLES)
        if owed > LIST_CYCLES:
            late += 1
            owed -= LIST_CYCLES
    ms = lambda c: c / MHZ / 1000
    print(f"{len(res)} lists: average {ms(sum(cyc) / len(cyc)):.2f} ms ({100 * sum(cyc) / len(cyc) / LIST_CYCLES:.0f}%), "
          f"99th percentile {ms(cyc[min(len(cyc) - 1, len(cyc) * 99 // 100)]):.2f} ms, worst {ms(worst):.2f} ms "
          f"(list {worst_n}), {late} late; a list has 16.77 ms")
    if o["profile"]:
        tot = sum(per_func.values()) or 1
        for fn, c in sorted(per_func.items(), key=lambda x: -x[1])[:25]:
            print(f"{fn:32} {100 * c / tot:5.1f}%  {ms(c / len(res)):6.2f} ms a list")
    if o["hot"]:
        for (a, size), c in sorted(per_block.items(), key=lambda x: -x[1])[:6]:
            print(f"--- {a:08x}  {ms(c / len(res)):.2f} ms a list")
            off = a - code_base
            for i in cs.disasm(code[off:off + size], a):
                print(f"    {i.address:08x} {i.mnemonic:8} {i.op_str}")


if __name__ == "__main__":
    main()
