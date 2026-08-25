#!/usr/bin/env python3
"""read_dump.py — general Windows x64 minidump reader + stack symbolizer.

Usage:
    python Tools/read_dump.py <dump.dmp> [--pdb-dir <dir>]...

What it does:
  - parses the minidump: exception stream (crash code/address/faulting thread),
    module list (bases + names), faulting-thread CONTEXT, memory ranges
  - loads modules into dbghelp (SymLoadModuleEx) with the bases recorded in the
    dump, so ASLR is handled and PDBs resolve against the exact build
  - walks the faulting thread's stack with StackWalk64
  - symbolizes every frame via SymFromAddr + SymGetLineFromAddr64
    (function + source file:line) using PDBs found next to the module paths
    recorded in the dump (--pdb-dir adds extra search dirs)

Dependencies: Windows only, dbghelp.dll (built into Windows). No installs,
no windbg/cdb needed. Verified against GPUBackendTester crash dumps.
"""
import argparse
import ctypes
import ctypes.wintypes as w
import struct
import sys

STREAM_THREAD_LIST = 3
STREAM_MODULE_LIST = 4
STREAM_MEMORY_LIST = 5
STREAM_EXCEPTION = 6
STREAM_MEMORY64_LIST = 9


# ---------------------------------------------------------------------------
# minidump structures (x64, natural MSVC alignment)
# ---------------------------------------------------------------------------
class MiniDumpHeader(ctypes.Structure):
    _fields_ = [
        ("Signature", w.DWORD), ("Version", w.DWORD),
        ("NumberOfStreams", w.DWORD), ("StreamDirectoryRva", w.DWORD),
        ("CheckSum", w.DWORD), ("Reserved", w.DWORD * 4),
    ]


class MiniDumpDirectory(ctypes.Structure):
    _fields_ = [("StreamType", w.DWORD), ("DataSize", w.DWORD), ("Rva", w.DWORD)]


class MiniDumpLocationDescriptor(ctypes.Structure):
    _fields_ = [("DataSize", w.DWORD), ("Rva", w.DWORD)]


class MiniDumpModule(ctypes.Structure):
    _fields_ = [
        ("BaseOfImage", ctypes.c_uint64),
        ("SizeOfImage", w.DWORD),
        ("CheckSum", w.DWORD),
        ("TimeDateStamp", w.DWORD),
        ("ModuleNameRva", w.DWORD),
        ("VersionInfo", w.BYTE * 52),       # VS_FIXEDFILEINFO
        ("CvRecord", MiniDumpLocationDescriptor),
        ("MiscRecord", MiniDumpLocationDescriptor),
        ("Reserved0", ctypes.c_uint64),
        ("Reserved1", ctypes.c_uint64),
    ]


class MiniDumpException(ctypes.Structure):
    _fields_ = [
        ("ExceptionCode", w.DWORD), ("ExceptionFlags", w.DWORD),
        ("ExceptionRecord", ctypes.c_uint64), ("ExceptionAddress", ctypes.c_uint64),
        ("NumberParameters", w.DWORD), ("Alignment", w.DWORD),
        ("ExceptionInformation", ctypes.c_uint64 * 15),
    ]


class MiniDumpExceptionStream(ctypes.Structure):
    _fields_ = [
        ("ThreadId", w.DWORD), ("Alignment", w.DWORD),
        ("ExceptionRecord", MiniDumpException),
        ("ThreadContext", MiniDumpLocationDescriptor),
    ]


class MiniDumpMemoryDescriptor(ctypes.Structure):  # type-5 stream (explicit Rva)
    _fields_ = [
        ("StartOfMemoryRange", ctypes.c_uint64), ("DataSize", w.DWORD), ("Rva", w.DWORD),
    ]


class MiniDumpMemoryDescriptor64(ctypes.Structure):  # type-9 stream (contiguous)
    _fields_ = [("StartOfMemoryRange", ctypes.c_uint64), ("DataSize", ctypes.c_uint64)]


