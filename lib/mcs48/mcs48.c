// license:BSD-3-Clause
// copyright-holders:Dan Boris, Mirko Buffoni, Aaron Giles, Couriersud
//
// mcs48.c -- the MCS-48's instructions, a template (mcs48.h). A plugin's
// .c file includes it after defining the CPU's connections. Each may use
// `m`, the mcs48_t * of the CPU running. Only MCS48_ROM is required:
//
//   MCS48_ROM           program memory, a const uint8_t * expression; read
//                       at each <name>_run and after each write below, so a
//                       port can switch its bank
//   MCS48_ROM_MASK      its address mask (default 0xfff: 4K; 0x7ff: 2K
//                       mirrored)
//   MCS48_RAM_MASK      internal RAM (default 0x3f: an 8035's 64 bytes;
//                       0x7f an 8039's, 0xff an 8040's)
//   MCS48_P1_R()  MCS48_P2_R()      the ports' pins (default 0xff); IN A,Pp
//                       reads them ANDed with the latch
//   MCS48_P1_W(d) MCS48_P2_W(d)     the ports' latches, written
//   MCS48_BUS_R() MCS48_BUS_W(d)    INS A,BUS (default 0xff); OUTL/ORL/ANL BUS
//   MCS48_T0()    MCS48_T1()        the test pins, 0 or 1 (default 0)
//   MCS48_EXT_R(a) MCS48_EXT_W(a,d) MOVX A,@Rr and MOVX @Rr,A (default 0)
//   MCS48_PROG_W(s)     the PROG pin, 0 then 1 around an expander
//                       operation (MOVD/ORLD/ANLD: an 8243 on P2's low half)
//   MCS48_NAME(x)       the functions' names (default mcs48_##x)
//   MCS48_API           their storage class (default none: extern; define it
//                       `static` to keep them in the file; an unused one
//                       doesn't warn)
//   MCS48_HOOK(m)       optional: called before an instruction with *m up to
//                       date (pc is the instruction's, the timer current,
//                       icount the cycles left); returns nonzero if it did
//                       the work of instructions itself (a trace returns 0).
//                       It may change *m as those instructions would, and
//                       must burn their cycles with <name>_burn().
//   MCS48_HOOK_AT(pc)   when to call it (default: always), a cheap test on
//                       the next instruction's address
//
// The connections are called with m->icount current, so a callback can
// read the time (mcs48_total), end the slice (mcs48_abort), add wait
// states (mcs48_adjust_icount) or set an interrupt line (mcs48_set_irq).
// The timer's registers are brought up to date only for the hook.
//
// Each instruction burns its cycles first and then acts, as MAME's
// handlers do: a port write is seen at the end of its instruction, and JTF
// and MOV A,T see the instruction's own cycles counted.
//
// Ported from MAME 0.288's mcs48.cpp by the Parrot Cart contributors (Star
// Trek's and Donkey Kong Jr.'s 8035s, merged); MAME's license is BSD-3-Clause.
#include "mcs48.h"

#ifndef MCS48_ROM
#error "mcs48.c: define MCS48_ROM (the program memory) before including it"
#endif
#ifndef MCS48_ROM_MASK
#define MCS48_ROM_MASK 0xfff
#endif
#ifndef MCS48_RAM_MASK
#define MCS48_RAM_MASK 0x3f
#endif
#ifndef MCS48_P1_R
#define MCS48_P1_R() 0xff
#endif
#ifndef MCS48_P2_R
#define MCS48_P2_R() 0xff
#endif
#ifndef MCS48_P1_W
#define MCS48_P1_W(d) ((void)(d))
#endif
#ifndef MCS48_P2_W
#define MCS48_P2_W(d) ((void)(d))
#endif
#ifndef MCS48_BUS_R
#define MCS48_BUS_R() 0xff
#endif
#ifndef MCS48_BUS_W
#define MCS48_BUS_W(d) ((void)(d))
#endif
#ifndef MCS48_T0
#define MCS48_T0() 0
#endif
#ifndef MCS48_T1
#define MCS48_T1() 0
#endif
#ifndef MCS48_EXT_R
#define MCS48_EXT_R(a) ((void)(a), 0)
#endif
#ifndef MCS48_EXT_W
#define MCS48_EXT_W(a, d) ((void)(a), (void)(d))
#endif
#ifndef MCS48_PROG_W
#define MCS48_PROG_W(s) ((void)(s))
#endif
#ifndef MCS48_NAME
#define MCS48_NAME(x) mcs48_##x
#endif
#ifndef MCS48_API
#define MCS48_API
#endif
#define MCS48_FN_ MCS48_API __attribute__((unused))
#if defined(MCS48_HOOK) && !defined(MCS48_HOOK_AT)
#define MCS48_HOOK_AT(pc) 1
#endif

