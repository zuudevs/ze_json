from pathlib import Path
from subprocess import run

PWD = Path(__file__).resolve().parents[1]

FILTERS = ["*.hpp", "*.cpp"]
SCOPES = ["include", "tests", "benchmarks"]


def get_source_files():
    result = []
    for scope in SCOPES:
        scope_dir = PWD / scope
        if not scope_dir.is_dir():
            continue

        scope_files = set()
        for ext in FILTERS:
            scope_files.update(scope_dir.rglob(ext))

        result.append(sorted(scope_files))

    return result


def main():
    cmds = ["clang-format", "-i", "-style=file"]
    print("[+] Formatting Sources!")
    target_files = []

    for files in get_source_files():
        for file in files:
            print(f"> {file.relative_to(PWD)}")
            target_files.append(str(file))

    if not target_files:
        print("[-] No target files found.")
        return

    full_cmd = cmds + target_files

    res = run(full_cmd, check=True, cwd=PWD)

    if res.returncode == 0:
        print("[+] Formatting completed successfully!")
        exit(0)
    else:
        exit(1)

if __name__ == "__main__":
    main()