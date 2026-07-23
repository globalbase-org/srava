# srava

**srava** — *the flow.* A kernel-agnostic dataflow engine with a small, Lisp-flavored
language for building solid geometry, backed by a content-addressed streaming runtime.

> Sanskrit **स्रव** *srava* (√*sru*, “to flow”). Data flows in; results are produced and
> reused. The geometry kernel is just one plugin in the flow — today CGAL, tomorrow
> anything.

> **Status:** first public release in preparation. The source tree is being renamed from
> its internal working name `cgalp` to `srava` (source extension `.cgalp` → `.sra`); some
> build artifacts and paths may still show the old name during this transition.

---

## What it is

srava is two things layered together:

1. **A language** — a compact scripting language (S-expression roots, JavaScript/Perl-ish
   surface) for describing 2D/3D solids: variables, arrays/hashes, lambdas, `map`, mesh
   boolean operators (`|||` union, `&&&` intersection, `---` difference), a small standard
   library (`std/curve`, `std/math`, …), and `async`/`sync` blocks for parallel sweeps.

2. **A runtime** — a planner + scheduler that normalizes every expression into a
   **content-addressed cache key**, runs heavy work in **process-separated worker agents**,
   and streams cached results (single-writer / multi-reader: readers can consume a cache
   entry *while* it is still being written). Re-running a program, or running a parametric
   sweep, reuses shared sub-results instead of recomputing them.

The distinguishing idea is a **kernel-agnostic orchestration layer**: the geometry kernel
(currently [CGAL](https://www.cgal.org/), EPECK corefinement) is confined to a single
*agent* process. The language, planner, cache and scheduler know nothing about CGAL, so the
kernel is swappable — the same program can, in principle, run over a different geometry
backend, or over an entirely different domain (image/volume/video pipelines) exposed as a
plugin agent.

## Quick example

```
# hello.sra
var body = box(40, 40, 40);
var tool = sphere(26);
export("hello.off", body --- tool);    # difference → OFF mesh
```

```sh
srava hello.sra          # produces hello.off; re-runs hit the cache and are instant
```

Because every sub-expression is content-addressed, a parametric sweep — e.g. `map` over a
`linspace`, or an `async { … }` block fanning out variants in parallel — recomputes only
what actually changed and **reuses shared sub-results across runs**. Generating many
related models (the common case for simulation datasets) stays close to the cost of the
*new* geometry, not the total.

## Building

Requirements:

- A C++20 compiler (`-std=gnu++2a`), CMake ≥ 3.16
- [CGAL](https://www.cgal.org/) (pulls in GMP / MPFR / Boost) — geometry kernel
- [tinyState](https://github.com/globalbase-org/tinyState) — thread + coroutine runtime (GLOBALBASE Project)
- POSIX threads
- *(optional)* HDF5 — enables `export_vox` (voxelization → k-Wave acoustic simulation)

```sh
cmake -B build -DCMAKE_BUILD_TYPE=Release
cmake --build build -j
ctest --test-dir build
sudo cmake --install build          # installs srava, agents, stdlib, plugins
```

The standard library and plugins are installed under `share/srava/`, so `include "std/…"`
resolves with no environment variables.

## Documentation

- `docs/srava_language_reference.md` — language reference
- `docs/srava_function_reference.md` — built-in / stdlib functions
- `docs/srava_plugin_reference.md` — writing external plugin agents
- `docs/srava_async_design.md` — the async/sync concurrency model
- `docs/srava_kwave.md` — geometry → voxels → k-Wave acoustic simulation

## License

**GPL-3.0** — see [LICENSE](LICENSE).

srava links CGAL (whose Boolean/corefinement packages are GPLv3), so the combined work is
distributed under the GPLv3.

**Licensing roadmap.** The srava *core* (language, planner, cache, scheduler) is already
independent of CGAL — the kernel lives only in a separate agent process. As the geometry
kernel is fully externalized as a swappable plugin, we intend to relicense the core under a
permissive license (MIT / Apache-2.0), keeping only the CGAL-linked agent under the GPL.
See [THIRD_PARTY.md](THIRD_PARTY.md) for component licenses.

## Acknowledgements

Built on [CGAL](https://www.cgal.org/), [Boost](https://www.boost.org/), GMP/MPFR, and the
[tinyState](https://github.com/globalbase-org/tinyState) runtime.

---

© 2026 GLOBALBASE Project, Hirohisa Mori
