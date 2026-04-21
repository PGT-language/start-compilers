#!/usr/bin/env python3

import argparse
import os
import shutil
import stat
import subprocess
import sys
import tempfile
from pathlib import Path
from typing import List, Optional


COMPILER_NAME = "pgt"
CPP_STANDARD = "-std=c++17"
SOURCE_FILES = (
    "src/main.cpp",
    "src/Lexer.cpp",
    "src/Parser.cpp",
    "src/Interpreter.cpp",
    "src/Utils.cpp",
    "src/SemanticAnalyzer.cpp",
    "src/GarbageCollector.cpp",
)
LINK_LIBRARIES = ("-lssl", "-lcrypto")


def project_root() -> Path:
    return Path(__file__).resolve().parent


def parse_args() -> argparse.Namespace:
    parser = argparse.ArgumentParser(
        description="Build the PGT compiler from source and install it into PATH."
    )
    parser.add_argument(
        "--install-dir",
        type=Path,
        help="Directory from PATH where the compiler should be installed.",
    )
    parser.add_argument(
        "--cxx",
        default=os.environ.get("CXX", "g++"),
        help="C++ compiler command. Defaults to CXX or g++.",
    )
    return parser.parse_args()


def path_entries() -> List[Path]:
    entries = []
    for raw_entry in os.environ.get("PATH", "").split(os.pathsep):
        if raw_entry:
            entries.append(Path(raw_entry).expanduser())
    return entries


def is_same_path(left: Path, right: Path) -> bool:
    try:
        return left.resolve() == right.resolve()
    except OSError:
        return left.absolute() == right.absolute()


def is_in_path(directory: Path) -> bool:
    return any(is_same_path(directory, entry) for entry in path_entries())


def find_writable_path_dir() -> Optional[Path]:
    for entry in path_entries():
        if entry.is_dir() and os.access(entry, os.W_OK):
            return entry
    return None


def find_installed_compiler() -> Optional[Path]:
    installed = shutil.which(COMPILER_NAME)
    if installed:
        return Path(installed)
    return None


def confirm_replace(existing_binary: Path) -> bool:
    print(f"Found installed compiler: {existing_binary}")
    answer = input("Delete it and install a new one? [y/N]: ").strip().lower()
    return answer in {"y", "yes"}


def remove_existing_binary(existing_binary: Path) -> None:
    if existing_binary.name != COMPILER_NAME:
        raise RuntimeError(f"Refusing to remove unexpected file: {existing_binary}")
    existing_binary.unlink()
    print(f"Removed old compiler: {existing_binary}")


def choose_install_dir(args: argparse.Namespace, existing_binary: Optional[Path]) -> Path:
    if args.install_dir:
        install_dir = args.install_dir.expanduser().resolve()
        if not is_in_path(install_dir):
            raise RuntimeError(f"Install directory is not in PATH: {install_dir}")
        return install_dir

    if existing_binary:
        return existing_binary.parent

    install_dir = find_writable_path_dir()
    if install_dir:
        return install_dir.resolve()

    raise RuntimeError("No writable directory was found in PATH.")


def ensure_install_target(install_dir: Path) -> Path:
    if not install_dir.exists():
        install_dir.mkdir(parents=True)
    if not install_dir.is_dir():
        raise RuntimeError(f"Install target is not a directory: {install_dir}")
    if not os.access(install_dir, os.W_OK):
        raise RuntimeError(f"Install directory is not writable: {install_dir}")
    return install_dir / COMPILER_NAME


def run_command(command: List[str], cwd: Path) -> None:
    print(" ".join(command))
    subprocess.run(command, cwd=cwd, check=True)


def compile_sources(cxx: str, build_dir: Path, root: Path) -> List[Path]:
    object_files = []
    include_flags = (f"-I{root}", f"-I{root / 'src'}")

    for source_file in SOURCE_FILES:
        source_path = root / source_file
        object_path = build_dir / f"{source_path.stem}.o"
        command = [
            cxx,
            CPP_STANDARD,
            *include_flags,
            "-c",
            str(source_path),
            "-o",
            str(object_path),
        ]
        run_command(command, root)
        object_files.append(object_path)

    return object_files


def link_compiler(cxx: str, build_dir: Path, root: Path, object_files: List[Path]) -> Path:
    binary_path = build_dir / COMPILER_NAME
    command = [
        cxx,
        CPP_STANDARD,
        "-o",
        str(binary_path),
        *(str(object_file) for object_file in object_files),
        *LINK_LIBRARIES,
    ]
    run_command(command, root)
    return binary_path


def build_compiler(cxx: str, build_dir: Path) -> Path:
    root = project_root()
    object_files = compile_sources(cxx, build_dir, root)
    return link_compiler(cxx, build_dir, root, object_files)


def install_compiler(binary_path: Path, target_path: Path) -> None:
    shutil.copy2(binary_path, target_path)
    current_mode = target_path.stat().st_mode
    target_path.chmod(current_mode | stat.S_IXUSR | stat.S_IXGRP | stat.S_IXOTH)
    print(f"Installed compiler: {target_path}")


def main() -> int:
    try:
        args = parse_args()
        existing_binary = find_installed_compiler()
        install_dir = choose_install_dir(args, existing_binary)
        target_path = ensure_install_target(install_dir)

        if existing_binary and not confirm_replace(existing_binary):
            print("Installation cancelled.")
            return 1

        if target_path.exists() and not existing_binary:
            if not confirm_replace(target_path):
                print("Installation cancelled.")
                return 1
            existing_binary = target_path

        with tempfile.TemporaryDirectory(prefix=f"{COMPILER_NAME}-build-") as raw_build_dir:
            binary_path = build_compiler(args.cxx, Path(raw_build_dir))
            if existing_binary:
                remove_existing_binary(existing_binary)
            install_compiler(binary_path, target_path)
    except FileNotFoundError:
        print(f"Error: C++ compiler was not found: {args.cxx}", file=sys.stderr)
        return 1
    except subprocess.CalledProcessError as error:
        print(f"Build failed with exit code {error.returncode}.", file=sys.stderr)
        return error.returncode
    except RuntimeError as error:
        print(f"Error: {error}", file=sys.stderr)
        return 1

    return 0


if __name__ == "__main__":
    sys.exit(main())