# AMD64 CONTEXT (size must be 1232). Register order per winnt.h.
class CONTEXT(ctypes.Structure):
    _fields_ = [
        ("P1Home", ctypes.c_uint64), ("P2Home", ctypes.c_uint64),
        ("P3Home", ctypes.c_uint64), ("P4Home", ctypes.c_uint64),
        ("P5Home", ctypes.c_uint64), ("P6Home", ctypes.c_uint64),
        ("ContextFlags", w.DWORD), ("MxCsr", w.DWORD),
        ("SegCs", w.WORD), ("SegDs", w.WORD), ("SegEs", w.WORD),
        ("SegFs", w.WORD), ("SegGs", w.WORD), ("SegSs", w.WORD),
        ("EFlags", w.DWORD),
        ("Dr0", ctypes.c_uint64), ("Dr1", ctypes.c_uint64),
        ("Dr2", ctypes.c_uint64), ("Dr3", ctypes.c_uint64),
        ("Dr6", ctypes.c_uint64), ("Dr7", ctypes.c_uint64),
        ("Rax", ctypes.c_uint64), ("Rcx", ctypes.c_uint64),
        ("Rdx", ctypes.c_uint64), ("Rbx", ctypes.c_uint64),
        ("Rsp", ctypes.c_uint64), ("Rbp", ctypes.c_uint64),
        ("Rsi", ctypes.c_uint64), ("Rdi", ctypes.c_uint64),
        ("R8", ctypes.c_uint64), ("R9", ctypes.c_uint64),
        ("R10", ctypes.c_uint64), ("R11", ctypes.c_uint64),
        ("R12", ctypes.c_uint64), ("R13", ctypes.c_uint64),
        ("R14", ctypes.c_uint64), ("R15", ctypes.c_uint64),
        ("Rip", ctypes.c_uint64),
        ("Xmm", w.BYTE * 256),
        ("Rest", w.BYTE * (1232 - 8 * 6 - 4 * 2 - 6 * 2 - 4 - 6 * 8 - 16 * 8 - 8 - 256)),
    ]


class ADDRESS64(ctypes.Structure):  # Offset 8 + Segment 2 + Mode enum 4
    _fields_ = [("Offset", ctypes.c_uint64), ("Segment", w.WORD), ("Mode", w.DWORD)]


class KDHELP64(ctypes.Structure):
    _fields_ = [
        ("Thread", ctypes.c_uint64),
        ("ThCallbackStack", w.DWORD), ("ThCallbackBStore", w.DWORD),
        ("NextCallback", w.DWORD), ("FramePointer", w.DWORD),
        ("KiCallUserMode", ctypes.c_uint64),
        ("KeUserCallbackDispatcher", ctypes.c_uint64),
        ("SystemRangeStart", ctypes.c_uint64),
        ("KiUserExceptionDispatcher", ctypes.c_uint64),
        ("StackBase", ctypes.c_uint64), ("StackLimit", ctypes.c_uint64),
        ("BuildVersion", w.DWORD),
        ("RetpolineStubFunctionTableSize", w.DWORD),
        ("RetpolineStubFunctionTable", w.DWORD),
        ("RetpolineStubOffset", w.DWORD),
        ("RetpolineStubSize", w.DWORD),
        ("Reserved0", w.DWORD), ("Reserved1", w.DWORD),
    ]


class STACKFRAME64(ctypes.Structure):
    # Modern SDK layout. Struct is at least as large as the API expects (incl.
    # trailing Params/ContextPrivate) so StackWalk64 can never write past it.
    _fields_ = [
        ("AddrPC", ADDRESS64),
        ("AddrReturn", ADDRESS64),
        ("AddrFrame", ADDRESS64),
        ("AddrStack", ADDRESS64),
        ("AddrBStore", ADDRESS64),
        ("FuncTableEntry", ctypes.c_void_p),
        ("Params", ctypes.c_uint64 * 4),
        ("Far", w.BOOL), ("Virtual", w.BOOL),
        ("Reserved", ctypes.c_uint64 * 3),
        ("KdHelp", KDHELP64),
        ("AddrFrameStore", ADDRESS64),
        ("PointerData", ctypes.c_uint64 * 2),
        ("Params2", ctypes.c_uint64 * 4),
        ("ContextPrivate", w.DWORD * 5),
    ]


