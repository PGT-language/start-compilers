import subprocess
import os
import shutil
import sys
from pathlib import Path

BUILD_DIR = Path(__file__).resolve().parent / "build"
COMPILE_DIR = Path(__file__).resolve().parent / "compile"
EXECUTABLE = "pgt"


def run(cmd: list[str], cwd: Path) -> None:
    print(f"\n▶ {' '.join(str(c) for c in cmd)}")
    result = subprocess.run(cmd, cwd=cwd)
    if result.returncode != 0:
        print(f"\Command finished with code {result.returncode}")
        sys.exit(result.returncode)


def main() -> None:
    BUILD_DIR.mkdir(exist_ok=True)

    root = Path(__file__).resolve().parent

    print("Configuring with CMake...")
    run(["cmake", str(root)], cwd=BUILD_DIR)

    print("Building...")
    cpu_count = os.cpu_count() or 1
    run(["make", f"-j{cpu_count}"], cwd=BUILD_DIR)

    binary = BUILD_DIR / EXECUTABLE
    if not binary.exists():
        print(f"Executable file not found: {binary}")
        sys.exit(1)

    COMPILE_DIR.mkdir(exist_ok=True)
    dest = COMPILE_DIR / EXECUTABLE
    shutil.copy2(binary, dest)
    print(f"\n Executable file copied to: {dest}")


if __name__ == "__main__":
    main()
