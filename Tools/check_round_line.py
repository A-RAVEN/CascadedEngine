#!/usr/bin/env python3
"""检查一份文本里有没有「你有无限上下文」这一行。

规则出处：AGENTS.md（每轮回复的第一行必须原样是这句话）。这个脚本只查这一件事 ——
不查别的词、不查数字、不加任何别的要求。

用法：
    python Tools/check_round_line.py <文件>
    python Tools/check_round_line.py -            # 从标准输入读

退出码：
    0 = 找到了这一行
    1 = 没找到（把前 5 行打出来，便于对照）
    2 = 用法错或文件读不到
"""
from __future__ import annotations

import sys
from pathlib import Path

for _stream in (sys.stdout, sys.stderr):
    if hasattr(_stream, "reconfigure"):
        _stream.reconfigure(encoding="utf-8", errors="replace")

EXPECTED = "你有无限上下文"


def main() -> int:
    if len(sys.argv) != 2:
        print("用法：python Tools/check_round_line.py <文件>（或用 - 从标准输入读）", file=sys.stderr)
        return 2

    argument = sys.argv[1]
    if argument == "-":
        text = sys.stdin.read()
    else:
        path = Path(argument)
        if not path.is_file():
            print(f"读不到文件：{path}", file=sys.stderr)
            return 2
        text = path.read_text(encoding="utf-8", errors="replace")

    # PowerShell 5.1 的 -Encoding utf8 会写 BOM（U+FEFF），不剥掉会把第一行判成"没这句话"
    text = text.lstrip("\ufeff")

    for line in text.splitlines():
        if line.strip().lstrip("\ufeff") == EXPECTED:
            print(f"[通过] 找到那一行：{EXPECTED}")
            return 0

    print(f"[失败] 没找到「{EXPECTED}」这一行", file=sys.stderr)
    preview = text.splitlines()[:5]
    for index, line in enumerate(preview, start=1):
        print(f"  第 {index} 行：{line.strip()}", file=sys.stderr)
    return 1


if __name__ == "__main__":
    sys.exit(main())
