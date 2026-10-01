#pragma once
// Defect B fixture 3 of 4: an include that cannot be resolved, and no annotation anywhere.
// Expected after the fix: non-zero exit + the include named. Measured before the fix: exit 0.
#include "NoSuchHeader_xyz.h"
struct Fine { int a; };
