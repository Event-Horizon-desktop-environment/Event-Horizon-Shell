# TinySpline — NURBS, B-Spline, Bezier Library

ANSI C library with C++ wrapper for creating, evaluating, and transforming
NURBS, B-Splines, and Bezier curves.

**Upstream:** https://github.com/msteinbeck/tinyspline  
**License:** MIT  
**Files:** `tinyspline.h`, `tinyspline.c`, `tinysplinecxx.h`, `tinysplinecxx.cxx`, `parson.h`, `parson.c`, `LICENSE`

## Usage in Horizon Photo

Used from C++ via `tinysplinecxx.h` for Catmull-Rom → Bezier decomposition
(freehand pen smoothing) and general spline fitting:

```cpp
#include "third_party/tinyspline/tinysplinecxx.h"

auto spline = tinyspline::BSpline::interpolateCatmullRom(2, pts);
auto beziers = spline.toBeziers();
// beziers.ctrlp() contains groups of 4 control points for cairo_curve_to
```
