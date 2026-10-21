"""Compile the isolated DB-worker harness against an existing AzerothCore build.

The output directory must be a separate test workspace, never the live build tree.
The harness is not run automatically because it requires a disposable MySQL schema.
"""
import argparse
import json
from pathlib import Path
import shlex
import subprocess


def main():
    parser = argparse.ArgumentParser()
    parser.add_argument("--build", type=Path, required=True, help="AzerothCore CMake build directory")
    parser.add_argument("--output", type=Path, required=True, help="Separate disposable directory")
    args = parser.parse_args()
    build = args.build.resolve()
    output = args.output.resolve()
    source = Path(__file__).with_name("isolated_db_worker.cpp").resolve()
    if output == build or build in output.parents:
        parser.error("Output must be outside the AzerothCore build directory")
    output.mkdir(parents=True, exist_ok=True)

    entries = json.loads((build / "compile_commands.json").read_text())
    entry = next(e for e in entries if e["file"].endswith("/Database/Implementation/CharacterDatabase.cpp"))
    compile_args = shlex.split(entry["command"])
    compile_args[compile_args.index(entry["file"])] = str(source)
    object_path = output / "isolated_db_worker.o"
    compile_args[compile_args.index("-o") + 1] = str(object_path)
    subprocess.run(compile_args, cwd=entry["directory"], check=True)

    # Reuse the installed core's linking configuration without any worldserver
    # object files or modifications to the original libraries.
    linkfile = build / "src/server/apps/CMakeFiles/worldserver.dir/link.txt"
    original = shlex.split(linkfile.read_text())
    libs_start = next(i for i, arg in enumerate(original) if arg.startswith("-Wl,-rpath,"))
    libraries = original[libs_start:]
    subprocess.run(
        [original[0], "-O2", "-pthread", "-o", str(output / "isolated_db_worker"),
         str(object_path)] + libraries,
        cwd=build / "src/server/apps",
        check=True,
    )
    print(output / "isolated_db_worker")


if __name__ == "__main__":
    main()
