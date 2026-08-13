# Contributing to srava

Thanks for your interest. srava is developed by the GLOBALBASE Project.

## Building and testing

```sh
cmake -B build -DCMAKE_BUILD_TYPE=Release
cmake --build build -j
ctest --test-dir build            # the test suite must stay green
```

- Language standard: **C++20** (`-std=gnu++2a`).
- Dependencies: CGAL (+ GMP/MPFR/Boost), tinyState, POSIX threads; HDF5 is optional
  (enables `export_vox`). See [THIRD_PARTY.md](THIRD_PARTY.md).
- Platforms: Linux is the primary target; Windows (MSYS2/MinGW-w64 and Cygwin) and macOS
  are supported. Keep changes portable and run `ctest` on your platform before submitting.

## Architecture at a glance

- **Core** (CGAL-independent): the language (parser + `pigData`/`ptsObject`), the planner
  (`cgptsPlanner`), the content-addressed streaming cache, and the scheduler.
- **Agents**: heavy work runs in process-separated worker agents. The **geometry** agent is
  the *only* component that links CGAL — keep it that way. New kernels/domains should be
  added as agents, not wired into the core.
- **Modules**: third parties can add operations as external modules (`.so`) without rebuilding
  the core (see `docs/srava_module_reference.md`). `pipe_proximity` is the first example.

Preserving the **core ↔ kernel boundary** is the most important review criterion: the core
must not gain a compile-time dependency on any geometry kernel.

## Proposing changes

1. Open an issue describing the change (bug, feature, or kernel/module addition).
2. Keep commits focused; include tests under `test/` for new language or runtime behavior.
3. Ensure `ctest` passes and no new compiler warnings are introduced.
4. By contributing, you agree your contributions are licensed under the project license
   (**GPL-3.0**; see [LICENSE](LICENSE)).

## Reporting bugs

Include a minimal reproducing `.sra` script, the exact command, expected vs actual output,
and your platform / compiler / CGAL version.
