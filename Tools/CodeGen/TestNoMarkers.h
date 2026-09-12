//
// TestNoMarkers.h — the "empty whitelist" config (task 3.3 oracle ② / spec "空白名单").
//
// This header deliberately carries NO discovery marker (no CA_REFLECT / CA_CODEGEN_REGISTER /
// CA_REFLECTION). It may still contain plain structs, but "标过了才生成" means none of them are
// eligible: the CodeGen tool computes an EMPTY whitelist and must simply EXIT 0 with no file
// generated (never blocking the build), rather than auto-reflecting every struct it parses.
//
#pragma once

namespace codegen_nomark_test
{
    struct PlainA { int x; };
    struct PlainB { float y; };
} // namespace codegen_nomark_test
