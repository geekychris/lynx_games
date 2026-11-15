"""tools/levels.txt (the 150 repaired levels, the same cells as the Amiga
original's level.c) -> src/levels_data.c / .h with tools/gridpack.py, all in
RAM with the program (8.3 KB). The Atari 8-bit port's packer, without its
$A000 segment.

    python3 tools/pack_levels.py
"""
import subprocess
import sys
from pathlib import Path

HERE = Path(__file__).resolve().parents[1]
SRC = HERE / "src"

subprocess.run([sys.executable, str(HERE / "tools" / "gridpack.py"), str(HERE / "tools" / "levels.txt"),
                "--prefix", "lv", "--no-names", "-o", str(SRC / "levels_data.c"),
                "--header", str(SRC / "levels_data.h")], check=True)
