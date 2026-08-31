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

## Style rules worth knowing

- **User-visible strings are written in English** — error messages, warnings and diagnostics that a
  user of `srava` can see. Source comments stay in Japanese; this rule is only about the text that
  leaves the program. (Established 2026-08-26; the codebase was already almost entirely English.)
- **Modules must not keep mutable file-scope statics.** With in-process execution several ops of the
  same module can live in one process, so module-global state gets mixed up between them. Keep
  per-module state in the registry slot instead (`set_module_data` / `module_data`), and write failure
  reasons into a buffer supplied by the caller. See `docs/srava_module_design.md` §5.3 / §5.4.
- **Never fall back silently.** If something cannot be resolved (an unknown extension, a format that
  no codec can write, a type that cannot be converted), raise an error rather than quietly picking
  something else. Silent fallbacks have twice produced results that looked right and were not.

## Proposing changes

1. Open an issue describing the change (bug, feature, or kernel/module addition).
2. Keep commits focused; include tests under `test/` for new language or runtime behavior.
3. Ensure `ctest` passes and no new compiler warnings are introduced.
4. By contributing, you agree your contributions are licensed under the project license
   (**GPL-3.0**; see [LICENSE](LICENSE)).

## Reporting bugs

Include a minimal reproducing `.sra` script, the exact command, expected vs actual output,
and your platform / compiler / CGAL version.
