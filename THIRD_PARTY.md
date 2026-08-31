# Third-party components

srava builds on the following external components. Their licenses apply to those
components; see each project for the authoritative terms.

| Component | Role | License |
|---|---|---|
| [CGAL](https://www.cgal.org/) | Geometry kernel (EPECK, corefinement Boolean ops) — linked only in the geometry *agent* | **GPLv3** (Boolean/mesh-processing packages; some foundational packages are LGPLv3). See <https://www.cgal.org/license.html> |
| [GMP](https://gmplib.org/) | Exact arithmetic (via CGAL) | LGPLv3 / GPLv2 (dual) |
| [MPFR](https://www.mpfr.org/) | Exact floating point (via CGAL) | LGPLv3+ |
| [Boost](https://www.boost.org/) | Utilities (via CGAL) | Boost Software License 1.0 (permissive) |
| [HDF5](https://www.hdfgroup.org/) *(optional)* | `export_vox` voxel output for k-Wave | BSD-style |
| [Manifold](https://github.com/elalish/manifold) *(optional)* | Fast geometry kernel — linked into `manifold.so` (FetchContent, pinned to v3.5.2, statically linked) | Apache-2.0 |
| [geogram](https://github.com/BrunoLevy/geogram) *(optional, off by default)* | Exact mesh-arrangement geometry kernel — linked into `geogram.so` (FetchContent, pinned to v1.10.0, statically linked). Enabled with `-DSRAVA_MODULE_GEOGRAM=ON` | BSD-3-Clause |
| [OpenVDB](https://github.com/AcademySoftwareFoundation/openvdb) *(optional, off by default)* | Sparse volume / level-set geometry kernel — linked into `openvdb.so` (FetchContent, pinned to v12.1.1, statically linked). Enabled with `-DSRAVA_MODULE_OPENVDB=ON` | Apache-2.0 |
| [oneTBB](https://github.com/uxlfoundation/oneTBB) *(optional, off by default)* | Task scheduler **required** by both OpenVDB and OCCT — detected from the system with `find_package(TBB CONFIG)` (Debian `libtbb-dev`) | Apache-2.0 |
| [Open CASCADE Technology](https://github.com/Open-Cascade-SAS/OCCT) *(optional, off by default)* | B-rep geometry kernel with analytic/NURBS surfaces — detected from the system with `find_package` (Debian `libocct-*`, 7.8.x). Enabled with `-DSRAVA_MODULE_OCCT=ON` | LGPL-2.1 **with the Open CASCADE exception** |
| [Interactive and Robust Mesh Booleans](https://github.com/gcherchi/InteractiveAndRobustMeshBooleans) *(optional, off by default)* | Exact mesh-arrangement Boolean kernel using **indirect predicates** — header-only, compiled into `cherchi.so` (FetchContent, pinned to commit `7bd6c26`). Enabled with `-DSRAVA_MODULE_CHERCHI=ON`. Pulls in its submodules: [arrangements](https://github.com/gcherchi/FastAndRobustMeshArrangements) (MIT), [Cinolib](https://github.com/mlivesu/cinolib) (MIT, which itself bundles Eigen — MPL-2.0), [Indirect_Predicates](https://github.com/MarcoAttene/Indirect_Predicates) (**LGPL-2.1**), abseil-cpp / parallel-hashmap (Apache-2.0). Its bundled oneTBB copy is **not** built — TBB comes from the system, like OpenVDB's and OCCT's. | MIT |
| [Clipper2](https://github.com/AngusJohnson/Clipper2) *(optional)* | 2D polygon ops (pulled in by Manifold) | Boost Software License 1.0 |
| [pipeProximity](modules/pipe_proximity/vendor/pipeProximity/) | Variable-thickness pipe proximity / distance adjustment — **vendored source**, managed inside this repo (GLOBALBASE UMUT) | MIT |
| [tinyState](https://github.com/globalbase-org/tinyState) | Thread + coroutine runtime (GLOBALBASE Project) | **BSD 3-Clause** |

## Why srava is GPLv3

CGAL's Boolean / polygon-mesh-processing packages (used for `|||` `&&&` `---` via
corefinement) are licensed under the GPLv3. srava links them (in the `srava` geometry
agent), so the distributed combination is a GPLv3 work.

The srava **core** (language, planner, content-addressed cache, scheduler — everything
outside the geometry agent) does not depend on CGAL. Once the geometry kernel is fully
delivered as a swappable plugin agent, the core can be relicensed under a permissive
license while the CGAL-linked agent remains GPLv3. See [README.md](README.md#license).

Vendored export formats (OFF/STL/PLY/OBJ, and AMF/3MF via a self-contained minimal
XML+ZIP writer) have **no external dependencies**.

> **Why oneTBB comes from the system while OpenVDB itself is fetched**: TBB is a *runtime with
> scheduler state*, so there must be exactly **one instance per process**. Linking it statically
> into more than one `.so` would put two thread pools in one process — the mirror image of the
> symbol-collision problem already hit with `nef_snc.so` / `nef_hybrid.so` (solved there with
> hidden visibility, which is exactly what would give each `.so` its own TBB copy here).
> It was originally fetched and built as a shared library, but **OCCT also requires TBB** and OCCT
> is taken from the system (intake method C), so a fetched copy would have coexisted with the
> system one — the very thing the rule forbids. TBB is therefore taken from the system too:
> a distro ships it, and "exactly one of these should exist" is precisely what method C is for.
> A future Manifold `PAR=ON` build (#3419) must use the same one.
>
> **OpenVDB dependencies srava does not take**: Boost is avoided by building with
> `OPENVDB_USE_DELAYED_LOADING=OFF` (only delayed grid loading needs `Boost::iostreams`), and
> Blosc is left off (`USE_BLOSC=OFF`) — `.vdb` files still get active-mask compression,
> half-float quantisation and zip via the system zlib. NanoVDB and AX are not built.

> **Why OCCT is safe to link from a GPLv3 program**: LGPL-2.1 §3 states in its own text that a
> recipient "may opt to apply the terms of the ordinary GNU General Public License … version 2 …
> (If a newer version than version 2 … has appeared, then you can specify that version instead)".
> GPLv3 is such a newer version, so the LGPL-2.1 library may be taken under GPLv3 and combined
> with srava. This is the same footing as GMP/MPFR (LGPLv3+), already linked via CGAL. The
> **Open CASCADE exception** attached to it is an *additional permission* (it allows object code
> that inlined OCCT headers to be distributed under terms of your choice); it adds no restriction.
> The Debian package (`7.8.1+dfsg1`) was checked file-by-file: besides LGPL-2.1 it carries only
> Expat, Unicode, BSD-2-clause and one Bison-generated file under GPL-3+-with-Bison-exception —
> **no AGPL and no non-commercial clause**. `dfsg1` strips Windows batch files and iOS/MFC samples,
> not licence-encumbered code.

> **geogram's own bundled third-party code**: geogram ships **TetGen (AGPLv3)** and **Triangle
> (non-commercial licence)**, both enabled by default in its build. srava turns them **off**
> (`GEOGRAM_WITH_TETGEN=OFF` / `GEOGRAM_WITH_TRIANGLE=OFF`) — neither is needed for mesh CSG, and
> pulling AGPL code in would change the terms under which srava may be distributed. Only the
> BSD-3 core is linked.

## How external dependencies are taken in

Every module picks exactly one of three intake methods — vendoring, FetchContent + static link, or
`find_package` against the system. The rule for choosing, and the obligations attached to each
(a `VENDORED.md` for vendored trees, `EXCLUDE_FROM_ALL` for FetchContent, module-scoped
`find_package` so the module can be switched off on machines without the dependency), is documented
in **[docs/srava_module_design.md §7.3](docs/srava_module_design.md)**. New geometry kernels must
follow it and add their license here.
