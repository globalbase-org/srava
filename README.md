# srava

**srava** — *the flow.* A kernel-agnostic dataflow engine with a small, Lisp-flavored
language for building solid geometry, backed by a content-addressed streaming runtime.

> Sanskrit **स्रव** *srava* (√*sru*, “to flow”). Data flows in; results are produced and
> reused. The geometry kernel is just one module in the flow — today CGAL, tomorrow
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
module.

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

### Requirements

**Toolchain**

- A C++20 compiler (`-std=gnu++2a`), CMake ≥ 3.16
- POSIX threads

**Build first — not available from a package manager**

- [tinyState](https://github.com/globalbase-org/tinyState) — thread + coroutine runtime
  (GLOBALBASE Project). Located with `find_package(tinyState REQUIRED)`; build and install
  it before configuring srava. For a non-standard prefix, pass `-DCMAKE_PREFIX_PATH=`.

**System libraries** — install these yourself; each one gates the modules listed beside it.
A module whose dependency is missing is simply left out of the build.

| Library | Enables | Notes |
| --- | --- | --- |
| [CGAL](https://www.cgal.org/) | `cgal`, `nef_hybrid`, `nef_snc` | pulls in GMP / MPFR / Boost |
| [Open CASCADE](https://dev.opencascade.org/) | `occt` (B-rep) | |
| [oneTBB](https://github.com/uxlfoundation/oneTBB) | intra-op parallelism | see note below |
| zlib | OpenVDB `.vdb` compression | usually already present |
| fontconfig | OCCT font ops (`text`) | |
| HDF5 *(optional)* | `export_vox` (voxelization → k-Wave acoustic simulation) | |

> **TBB must come from the system.** `SRAVA_MANIFOLD_PAR` defaults to `ON`, and OpenVDB and
> OCCT also require TBB. Do not let a subproject fetch its own copy — two TBB runtimes in one
> process is not a supported configuration.

**Fetched automatically** (`FetchContent`; nothing to install)

- [Manifold](https://github.com/elalish/manifold), [geogram](https://github.com/BrunoLevy/geogram),
  [OpenVDB](https://www.openvdb.org/), [Cherchi](https://github.com/gcherchi/InteractiveAndRobustMeshBooleans)
  — pipeProximity is vendored in-tree.

These are configured to keep their own dependency footprint minimal: OpenVDB is built without
Boost or Blosc, Manifold uses the system TBB rather than a bundled copy, and geogram is built
library-only (no OpenGL / Lua). TBB and zlib above are the only system libraries they add.

Example (macOS / Homebrew):

```sh
brew install cmake cgal opencascade tbb hdf5 fontconfig zlib
```

### Build

```sh
cmake -B build -DCMAKE_BUILD_TYPE=Release
cmake --build build -j
ctest --test-dir build
sudo cmake --install build          # installs srava, srava_agent, stdlib, modules
```

Every geometry module is built by default, including `nef_snc`, which offers the same
operations as `nef_hybrid` using a different internal representation:

```sh
cmake -B build -DCMAKE_BUILD_TYPE=Release
```

Individual modules can be turned off with `-DSRAVA_MODULE_<NAME>=OFF` (`CGAL`, `MANIFOLD`,
`GEOGRAM`, `OPENVDB`, `OCCT`, `CHERCHI`, `NEF`, `PIPEPROX`); with `SRAVA_MODULE_CGAL=OFF`
srava builds without CGAL, GMP or MPFR at all.

The standard library is installed under `share/srava/` and modules (`.so`) under
`lib/srava/modules/`, so `include "std/…"` and bundled ops resolve with no environment variables.

## Documentation

- `docs/srava_language_reference.md` — language reference
- `docs/srava_function_reference.md` — built-in / stdlib functions
- `docs/srava_module_reference.md` — writing external modules (`.so`)
- `docs/srava_async_design.md` — the async/sync concurrency model
- `docs/srava_kwave.md` — geometry → voxels → k-Wave acoustic simulation

## License

**GPL-3.0** — see [LICENSE](LICENSE).

srava links CGAL (whose Boolean/corefinement packages are GPLv3), so the combined work is
distributed under the GPLv3.

**Licensing roadmap.** The srava *core* (language, planner, cache, scheduler) is already
independent of CGAL — the kernel lives only in a separate agent process. As the geometry
kernel is fully externalized as a swappable module, we intend to relicense the core under a
permissive license (MIT / Apache-2.0), keeping only the CGAL-linked agent under the GPL.
See [THIRD_PARTY.md](THIRD_PARTY.md) for component licenses.

## Acknowledgements

Built on [CGAL](https://www.cgal.org/), [Boost](https://www.boost.org/), GMP/MPFR, and the
[tinyState](https://github.com/globalbase-org/tinyState) runtime.

---

© 2026 GLOBALBASE Project, Hirohisa Mori