// ---- what every copy shares (once per file) ------------------------------------------------
#ifndef MCS48_SHARED_
#define MCS48_SHARED_

#if defined(PICO_ON_DEVICE) && PICO_ON_DEVICE
#define MCS48_HOT __attribute__((section(".time_critical.mcs48")))
#else
#define MCS48_HOT
#endif
#define MCS48_INLINE static inline __attribute__((always_inline))

// each opcode's cycles (the illegal ones 1): MAME's handlers burn them first
static const uint8_t mcs48_op_cycles[256] = {
    1, 1, 2, 2, 2, 1, 1, 1, 2, 2, 2, 1, 2, 2, 2, 2,
    1, 1, 2, 2, 2, 1, 2, 1, 1, 1, 1, 1, 1, 1, 1, 1,
    1, 1, 1, 2, 2, 1, 2, 1, 1, 1, 1, 1, 1, 1, 1, 1,
    1, 1, 2, 1, 2, 1, 2, 1, 1, 2, 2, 1, 2, 2, 2, 2,
    1, 1, 1, 2, 2, 1, 2, 1, 1, 1, 1, 1, 1, 1, 1, 1,
    1, 1, 2, 2, 2, 1, 2, 1, 1, 1, 1, 1, 1, 1, 1, 1,
    1, 1, 1, 1, 2, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1,
    1, 1, 2, 1, 2, 1, 2, 1, 1, 1, 1, 1, 1, 1, 1, 1,
    2, 2, 1, 2, 2, 1, 2, 1, 2, 2, 2, 1, 2, 2, 2, 2,
    2, 2, 2, 2, 2, 1, 2, 1, 2, 2, 2, 1, 2, 2, 2, 2,
    1, 1, 1, 2, 2, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1,
    2, 2, 2, 2, 2, 1, 2, 1, 2, 2, 2, 2, 2, 2, 2, 2,
    1, 1, 1, 1, 2, 1, 2, 1, 1, 1, 1, 1, 1, 1, 1, 1,
    1, 1, 2, 2, 2, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1,
    1, 1, 1, 2, 2, 1, 2, 1, 2, 2, 2, 2, 2, 2, 2, 2,
    1, 1, 2, 1, 2, 1, 2, 1, 1, 1, 1, 1, 1, 1, 1, 1,
};

// the 8048's expander operations (MOVD, ORLD, ANLD)
enum { MCS48_EXP_READ = 0, MCS48_EXP_WRITE = 1, MCS48_EXP_OR = 2, MCS48_EXP_AND = 3 };

// the next code byte; the program counter's low 11 bits count, bit 11 stays
MCS48_INLINE uint8_t mcs48_fetch_(const uint8_t *rom, unsigned mask, uint16_t *pc) {
    const uint16_t address = *pc;
    *pc = (uint16_t)(((address + 1) & 0x7ff) | (address & 0x800));
    return rom[address & mask];
}

MCS48_INLINE void mcs48_add_(mcs48_t *m, uint8_t dat, unsigned carry) {
    const unsigned temp = (unsigned)m->a + dat + carry;
    const unsigned temp4 = (unsigned)(m->a & 0x0f) + (dat & 0x0f) + carry;
    m->psw = (uint8_t)((m->psw & ~(MCS48_C_FLAG | MCS48_A_FLAG)) | ((temp4 << 2) & MCS48_A_FLAG) | ((temp >> 1) & MCS48_C_FLAG));
    m->a = (uint8_t)temp;
}

// an interrupt to take before the next instruction (check_irqs would act;
// a timer overflow is only kept while its interrupt is on)
MCS48_INLINE bool mcs48_pending_(const mcs48_t *m) {
    return !m->irq_in_progress && ((m->irq_state && m->xirq_enabled) || m->timer_overflow);
}

// the timer/counter's 13 bits (timer, prescaler) as one count, and back
#define MCS48_TD_NEVER  (-0x40000000)
#define MCS48_TD_ALWAYS 0x7fffffff
#endif // MCS48_SHARED_

// ---- this copy's functions -----------------------------------------------------------------
#define MCS48_RAM_(m, a) ((m)->ram[(a) & (MCS48_RAM_MASK)])

MCS48_FN_ int MCS48_NAME(run)(mcs48_t *m, int cycles);
MCS48_FN_ void MCS48_NAME(reset)(mcs48_t *m);
MCS48_FN_ void MCS48_NAME(burn)(mcs48_t *m, int count);

