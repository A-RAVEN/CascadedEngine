#pragma once
// Defect B fixture 2 of 4: a hard syntax error, and no annotation anywhere in the file.
// Expected after the fix: non-zero exit + the diagnostic. Measured before the fix: exit 0.
struct Broken { int a }
