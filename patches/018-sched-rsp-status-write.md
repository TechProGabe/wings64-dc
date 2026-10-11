# 018-sched-rsp-status-write

**Why:** `_uvScDlistRecover` (RSP/RDP timeout recovery) writes the N64 SP status register
(`IO_WRITE(SP_STATUS_REG)`, 0xA4040010). On the Dreamcast that address is unmapped. There is no RSP;
the recovery's message posting is kept, the register write is dropped.

**Origin:** own (Birdman64 replaces the line with a trace log).

**N64 build:** unchanged.