// burn_cycles: the timer or the counter, then the cycles. The timer counts
// one every 32 cycles (the prescaler); the counter a falling edge on T1,
// which it samples once a cycle, with the cycles going by one at a time.
MCS48_FN_ void MCS48_NAME(burn)(mcs48_t *m, int count_) {
    if (m->timecount_enabled) {
        bool timerover_ = false;
        if (m->timecount_enabled & MCS48_TIMER_ENABLED) {
            // (as MAME's prescaler += count; timer += prescaler >> 5, for any count)
            unsigned pos_ = ((unsigned)m->timer << 5 | (m->prescaler & 0x1f)) + (unsigned)count_;
            timerover_ = pos_ >= 0x2000;
            m->timer = (uint8_t)(pos_ >> 5);
            m->prescaler = (uint8_t)(pos_ & 0x1f);
        } else if (m->timecount_enabled & MCS48_COUNTER_ENABLED) {
            for (; count_ > 0; count_--, m->icount--) {
                m->t1_history = (uint8_t)((m->t1_history << 1) | ((MCS48_T1()) & 1));
                if ((m->t1_history & 3) == 2)
                    if (++m->timer == 0) timerover_ = true;
            }
        }
        if (timerover_) {
            m->timer_flag = true;
            // with the interrupt off, an overflow isn't kept
            if (m->tirq_enabled) m->timer_overflow = true;
        }
    }
    m->icount -= count_;
}

MCS48_INLINE void MCS48_NAME(push_pc_psw_)(mcs48_t *m) {
    const uint8_t sp_ = m->psw & 0x07;
    MCS48_RAM_(m, 8 + 2 * sp_) = (uint8_t)m->pc;
    MCS48_RAM_(m, 9 + 2 * sp_) = (uint8_t)(((m->pc >> 8) & 0x0f) | (m->psw & 0xf0));
    m->psw = (uint8_t)((m->psw & 0xf0) | ((sp_ + 1) & 0x07));
}

// an interrupt's entry (check_irqs), between instructions. MAME's hack: if
// the last instruction was a JNI that wasn't taken, it is taken first.
static void MCS48_NAME(check_irqs_)(mcs48_t *m) {
    if (m->irq_in_progress) return;
    if (m->irq_state && m->xirq_enabled) {
        MCS48_NAME(burn)(m, 2);
        m->irq_in_progress = true;
        if (m->irq_polled) {
            const uint8_t *rom_ = MCS48_ROM;
            m->pc = (uint16_t)(((m->prevpc + 1) & 0x7ff) | (m->prevpc & 0x800));
            const uint16_t page_ = m->pc & 0xf00;
            const uint8_t offset_ = mcs48_fetch_(rom_, MCS48_ROM_MASK, &m->pc);
            m->pc = (uint16_t)(page_ | offset_);
        }
        MCS48_NAME(push_pc_psw_)(m);
        m->pc = 0x03;           // (A11 is 0 in an interrupt)
    } else if (m->timer_overflow && m->tirq_enabled) {
        MCS48_NAME(burn)(m, 2);
        m->irq_in_progress = true;
        MCS48_NAME(push_pc_psw_)(m);
        m->pc = 0x07;
        m->timer_overflow = false;
    }
}

MCS48_FN_ void MCS48_NAME(reset)(mcs48_t *m) {
    m->total = 0;               // (device_execute_interface's pre-reset)
    m->pc = 0;
    m->psw = m->psw & (MCS48_C_FLAG | MCS48_A_FLAG);
    m->f1 = false;
    m->a11 = 0;
    m->tirq_enabled = false;
    m->xirq_enabled = false;
    m->timecount_enabled = 0;
    m->timer_flag = false;
    m->irq_in_progress = false;
    m->timer_overflow = false;
    m->irq_polled = false;
    // the bus and ports 1 and 2 to input
    MCS48_BUS_W(0xff);
    m->p1 = 0xff;
    m->p2 = 0xff;
    MCS48_P1_W(m->p1);
    MCS48_P2_W(m->p2);
}

