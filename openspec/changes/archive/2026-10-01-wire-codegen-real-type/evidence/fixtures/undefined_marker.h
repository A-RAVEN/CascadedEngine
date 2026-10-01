#pragma once
// Defect B fixture 4 of 4: the marker macro is USED but never defined (CodeGenMeta.h not included),
// and there is no other annotation in the file.
// Expected after the fix: non-zero exit + the diagnostic. Measured before the fix: exit 0.
CA_REFLECT(serializable)
struct Nope { int a; };
