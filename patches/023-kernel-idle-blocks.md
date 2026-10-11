# 023-kernel-idle-blocks

**Why:** `Thread_Kernel` (kernel/system.c) ends by dropping to priority 0 and spinning in `while (1) { }`:
the N64's idle loop, which only runs when every other thread waits. KOS v2.2.2's `thd_schedule` searches the
run queue *before* re-queuing the running thread on a timer tick, so the running thread hands the CPU to the
first other ready thread whatever its priority. The always-ready spin therefore got the CPU at every tick
(1 kHz here, os_shim.c): in Flycast ~44% of each display-list window ran the spin (the interpreter thread was
parked mid-work), and the game logic lost time the same way. The first hardware runs (15-25 fps, CPU-bound in
the interpreter) paid for it too. Under `WINGS64_DC` the thread blocks forever on a private message queue.

**Origin:** own (frame-rate work after hardware run 1, docs/fps-plan.md).

**N64 build:** unchanged (`#ifdef WINGS64_DC` / `#else` original).