// The slice. pc, icount and the ROM pointer live in registers, and are
// written back to *m where a call can see them. While the timer runs (STRT
// T), `tdead` is the icount at which it overflows (the 13-bit count's 8192
// less where it is), so a burn is a subtract and a compare; m->timer and
// m->prescaler are made from it where they are read (MOV A,T, a change of
// its mode or value, an interrupt's entry, the hook, the end). As a counter
// (STRT CNT) every burn takes the slow way (T1 a cycle at a time); stopped,
// none does. `pend`: an interrupt may be taken before the next instruction.
static MCS48_HOT __attribute__((noinline)) void MCS48_NAME(execute_)(mcs48_t *m) {
    const uint8_t *rom_ = MCS48_ROM;
    uint16_t pc_ = m->pc;
    int ic_ = m->icount;
    int tmode_, tdead_;
    bool pend_ = mcs48_pending_(m);
    uint8_t op_ = 0;
#define T_IN() do {                                                                             \
        tmode_ = m->timecount_enabled;                                                           \
        tdead_ = tmode_ == MCS48_TIMER_ENABLED ? ic_ - (0x2000 - ((m->timer << 5) | (m->prescaler & 0x1f))) \
              : tmode_ ? MCS48_TD_ALWAYS : MCS48_TD_NEVER;                                       \
    } while (0)
#define T_OUT() do {                                                                            \
        if (tmode_ == MCS48_TIMER_ENABLED) {                                                     \
            const int tpos_ = 0x2000 - (ic_ - tdead_);                                            \
            m->timer = (uint8_t)(tpos_ >> 5);                                                   \
            m->prescaler = (uint8_t)(tpos_ & 0x1f);                                             \
        }                                                                                       \
    } while (0)
    T_IN();
#define fetch() mcs48_fetch_(rom_, MCS48_ROM_MASK, &pc_)
#define prog(a) rom_[(a) & (MCS48_ROM_MASK)]
#define R (m->ram + ((m->psw & MCS48_B_FLAG) ? 24 : 0))
#define RAM(a) MCS48_RAM_(m, a)
#define burn_cycles(n) do {                                                                     \
        ic_ -= (n);                                                                              \
        if (__builtin_expect(ic_ <= tdead_, 0)) {                                                 \
            if (tmode_ == MCS48_TIMER_ENABLED) {                                                 \
                tdead_ -= 0x2000;                                                                \
                m->timer_flag = true;                                                           \
                if (m->tirq_enabled) { m->timer_overflow = true; pend_ = mcs48_pending_(m); }    \
            } else {                                                                            \
                m->icount = ic_ + (n);                                                           \
                MCS48_NAME(burn)(m, n);                                                         \
                ic_ = m->icount;                                                                 \
                pend_ = mcs48_pending_(m);                                                       \
            }                                                                                   \
        }                                                                                       \
    } while (0)
    // a call out to the board: the cycles left to *m and back (it can end the
    // slice or add wait states: the timer stays where it was) and the INT
    // line looked at again
#define CALL_IN_() do {                                                                         \
        if (tmode_ == MCS48_TIMER_ENABLED) tdead_ += m->icount - ic_;                              \
        ic_ = m->icount;                                                                         \
        pend_ |= m->irq_state;                                                                   \
    } while (0)
#define READ(x, v) do { m->icount = ic_; v = (x); CALL_IN_(); } while (0)
#define WRITE(x) do { m->icount = ic_; x; CALL_IN_(); rom_ = MCS48_ROM; } while (0)
#define jmp(address) (pc_ = (uint16_t)((address) | (m->irq_in_progress ? 0 : m->a11 ? 0x800 : 0)))
#define jcc(cond) do {                                                                          \
        const uint16_t pch_ = pc_ & 0xf00;                                                       \
        const uint8_t off_ = fetch();                                                           \
        if (cond) pc_ = (uint16_t)(pch_ | off_);                                                 \
    } while (0)
#define push() do {                                                                             \
        const uint8_t sp_ = m->psw & 0x07;                                                      \
        RAM(8 + 2 * sp_) = (uint8_t)pc_;                                                         \
        RAM(9 + 2 * sp_) = (uint8_t)(((pc_ >> 8) & 0x0f) | (m->psw & 0xf0));                     \
        m->psw = (uint8_t)((m->psw & 0xf0) | ((sp_ + 1) & 0x07));                               \
    } while (0)
