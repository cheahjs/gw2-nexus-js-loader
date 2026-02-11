#!/usr/bin/env python3
"""
Build helper for Wine/Rosetta environments where ninja can't spawn subprocesses.
Reads the compile database from ninja and executes commands sequentially.
Then runs the link commands using response files for long command lines.
"""
import json
import subprocess
import sys
import os
import glob


BUILD_DIR_WIN = "Z:\\project\\build"
BUILD_DIR_LINUX = "/project/build"
DRIVE_C = "/home/wine/.wine/drive_c"
CEF_LIB = "Z:\\project\\third_party\\cef\\cef_dist\\Release\\libcef.lib"

# System libraries that CMake adds to link commands
SYSTEM_LIBS = (
    "kernel32.lib user32.lib gdi32.lib winspool.lib shell32.lib "
    "ole32.lib oleaut32.lib uuid.lib comdlg32.lib advapi32.lib"
)


def run_wine_bat(bat_linux_path, bat_win_path, content, timeout=120):
    """Write a batch file and execute it via wine64."""
    with open(bat_linux_path, "w") as f:
        f.write(content)
    result = subprocess.run(
        ["wine64", "cmd", "/c", bat_win_path],
        capture_output=True, text=True, timeout=timeout
    )
    return result


def compile_all(compdb_linux):
    """Compile all entries from the compile database."""
    with open(compdb_linux) as f:
        commands = json.load(f)

    compile_cmds = [c for c in commands if c.get("command", "").strip()]
    total = len(compile_cmds)
    print(f"Compiling {total} targets...")

    failed = 0
    failed_outputs = []
    bat_path_linux = os.path.join(DRIVE_C, "compile_cmd.bat")
    bat_path_win = "C:\\compile_cmd.bat"

    for i, entry in enumerate(compile_cmds):
        cmd = entry["command"]
        directory = entry.get("directory", BUILD_DIR_WIN)
        output = entry.get("output", "?")

        print(f"[{i+1}/{total}] {output}")

        bat_content = f"@echo off\ncall C:\\x64.bat\ncd /d {directory}\n{cmd}\n"
        result = run_wine_bat(bat_path_linux, bat_path_win, bat_content)

        if result.returncode != 0:
            print(f"  FAILED (exit {result.returncode})")
            if result.stderr:
                print(result.stderr[:500])
            if result.stdout:
                print(result.stdout[:500])
            failed += 1
            failed_outputs.append(output)
            if failed > 10:
                print("Too many failures, stopping.")
                sys.exit(1)

    print(f"\nCompilation: {total - failed}/{total} succeeded, {failed} failed")
    if failed_outputs:
        print("Failed files:")
        for f in failed_outputs:
            print(f"  - {f}")

    return failed


def find_obj_files(subdir, pattern="**/*.obj"):
    """Find .obj files on the Linux filesystem and return Windows paths."""
    search_dir = os.path.join(BUILD_DIR_LINUX, subdir)
    objs_linux = sorted(glob.glob(os.path.join(search_dir, pattern), recursive=True))
    # Convert Linux paths to Windows paths
    objs_win = []
    for p in objs_linux:
        # /project/build/CMakeFiles/... -> Z:\project\build\CMakeFiles\...
        rel = os.path.relpath(p, "/project")
        win_path = "Z:\\project\\" + rel.replace("/", "\\")
        objs_win.append(win_path)
    return objs_win


def link_static_lib():
    """Create libcef_dll_wrapper.lib using a response file."""
    print("\n=== Linking libcef_dll_wrapper.lib ===")

    wrapper_objs = find_obj_files("CMakeFiles/libcef_dll_wrapper.dir")
    if not wrapper_objs:
        print("ERROR: No wrapper .obj files found!")
        return False

    print(f"  Found {len(wrapper_objs)} object files")

    # Write response file (one obj per line to avoid command line length limits)
    rsp_linux = os.path.join(DRIVE_C, "lib_wrapper.rsp")
    rsp_win = "C:\\lib_wrapper.rsp"
    with open(rsp_linux, "w") as f:
        for obj in wrapper_objs:
            f.write(f'"{obj}"\n')

    bat_linux = os.path.join(DRIVE_C, "link_cmd.bat")
    bat_content = (
        f"@echo off\n"
        f"call C:\\x64.bat\n"
        f"cd /d {BUILD_DIR_WIN}\n"
        f"lib.exe /nologo /machine:x64 /OUT:libcef_dll_wrapper.lib @{rsp_win}\n"
    )

    result = run_wine_bat(bat_linux, "C:\\link_cmd.bat", bat_content, timeout=300)
    if result.returncode != 0:
        print(f"  FAILED: {result.stderr[:500]} {result.stdout[:500]}")
        return False
    else:
        print("  OK")
        return True


