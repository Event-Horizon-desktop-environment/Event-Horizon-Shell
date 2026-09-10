# psimpl — Polyline Simplification

Header-only C++ polyline simplification library.

**Upstream:** https://github.com/skyrpex/psimpl (mirror) / https://psimpl.sourceforge.net/  
**License:** MPL 1.1  
**Files:** `psimpl.h` (single header)

Provides Douglas-Peucker (RDP), Visvalingam-Whyatt, Lang, Perpendicular-Distance, and Radial-Distance simplification algorithms.

## Usage in Horizon Photo

Used for decimating freehand pen input before bezier fitting:

```cpp
#include "third_party/psimpl/psimpl.h"

std::vector<float> coords; // interleaved x0,y0,x1,y1,...
std::vector<float> simplified;
psimpl::simplify_douglas_peucker<2>(coords.begin(), coords.end(),
    1.5, std::back_inserter(simplified));
```