#define EXPANDER(operation, port) do {                                                          \
        m->p2 = (uint8_t)((m->p2 & 0xf0) | ((operation) << 2) | ((port) & 3));                  \
        WRITE(MCS48_P2_W(m->p2));                                                               \
        WRITE(MCS48_PROG_W(0));                                                                 \
        if ((operation) != MCS48_EXP_READ) {                                                    \
            m->p2 = (uint8_t)((m->p2 & 0xf0) | (m->a & 0x0f));                                  \
            WRITE(MCS48_P2_W(m->p2));                                                           \
        } else {                                                                                \
            m->p2 |= 0x0f;                                                                      \
            WRITE(MCS48_P2_W(m->p2));                                                           \
            uint8_t v_;                                                                         \
            READ(MCS48_P2_R(), v_);                                                             \
            m->a = v_ & 0x0f;                                                                   \
        }                                                                                       \
        WRITE(MCS48_PROG_W(1));                                                                 \
    } while (0)

    // the first check (a JNI at the end of the last slice counts), then
    // the loop, which checks after each instruction
    if (pend_) {
        m->pc = pc_;
        m->icount = ic_;
        T_OUT();
        MCS48_NAME(check_irqs_)(m);
        pc_ = m->pc;
        ic_ = m->icount;
        T_IN();
        pend_ = mcs48_pending_(m);
    }
    m->irq_polled = false;
    for (;;) {
#ifdef MCS48_HOOK
        if (__builtin_expect(!!(MCS48_HOOK_AT(pc_)), 0)) {
            m->pc = m->prevpc = pc_;
            m->icount = ic_;
            T_OUT();
            const int moved = MCS48_HOOK(m);
            pc_ = m->pc;
            ic_ = m->icount;
            T_IN();
            pend_ = mcs48_pending_(m);
            rom_ = MCS48_ROM;
            if (moved) {
                op_ = 0;
                goto next;
            }
        }
#endif
        op_ = fetch();
        burn_cycles(mcs48_op_cycles[op_]);
        {
        uint8_t t_;
        switch (op_) {
        case 0x00: break;                                               // nop
        case 0x02: WRITE(MCS48_BUS_W(m->a)); break;                     // outl bus,a
        case 0x03: mcs48_add_(m, fetch(), 0); break;                    // add a,#n
        case 0x04: case 0x24: case 0x44: case 0x64: case 0x84: case 0xa4: case 0xc4: case 0xe4:
            t_ = fetch(); jmp((uint16_t)(t_ | ((op_ & 0xe0) << 3))); break;  // jmp
        case 0x05: m->xirq_enabled = true; pend_ = mcs48_pending_(m); break;   // en i
        case 0x07: m->a--; break;                                       // dec a
        case 0x08: READ(MCS48_BUS_R(), m->a); break;                    // ins a,bus
        case 0x09: READ(MCS48_P1_R(), t_); m->a = t_ & m->p1; break;      // in a,p1
        case 0x0a: READ(MCS48_P2_R(), t_); m->a = t_ & m->p2; break;      // in a,p2
        case 0x0c: case 0x0d: case 0x0e: case 0x0f: EXPANDER(MCS48_EXP_READ, op_ & 3); break;   // movd a,pp
        case 0x10: case 0x11: RAM(R[op_ & 1])++; break;                  // inc @r
        case 0x12: case 0x32: case 0x52: case 0x72: case 0x92: case 0xb2: case 0xd2: case 0xf2:
            jcc((m->a & (1u << (op_ >> 5))) != 0); break;                // jbN
        case 0x13: mcs48_add_(m, fetch(), m->psw >> 7); break;          // addc a,#n
        case 0x14: case 0x34: case 0x54: case 0x74: case 0x94: case 0xb4: case 0xd4: case 0xf4:
            t_ = fetch(); push(); jmp((uint16_t)(t_ | ((op_ & 0xe0) << 3))); break;  // call
        case 0x15: m->xirq_enabled = false; pend_ = mcs48_pending_(m); break;  // dis i
        case 0x16: jcc(m->timer_flag); m->timer_flag = false; break;    // jtf
        case 0x17: m->a++; break;                                       // inc a
        case 0x18: case 0x19: case 0x1a: case 0x1b: case 0x1c: case 0x1d: case 0x1e: case 0x1f:
            R[op_ & 7]++; break;                                         // inc r
        case 0x20: case 0x21: t_ = m->a; m->a = RAM(R[op_ & 1]); RAM(R[op_ & 1]) = t_; break;  // xch a,@r
        case 0x23: m->a = fetch(); break;                               // mov a,#n
        case 0x25: m->tirq_enabled = true; pend_ = mcs48_pending_(m); break;   // en tcnti
        case 0x26: READ(MCS48_T0(), t_); jcc(t_ == 0); break;             // jnt0
        case 0x27: m->a = 0; break;                                     // clr a
        case 0x28: case 0x29: case 0x2a: case 0x2b: case 0x2c: case 0x2d: case 0x2e: case 0x2f:
            t_ = m->a; m->a = R[op_ & 7]; R[op_ & 7] = t_; break;           // xch a,r
        case 0x30: case 0x31: {                                         // xchd a,@r
            const uint8_t oldram = RAM(R[op_ & 1]);
            RAM(R[op_ & 1]) = (uint8_t)((oldram & 0xf0) | (m->a & 0x0f));
            m->a = (uint8_t)((m->a & 0xf0) | (oldram & 0x0f));
            break;
        }
        case 0x35: m->tirq_enabled = false; m->timer_overflow = false; pend_ = mcs48_pending_(m); break;  // dis tcnti
        case 0x36: READ(MCS48_T0(), t_); jcc(t_ != 0); break;             // jt0
        case 0x37: m->a ^= 0xff; break;                                 // cpl a
        case 0x39: m->p1 = m->a; WRITE(MCS48_P1_W(m->p1)); break;       // outl p1,a
        case 0x3a: m->p2 = m->a; WRITE(MCS48_P2_W(m->p2)); break;       // outl p2,a
        case 0x3c: case 0x3d: case 0x3e: case 0x3f: EXPANDER(MCS48_EXP_WRITE, op_ & 3); break;  // movd pp,a
        case 0x40: case 0x41: m->a |= RAM(R[op_ & 1]); break;            // orl a,@r
        case 0x42: T_OUT(); m->a = m->timer; break;                     // mov a,t
        case 0x43: m->a |= fetch(); break;                              // orl a,#n
        case 0x45:                                                      // strt cnt
            T_OUT();
            if (!(m->timecount_enabled & MCS48_COUNTER_ENABLED)) { READ(MCS48_T1(), t_); m->t1_history = t_; }
            m->timecount_enabled = MCS48_COUNTER_ENABLED;
            T_IN();
            break;
        case 0x46: READ(MCS48_T1(), t_); jcc(t_ == 0); break;             // jnt1
        case 0x47: m->a = (uint8_t)((m->a << 4) | (m->a >> 4)); break;  // swap a
        case 0x48: case 0x49: case 0x4a: case 0x4b: case 0x4c: case 0x4d: case 0x4e: case 0x4f:
            m->a |= R[op_ & 7]; break;                                   // orl a,r
        case 0x50: case 0x51: m->a &= RAM(R[op_ & 1]); break;            // anl a,@r
        case 0x53: m->a &= fetch(); break;                              // anl a,#n
        case 0x55: T_OUT(); m->timecount_enabled = MCS48_TIMER_ENABLED; m->prescaler = 0; T_IN(); break;  // strt t
        case 0x56: READ(MCS48_T1(), t_); jcc(t_ != 0); break;             // jt1
        case 0x57:                                                      // da a
            if ((m->a & 0x0f) > 0x09 || (m->psw & MCS48_A_FLAG)) {
                if (m->a > 0xf9) m->psw |= MCS48_C_FLAG;
                m->a += 0x06;
            }
            if ((m->a & 0xf0) > 0x90 || (m->psw & MCS48_C_FLAG)) {
                m->a += 0x60;
                m->psw |= MCS48_C_FLAG;
            }
            break;
        case 0x58: case 0x59: case 0x5a: case 0x5b: case 0x5c: case 0x5d: case 0x5e: case 0x5f:
            m->a &= R[op_ & 7]; break;                                   // anl a,r
        case 0x60: case 0x61: mcs48_add_(m, RAM(R[op_ & 1]), 0); break;  // add a,@r
        case 0x62: T_OUT(); m->timer = m->a; T_IN(); break;             // mov t,a
        case 0x65: T_OUT(); m->timecount_enabled = 0; T_IN(); break;    // stop tcnt
        case 0x67: {                                                    // rrc a
            const uint8_t newc = (uint8_t)((m->a << 7) & MCS48_C_FLAG);
            m->a = (uint8_t)((m->a >> 1) | (m->psw & MCS48_C_FLAG));
            m->psw = (uint8_t)((m->psw & ~MCS48_C_FLAG) | newc);
            break;
        }
        case 0x68: case 0x69: case 0x6a: case 0x6b: case 0x6c: case 0x6d: case 0x6e: case 0x6f:
            mcs48_add_(m, R[op_ & 7], 0); break;                         // add a,r
        case 0x70: case 0x71: mcs48_add_(m, RAM(R[op_ & 1]), m->psw >> 7); break;  // addc a,@r
        case 0x75: break;                                               // ent0 clk (no T0 clock out)
        case 0x76: jcc(m->f1); break;                                   // jf1
        case 0x77: m->a = (uint8_t)((m->a >> 1) | (m->a << 7)); break;  // rr a
        case 0x78: case 0x79: case 0x7a: case 0x7b: case 0x7c: case 0x7d: case 0x7e: case 0x7f:
            mcs48_add_(m, R[op_ & 7], m->psw >> 7); break;               // addc a,r
        case 0x80: case 0x81: READ(MCS48_EXT_R(R[op_ & 1]), m->a); break;  // movx a,@r
        case 0x83: {                                                    // ret
            const uint8_t sp = (m->psw - 1) & 0x07;
            const uint16_t v = (uint16_t)(RAM(8 + 2 * sp) | RAM(9 + 2 * sp) << 8);
            pc_ = v & (m->irq_in_progress ? 0x7ff : 0xfff);
            m->psw = (uint8_t)((m->psw & 0xf0) | sp);
            break;
        }
        case 0x85: m->psw &= (uint8_t)~MCS48_F_FLAG; break;             // clr f0
        case 0x86:                                                      // jni (MAME's hack: see check_irqs_)
            m->prevpc = (uint16_t)(((pc_ - 1) & 0x7ff) | (pc_ & 0x800));
            jcc(m->irq_state);
            break;
        case 0x88: READ(MCS48_BUS_R(), t_); t_ |= fetch(); WRITE(MCS48_BUS_W(t_)); break;  // orl bus,#n
        case 0x89: m->p1 |= fetch(); WRITE(MCS48_P1_W(m->p1)); break;   // orl p1,#n
        case 0x8a: m->p2 |= fetch(); WRITE(MCS48_P2_W(m->p2)); break;   // orl p2,#n
        case 0x8c: case 0x8d: case 0x8e: case 0x8f: EXPANDER(MCS48_EXP_OR, op_ & 3); break;     // orld pp,a
        case 0x90: case 0x91: WRITE(MCS48_EXT_W(R[op_ & 1], m->a)); break;  // movx @r,a
        case 0x93: {                                                    // retr
            m->irq_in_progress = false;
            const uint8_t sp = (m->psw - 1) & 0x07;
            const uint16_t v = (uint16_t)(RAM(8 + 2 * sp) | RAM(9 + 2 * sp) << 8);
            m->psw = (uint8_t)(((v >> 8) & 0xf0) | sp);
            pc_ = v & 0xfff;
            pend_ = mcs48_pending_(m);
            break;
        }
        case 0x95: m->psw ^= MCS48_F_FLAG; break;                       // cpl f0
        case 0x96: jcc(m->a != 0); break;                               // jnz
        case 0x97: m->psw &= (uint8_t)~MCS48_C_FLAG; break;             // clr c
        case 0x98: READ(MCS48_BUS_R(), t_); t_ &= fetch(); WRITE(MCS48_BUS_W(t_)); break;  // anl bus,#n
        case 0x99: m->p1 &= fetch(); WRITE(MCS48_P1_W(m->p1)); break;   // anl p1,#n
        case 0x9a: m->p2 &= fetch(); WRITE(MCS48_P2_W(m->p2)); break;   // anl p2,#n
        case 0x9c: case 0x9d: case 0x9e: case 0x9f: EXPANDER(MCS48_EXP_AND, op_ & 3); break;    // anld pp,a
        case 0xa0: case 0xa1: RAM(R[op_ & 1]) = m->a; break;             // mov @r,a
        case 0xa3: m->a = prog((pc_ & 0xf00) | m->a); break;             // movp a,@a
        case 0xa5: m->f1 = false; break;                                // clr f1
        case 0xa7: m->psw ^= MCS48_C_FLAG; break;                       // cpl c
        case 0xa8: case 0xa9: case 0xaa: case 0xab: case 0xac: case 0xad: case 0xae: case 0xaf:
            R[op_ & 7] = m->a; break;                                    // mov r,a
        case 0xb0: case 0xb1: t_ = fetch(); RAM(R[op_ & 1]) = t_; break;   // mov @r,#n
        case 0xb3: pc_ &= 0xf00; pc_ |= prog(pc_ | m->a); break;           // jmpp @a
        case 0xb5: m->f1 = !m->f1; break;                               // cpl f1
        case 0xb6: jcc((m->psw & MCS48_F_FLAG) != 0); break;            // jf0
        case 0xb8: case 0xb9: case 0xba: case 0xbb: case 0xbc: case 0xbd: case 0xbe: case 0xbf:
            R[op_ & 7] = fetch(); break;                                 // mov r,#n
        case 0xc5: m->psw &= (uint8_t)~MCS48_B_FLAG; break;             // sel rb0
        case 0xc6: jcc(m->a == 0); break;                               // jz
        case 0xc7: m->a = m->psw | 0x08; break;                         // mov a,psw
        case 0xc8: case 0xc9: case 0xca: case 0xcb: case 0xcc: case 0xcd: case 0xce: case 0xcf:
            R[op_ & 7]--; break;                                         // dec r
        case 0xd0: case 0xd1: m->a ^= RAM(R[op_ & 1]); break;            // xrl a,@r
        case 0xd3: m->a ^= fetch(); break;                              // xrl a,#n
        case 0xd5: m->psw |= MCS48_B_FLAG; break;                       // sel rb1
        case 0xd7: m->psw = m->a & (uint8_t)~0x08; break;               // mov psw,a
        case 0xd8: case 0xd9: case 0xda: case 0xdb: case 0xdc: case 0xdd: case 0xde: case 0xdf:
            m->a ^= R[op_ & 7]; break;                                   // xrl a,r
        case 0xe3: m->a = prog(0x300 | m->a); break;                    // movp3 a,@a
        case 0xe5: m->a11 = 0; break;                                   // sel mb0
        case 0xe6: jcc((m->psw & MCS48_C_FLAG) == 0); break;            // jnc
        case 0xe7: m->a = (uint8_t)((m->a << 1) | (m->a >> 7)); break;  // rl a
        case 0xe8: case 0xe9: case 0xea: case 0xeb: case 0xec: case 0xed: case 0xee: case 0xef:
            jcc(--R[op_ & 7] != 0); break;                               // djnz r
        case 0xf0: case 0xf1: m->a = RAM(R[op_ & 1]); break;             // mov a,@r
        case 0xf5: m->a11 = 1; break;                                   // sel mb1
        case 0xf6: jcc((m->psw & MCS48_C_FLAG) != 0); break;            // jc
        case 0xf7: {                                                    // rlc a
            const uint8_t newc = m->a & MCS48_C_FLAG;
            m->a = (uint8_t)((m->a << 1) | (m->psw >> 7));
            m->psw = (uint8_t)((m->psw & ~MCS48_C_FLAG) | newc);
            break;
        }
        case 0xf8: case 0xf9: case 0xfa: case 0xfb: case 0xfc: case 0xfd: case 0xfe: case 0xff:
            m->a = R[op_ & 7]; break;                                    // mov a,r
        default: break;                                                 // illegal: 1 cycle
        }
        }
#ifdef MCS48_HOOK
    next:
#endif
        if (ic_ <= 0) break;
        if (__builtin_expect(pend_, 0)) {
            m->pc = pc_;
            m->icount = ic_;
            m->irq_polled = false;
            T_OUT();
            MCS48_NAME(check_irqs_)(m);
            pc_ = m->pc;
            ic_ = m->icount;
            T_IN();
            pend_ = mcs48_pending_(m);
        }
    }
    T_OUT();
    m->pc = pc_;
    m->icount = ic_;
    // a JNI not taken at the slice's end is taken if the next slice starts
    // with the interrupt (the line can only change between slices)
    m->irq_polled = op_ == 0x86 && !m->irq_state;