def link_subprocess():
    """Link nexus_js_subprocess.exe."""
    print("\n=== Linking nexus_js_subprocess.exe ===")

    sub_objs = find_obj_files("CMakeFiles/nexus_js_subprocess.dir")
    if not sub_objs:
        print("ERROR: No subprocess .obj files found!")
        return False

    print(f"  Found {len(sub_objs)} object files")
    obj_list = " ".join(f'"{o}"' for o in sub_objs)

    bat_linux = os.path.join(DRIVE_C, "link_cmd.bat")
    bat_content = (
        f"@echo off\n"
        f"call C:\\x64.bat\n"
        f"cd /d {BUILD_DIR_WIN}\n"
        f"link.exe /nologo /machine:x64 /INCREMENTAL:NO /subsystem:windows "
        f"/OUT:nexus_js_subprocess.exe "
        f"{obj_list} "
        f"libcef_dll_wrapper.lib "
        f"{CEF_LIB} "
        f"{SYSTEM_LIBS}\n"
    )

    result = run_wine_bat(bat_linux, "C:\\link_cmd.bat", bat_content, timeout=300)
    if result.returncode != 0:
        print(f"  FAILED: {result.stderr[:500]} {result.stdout[:500]}")
        return False
    else:
        print("  OK")
        return True


def link_dll():
    """Link nexus_js_loader.dll."""
    print("\n=== Linking nexus_js_loader.dll ===")

    dll_objs = find_obj_files("CMakeFiles/nexus_js_loader.dir")
    if not dll_objs:
        print("ERROR: No loader .obj files found!")
        return False

    print(f"  Found {len(dll_objs)} object files")

    # Use response file for DLL too (in case of many objs in future)
    rsp_linux = os.path.join(DRIVE_C, "link_dll.rsp")
    rsp_win = "C:\\link_dll.rsp"
    with open(rsp_linux, "w") as f:
        for obj in dll_objs:
            f.write(f'"{obj}"\n')
        # Add libraries
        f.write("libcef_dll_wrapper.lib\n")
        f.write(f"{CEF_LIB}\n")
        f.write("d3d11.lib\n")
        f.write("dxgi.lib\n")
        for lib in SYSTEM_LIBS.split():
            f.write(f"{lib}\n")

    bat_linux = os.path.join(DRIVE_C, "link_cmd.bat")
    bat_content = (
        f"@echo off\n"
        f"call C:\\x64.bat\n"
        f"cd /d {BUILD_DIR_WIN}\n"
        f"link.exe /nologo /machine:x64 /INCREMENTAL:NO /DLL "
        f"/OUT:nexus_js_loader.dll "
        f"/IMPLIB:nexus_js_loader.lib "
        f"@{rsp_win}\n"
    )

    result = run_wine_bat(bat_linux, "C:\\link_cmd.bat", bat_content, timeout=300)
    if result.returncode != 0:
        print(f"  FAILED: {result.stderr[:500]} {result.stdout[:500]}")
        return False
    else:
        print("  OK")
        return True


def main():
    link_only = "--link-only" in sys.argv

    if not link_only:
        compdb_linux = os.path.join(DRIVE_C, "compdb.json")

        if not os.path.exists(compdb_linux):
            print("ERROR: compdb.json not found. Run ninja -t compdb first.", file=sys.stderr)
            sys.exit(1)

        # Step 1: Compile all source files
        compile_failures = compile_all(compdb_linux)
    else:
        print("=== Link-only mode (skipping compilation) ===")
        compile_failures = 0

    # Non-critical wrapper files may fail (e.g. cef_scoped_library_loader_win.cc,
    # cef_util_win.cc). Continue to linking as long as project source files compiled.
    project_objs = find_obj_files("CMakeFiles/nexus_js_loader.dir")
    sub_objs = find_obj_files("CMakeFiles/nexus_js_subprocess.dir")

    if len(project_objs) < 11:  # We expect 11 plugin obj files
        print(f"\nERROR: Only {len(project_objs)} plugin obj files found (expected 11)")
        sys.exit(1)
    if len(sub_objs) < 4:  # We expect 4 subprocess obj files
        print(f"\nERROR: Only {len(sub_objs)} subprocess obj files found (expected 4)")
        sys.exit(1)

    if compile_failures > 0:
        print(f"\n{compile_failures} non-critical compile failure(s), continuing to link...")

    # Step 2: Create static library
    if not link_static_lib():
        print("\nFATAL: Failed to create libcef_dll_wrapper.lib")
        sys.exit(1)

    # Step 3: Link subprocess exe (must be before DLL)
    if not link_subprocess():
        print("\nFATAL: Failed to link nexus_js_subprocess.exe")
        sys.exit(1)

    # Step 4: Link main DLL
    if not link_dll():
        print("\nFATAL: Failed to link nexus_js_loader.dll")
        sys.exit(1)

    # Step 5: Verify outputs exist
    print("\n=== Verifying outputs ===")
    outputs = ["libcef_dll_wrapper.lib", "nexus_js_subprocess.exe", "nexus_js_loader.dll"]
    all_ok = True
    for out in outputs:
        path = os.path.join(BUILD_DIR_LINUX, out)
        if os.path.exists(path):
            size = os.path.getsize(path)
            print(f"  {out}: {size:,} bytes")
        else:
            print(f"  {out}: MISSING!")
            all_ok = False

    if all_ok:
        print("\nBuild succeeded!")
    else:
        print("\nBuild FAILED - some outputs are missing")
        sys.exit(1)


if __name__ == "__main__":
    main()
