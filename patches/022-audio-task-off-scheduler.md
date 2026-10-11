# 022-audio-task-off-scheduler

**Why:** on the N64 the audio thread (priority 110) submits each frame's audio command list to the
scheduler, which hands it to the RSP and returns at once; the RSP's done interrupt wakes the audio thread
before the next retrace. In the port the scheduler thread interprets display lists on the CPU (~30 ms per
flight frame), so the audio task waited behind gfx work and the audio thread, blocked on its reply,
missed retraces: the emulator gate measured 7% of audio frames lost and 18% virtual-DAC underruns
(music time dropped, ~20.4 kHz effective). With M4 the AICA plays the voices (`src/dc/audio`), so the
audio command list is a placeholder: `__amHandleFrameMsg` posts the task's done message to
`audioReplyMsgQ` itself instead of queuing the task on the scheduler (`DIVERGES`: no audio RSP task).
After it: 300 audio frames per 5 s, 22.0-22.3 kHz, 0 underruns over the gate. Raising the audio thread
above the scheduler (priority 200) was tried and changed nothing measurable; the N64's 110 is kept.

**Origin:** own (M4 audio, docs/audio-design.md section 6).

**N64 build:** unchanged (`#ifdef WINGS64_DC` / `#else` original).