#undef T_IN
#undef T_OUT
#undef fetch
#undef prog
#undef R
#undef RAM
#undef burn_cycles
#undef CALL_IN_
#undef READ
#undef WRITE
#undef jmp
#undef jcc
#undef push
#undef EXPANDER
}

// execute_run, for a timeslice of `cycles`: the cycles it ran
MCS48_FN_ int MCS48_NAME(run)(mcs48_t *m, int cycles) {
    if (cycles <= 0) return 0;
    m->cycles_running = cycles;
    m->cycles_stolen = 0;
    m->icount = cycles;
    m->executing = true;
    MCS48_NAME(execute_)(m);
    m->executing = false;
    const int ran = cycles - m->icount - m->cycles_stolen;
    m->total += (uint64_t)(int64_t)ran;
    return ran;
}

// the connections, for the next copy
#undef MCS48_RAM_
#undef MCS48_ROM
#undef MCS48_ROM_MASK
#undef MCS48_RAM_MASK
#undef MCS48_P1_R
#undef MCS48_P2_R
#undef MCS48_P1_W
#undef MCS48_P2_W
#undef MCS48_BUS_R
#undef MCS48_BUS_W
#undef MCS48_T0
#undef MCS48_T1
#undef MCS48_EXT_R
#undef MCS48_EXT_W
#undef MCS48_PROG_W
#undef MCS48_NAME
#undef MCS48_API
#undef MCS48_FN_
#undef MCS48_HOOK
#undef MCS48_HOOK_AT