class _LineInfo(ctypes.Structure):
    _fields_ = [
        ("SizeOfStruct", w.DWORD), ("Key", ctypes.c_void_p),
        ("LineNumber", w.DWORD), ("FileName", ctypes.c_char_p),
        ("Address", ctypes.c_uint64),
    ]


# ---------------------------------------------------------------------------
# minidump parsing
# ---------------------------------------------------------------------------
class Minidump:
    def __init__(self, path):
        self.data = open(path, "rb").read()
        self.header = MiniDumpHeader.from_buffer_copy(self.data, 0)
        if self.header.Signature != 0x504D444D:  # 'MDMP'
            raise ValueError(f"{path}: not a minidump (signature {self.header.Signature:#x})")
        self.streams = self._read_directory()
        self.modules = []          # list of (path, base, size)
        self.exception = None      # (thread_id, code, address)
        self.memory_ranges = []    # list of (start, bytes)
        self.fault_context = None  # CONTEXT
        self._parse()

    def _read_directory(self):
        streams = {}
        for i in range(self.header.NumberOfStreams):
            off = self.header.StreamDirectoryRva + i * ctypes.sizeof(MiniDumpDirectory)
            d = MiniDumpDirectory.from_buffer_copy(self.data, off)
            streams[d.StreamType] = d
        return streams

    def _str16(self, rva):
        # MINIDUMP_STRING: [DWORD byteLength][UTF-16LE chars] (no NUL terminator
        # required by the format; Length is the byte count of the char data).
        if not rva or rva + 4 > len(self.data):
            return ""
        length = struct.unpack_from("<I", self.data, rva)[0]
        if length == 0 or rva + 4 + length > len(self.data):
            return ""
        return self.data[rva + 4: rva + 4 + length].decode("utf-16-le", "replace")

    def _parse(self):
        if STREAM_MODULE_LIST in self.streams:
            d = self.streams[STREAM_MODULE_LIST]
            count = struct.unpack_from("<I", self.data, d.Rva)[0]
            # stride = sizeof(MINIDUMP_MODULE). Derive from the stream size first
            # (DataSize / count), fall back to known MSVC sizes.
            candidates = []
            if count:
                candidates.append(d.DataSize // count)
            for c in (112, 108):
                if c not in candidates:
                    candidates.append(c)
            for stride in candidates:
                mods, ok = self._parse_modules(d.Rva + 4, count, stride)
                if ok >= max(1, count * 3 // 4):
                    self.modules = mods
                    break
        if STREAM_EXCEPTION in self.streams:
            d = self.streams[STREAM_EXCEPTION]
            es = MiniDumpExceptionStream.from_buffer_copy(self.data, d.Rva)
            self.exception = (es.ThreadId, es.ExceptionRecord.ExceptionCode,
                              es.ExceptionRecord.ExceptionAddress)
            size = es.ThreadContext.DataSize
            raw = self.data[es.ThreadContext.Rva: es.ThreadContext.Rva + size]
            if raw:
                ctx = CONTEXT()
                ctypes.memmove(ctypes.byref(ctx), raw, min(len(raw), ctypes.sizeof(CONTEXT)))
                self.fault_context = ctx
        self.memory_ranges = self._read_memory_ranges()

    def _parse_modules(self, off, count, stride):
        mods = []
        ok = 0
        for _ in range(count):
            m = MiniDumpModule.from_buffer_copy(self.data, off)
            name = self._str16(m.ModuleNameRva)
            if 0x10000 < m.BaseOfImage < 0x7FFF000000000000 and m.SizeOfImage > 0 \
                    and ("." in name or "\\" in name):
                ok += 1
            mods.append((name, m.BaseOfImage, m.SizeOfImage))
            off += stride
        return mods, ok

    def _read_memory_ranges(self):
        ranges = []
        if STREAM_MEMORY64_LIST in self.streams:
            d = self.streams[STREAM_MEMORY64_LIST]
            n, base = struct.unpack_from("<QQ", self.data, d.Rva)
            p = d.Rva + 16
            for _ in range(n):
                start, size = struct.unpack_from("<QQ", self.data, p)
                ranges.append((start, self.data[base: base + size]))
                base += size
                p += 16
        elif STREAM_MEMORY_LIST in self.streams:
            d = self.streams[STREAM_MEMORY_LIST]
            n = struct.unpack_from("<I", self.data, d.Rva)[0]
            p = d.Rva + 4
            for _ in range(n):
                start, size, rva = struct.unpack_from("<QII", self.data, p)
                ranges.append((start, self.data[rva: rva + size]))
                p += 16
        return ranges

    def read_memory(self, addr, size):
        for start, blob in self.memory_ranges:
            if start <= addr < start + len(blob):
                off = addr - start
                return blob[off: off + size]
        return None


# ---------------------------------------------------------------------------
# dbghelp symbolization + stack walk
# ---------------------------------------------------------------------------
class Symbolizer:
    def __init__(self, search_dirs):
        dh = ctypes.WinDLL("dbghelp", use_last_error=True)
        self.dbghelp = dh
        dh.SymSetOptions(0x2 | 0x4 | 0x10)  # UNDNAME | DEFERRED_LOADS | LOAD_LINES

        kernel32 = ctypes.WinDLL("kernel32")
        self.hProcess = ctypes.c_void_p(kernel32.GetCurrentProcess())

        dh.SymInitializeW.argtypes = [ctypes.c_void_p, ctypes.c_char_p, w.BOOL]
        dh.SymInitializeW.restype = w.BOOL
        ok = dh.SymInitializeW(self.hProcess, ";".join(search_dirs).encode("utf-8"), False)
        if not ok:
            print(f"WARN: SymInitialize failed (err {ctypes.get_last_error()})")

        dh.SymLoadModuleEx.argtypes = [ctypes.c_void_p, ctypes.c_void_p, ctypes.c_char_p,
                                       ctypes.c_char_p, ctypes.c_uint64, w.DWORD,
                                       ctypes.c_void_p, w.DWORD]
        dh.SymLoadModuleEx.restype = ctypes.c_uint64
        dh.SymRefreshModuleList.argtypes = [ctypes.c_void_p]
        dh.SymRefreshModuleList.restype = w.BOOL

        dh.SymFromAddr.argtypes = [ctypes.c_void_p, ctypes.c_uint64,
                                   ctypes.POINTER(ctypes.c_uint64), ctypes.c_void_p]
        dh.SymFromAddr.restype = w.BOOL
        dh.SymGetLineFromAddr64.argtypes = [ctypes.c_void_p, ctypes.c_uint64,
                                            ctypes.POINTER(ctypes.c_uint64), ctypes.c_void_p]
        dh.SymGetLineFromAddr64.restype = w.BOOL
        dh.SymGetModuleBase64.argtypes = [ctypes.c_void_p, ctypes.c_uint64]
        dh.SymGetModuleBase64.restype = ctypes.c_uint64
        dh.SymFunctionTableAccess64.argtypes = [ctypes.c_void_p, ctypes.c_uint64]
        dh.SymFunctionTableAccess64.restype = ctypes.c_void_p

        # callback wrappers for StackWalk64
        self._fa64 = ctypes.CFUNCTYPE(ctypes.c_void_p, ctypes.c_void_p, ctypes.c_uint64)(
            dh.SymFunctionTableAccess64)
        self._modbase = dh.SymGetModuleBase64

        dh.StackWalk64.argtypes = [w.DWORD, ctypes.c_void_p, ctypes.c_void_p,
                                   ctypes.POINTER(STACKFRAME64), ctypes.POINTER(CONTEXT),
                                   ctypes.c_void_p, ctypes.c_void_p, ctypes.c_void_p,
                                   ctypes.c_void_p]
        dh.StackWalk64.restype = w.BOOL
        self.loaded = set()

    def load_module(self, path, base, size):
        key = (path, base)
        if key in self.loaded:
            return
        self.dbghelp.SymLoadModuleEx(self.hProcess, None, path.encode("utf-8"),
                                     None, base, size, None, 0)
        self.loaded.add(key)

    def symbol(self, addr):
        dh = self.dbghelp
        base = dh.SymGetModuleBase64(self.hProcess, addr)
        label = f"0x{addr:X}" + (f" (rva 0x{addr - base:X})" if base else "")

        class SYM_INFO(ctypes.Structure):
            _fields_ = [
                ("SizeOfStruct", w.DWORD), ("TypeIndex", w.DWORD),
                ("Reserved", ctypes.c_uint64 * 2), ("Index", w.DWORD), ("Size", w.DWORD),
                ("ModBase", ctypes.c_uint64), ("Flags", w.DWORD), ("Value", ctypes.c_uint64),
                ("Address", ctypes.c_uint64), ("Register", w.DWORD), ("Scope", w.DWORD),
                ("Tag", w.DWORD), ("NameLen", w.DWORD), ("MaxNameLen", w.DWORD),
                ("Name", ctypes.c_char * 1024),
            ]

        si = SYM_INFO()
        si.SizeOfStruct = 88          # MSDN: must equal sizeof(SYMBOL_INFO)
        si.MaxNameLen = 1024
        disp = ctypes.c_uint64()
        out = label
        if dh.SymFromAddr(self.hProcess, addr, ctypes.byref(disp), ctypes.byref(si)):
            out = si.Name.decode("utf-8", "replace") + f"+0x{disp.value:X}"
        line = _LineInfo()
        line.SizeOfStruct = ctypes.sizeof(_LineInfo)
        ldisp = ctypes.c_uint64()
        if dh.SymGetLineFromAddr64(self.hProcess, addr, ctypes.byref(ldisp), ctypes.byref(line)) \
                and line.FileName:
            src = line.FileName.decode("utf-8", "replace").replace("E:\\Projects\\CascadedEngine\\", "")
            out += f"  [{src}:{line.LineNumber}]"
        return out


# ---------------------------------------------------------------------------
# main
# ---------------------------------------------------------------------------
def main():
    ap = argparse.ArgumentParser(description="Read a Windows x64 minidump and symbolize the faulting stack.")
    ap.add_argument("dump", help="path to the .dmp file")
    ap.add_argument("--pdb-dir", action="append", default=[], help="extra PDB search dir (repeatable)")
    args = ap.parse_args()

    try:
        md = Minidump(args.dump)
    except (ValueError, OSError) as e:
        print(f"ERROR: cannot read {args.dump}: {e}")
        return 1
    print(f"minidump: {len(md.streams)} streams, {len(md.modules)} modules")

    if not md.exception:
        print("ERROR: no exception stream in dump")
        return 1
    tid, code, addr = md.exception
    names = {0xC0000005: "ACCESS_VIOLATION", 0xC000000D: "ARRAY_BOUNDS_EXCEEDED",
             0x80000003: "BREAKPOINT", 0xC00000FD: "STACK_OVERFLOW",
             0xC000001D: "ILLEGAL_INSTRUCTION", 0xC0000094: "INT_DIVIDE_BY_ZERO"}
    print(f"exception: {names.get(code, 'UNKNOWN')} (0x{code:08X}) tid={tid} address=0x{addr:X}")
    if not md.fault_context:
        print("ERROR: no faulting-thread CONTEXT in dump")
        return 1
    ctx = md.fault_context
    print(f"context: Rip=0x{ctx.Rip:X} Rsp=0x{ctx.Rsp:X} Rbp=0x{ctx.Rbp:X} "
          f"Rax=0x{ctx.Rax:X} Rcx=0x{ctx.Rcx:X} Rdx=0x{ctx.Rdx:X} Rbx=0x{ctx.Rbx:X}")

    # symbol search dirs: PDBs live next to the module paths recorded in the dump
    search_dirs = set(args.pdb_dir)
    for name, _b, _s in md.modules:
        if "\\" in name:
            search_dirs.add(name.rsplit("\\", 1)[0])
    sym = Symbolizer(sorted(search_dirs))

    print("\nloaded modules:")
    for name, base, size in md.modules:
        print(f"  {base:016X} +{size:08X}  {name.split(chr(92))[-1].split('/')[-1]}")
        sym.load_module(name, base, size)
    sym.dbghelp.SymRefreshModuleList(sym.hProcess)

    dh = sym.dbghelp
    frames = _stackwalk(md, sym, ctx, dh)
    print("\ncall stack:")
    shown = 0
    for pc in frames:
        text = sym.symbol(pc)
        # skip obvious data symbols that scan noise produces (vftables, string
        # literals, ILT thunks, CRT dynamic-initializer blocks)
        if "`vftable'" in text or "`string'" in text or "ILT+" in text \
                or "`dynamic initializer'" in text:
            continue
        print(f"  {shown:2d}  0x{pc:X}  {text}")
        shown += 1
        if shown >= 40:
            print("  ... (truncated)")
            break
    if not frames:
        print("  (no frames recovered)")
    return 0


def _stackwalk(md, sym, ctx, dh, max_frames=48):
    """Recover the faulting call stack.

    Prefers StackWalk64 (precise unwind); if dbghelp cannot resolve function
    tables for the dumped modules (common in minidump analysis — see
    SymFunctionTableAccess64 returning NULL), falls back to scanning the
    faulting thread's stack memory for return addresses that land inside a
    loaded module's image range. Frame 0 is always the crash Rip.
    """
    # --- attempt StackWalk64 ---
    @ctypes.CFUNCTYPE(w.BOOL, ctypes.c_void_p, ctypes.c_uint64, ctypes.c_void_p,
                      w.DWORD, ctypes.POINTER(w.DWORD))
    def read_mem(hProcess, qwBase, lpBuffer, nSize, lpNumberOfBytesRead):
        blob = md.read_memory(qwBase, nSize)
        if blob is None:
            lpNumberOfBytesRead.contents.value = 0
            return False
        ctypes.memmove(lpBuffer, blob, len(blob))
        lpNumberOfBytesRead.contents.value = len(blob)
        return True

    frame = STACKFRAME64()
    frame.AddrPC.Offset = ctx.Rip
    frame.AddrPC.Segment = ctx.SegCs
    frame.AddrPC.Mode = 0
    frame.AddrStack.Offset = ctx.Rsp
    frame.AddrStack.Mode = 0
    frame.AddrFrame.Offset = ctx.Rbp
    frame.AddrFrame.Mode = 0

    walked = []
    for _ in range(64):
        ok = dh.StackWalk64(0x8664, sym.hProcess, None, ctypes.byref(frame),
                            ctypes.byref(ctx), read_mem, sym._fa64, sym._modbase, None)
        if not ok or frame.AddrPC.Offset == 0:
            break
        walked.append(frame.AddrPC.Offset)
    if len(walked) >= 2:
        return walked

    # --- fallback: stack scan for return addresses ---
    frames = [ctx.Rip]
    mod_ranges = [(b, b + s) for _, b, s in md.modules]
    stack = md.read_memory(ctx.Rsp, 0x40000)  # up to 256 KB of stack
    if stack:
        for off in range(0, len(stack) - 8, 8):
            val = struct.unpack_from("<Q", stack, off)[0]
            if val in frames:
                continue
            # return address candidates live in a module's image, past its header
            if any(lo + 0x1000 <= val < hi for lo, hi in mod_ranges):
                frames.append(val)
                if len(frames) >= max_frames:
                    break
    return frames


if __name__ == "__main__":
    sys.exit(main())
