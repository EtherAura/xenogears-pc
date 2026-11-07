# engine/: the Enhanced engine's game C

This tree is the game C of the **Enhanced** engine (`xenogears-enhanced`,
`cmake -DXG_ENGINE=enhanced`). It started as a copy of `src/` and `include/`
with every `PLATFORM_PC` conditional resolved to the PC side
(`tools/fork_engine.py`; the generating commit and file hashes are in
`FORK_MANIFEST.json`). From the fork on it is edited freely: none of the
matching rules apply here, there is no PS1 build of it, and nothing in it
has to stay byte-exact. `src/` remains the matching decompilation and the
**Original** engine, which the Enhanced engine never changes.

What does carry over: the clean-room rule (facts from other projects, never
their code), the 64-bit conventions of the port (`uptr`/`sptr`, no `long`,
no libc from game code, `PORT_TODO` where something is still missing), and
the shared platform layer in `port/` (GPU, SPU, CD, input, settings, UI),
which both engines compile unchanged.

The two engines run the same program at the fork point (`tools/fork_engine.py
--verify`, and `tools/engine_diff.py`, which replays the same input on both
and compares frames, audio and game-state invariants). Every change here is
measured against the Original with that tool; what it changes on purpose is
written down in `docs/ENGINE_PLAN.md`.
