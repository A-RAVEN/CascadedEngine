#pragma once
#include "CodeGenMeta.h"
// Red case for the marker scan: the ONLY mention of the annotation is inside a comment (the
// commented-out line just above the struct below). The type itself carries no annotation, so no
// specialization may be produced for it.
//
// Measured on the 035a795 binary before the fix: the comment text was read as a real annotation, the
// type was therefore treated as annotated, and --emit-serialization wrote a specialization for it and
// exited 0. After the fix the comment bytes are blanked out before any textual scan, the annotation
// list of this file comes out empty, and the tool reports that and leaves the requested output path
// untouched.
//
// The words in this comment block avoid the exact marker spelling followed by an opening parenthesis,
// so the only place that spelling occurs in this file is the commented-out line below.
namespace audit_ns
{
    // CA_REFLECT(serializable)
    struct NotAnnotated { int a; float b; };
}
