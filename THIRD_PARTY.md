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
