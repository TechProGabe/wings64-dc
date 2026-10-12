# 024-debug-printf-to-log

**Why:** `_uvDebugPrintf` (kernel/system.c) is an empty function in the retail game, so every diagnostic the
game has ("_uvSortAdd: sort list full", "uvDobjModel ... model does not exist in level", "uvaSeqStop timed
out", "no free emitters", ...) vanished. The second hardware run hung at a test overview with nothing in the
SD log about why. Under `WINGS64_DC` the function forwards to `wings64_uv_debug` (src/dc/db.c), which logs
each distinct message a few times (rate-limited per format string) through `db_log`; release builds
(`DB_ENABLE=0`) compile it to nothing.

**Origin:** own (hardware hang triage, 2026-10-11).

**N64 build:** unchanged (`#ifdef WINGS64_DC` / `#else` original).
