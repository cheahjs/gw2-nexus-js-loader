#!/usr/bin/env python3
"""
Build helper for Wine/Rosetta environments where ninja can't reliably spawn
subprocesses (Rosetta intermittently kills Wine child processes with GDT
selector errors, causing ninja to silently produce incomplete builds).

Reads the compile database from ninja and executes commands in parallel.
Supports incremental builds by skipping .obj files newer than their source.
Then runs the link commands using response files for long command lines.
"""
import argparse
import json
import subprocess
import sys
import os
import glob
import concurrent.futures
import threading


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


def compile_one(index, entry, total, progress_lock, progress_counter, failed_counter,
                failed_outputs, abort_event):
    """Compile a single entry. Called from thread pool workers."""
    if abort_event.is_set():
        return

    cmd = entry["command"]
    directory = entry.get("directory", BUILD_DIR_WIN)
    output = entry.get("output", "?")

    # Each thread gets its own batch file to avoid write conflicts
    bat_name = f"compile_cmd_{index}.bat"
    bat_path_linux = os.path.join(DRIVE_C, bat_name)
    bat_path_win = f"C:\\{bat_name}"

    bat_content = f"@echo off\ncall C:\\x64.bat\ncd /d {directory}\n{cmd}\n"
    result = run_wine_bat(bat_path_linux, bat_path_win, bat_content)

    with progress_lock:
        progress_counter[0] += 1
        n = progress_counter[0]

    if result.returncode != 0:
        with progress_lock:
            failed_counter[0] += 1
            failed_outputs.append(output)
            current_failed = failed_counter[0]
        print(f"[{n}/{total}] FAILED {output} (exit {result.returncode})")
        if result.stderr:
            print(result.stderr[:500])
        if result.stdout:
            print(result.stdout[:500])
        if current_failed > 10:
            abort_event.set()
    else:
        print(f"[{n}/{total}] {output}")


def win_to_linux_path(win_path):
    """Convert a Windows path (Z:\\project\\...) to a Linux path (/project/...)."""
    p = win_path.replace("\\", "/")
    if p.startswith("Z:/") or p.startswith("z:/"):
        p = p[2:]
    return p


def is_up_to_date(entry):
    """Check if an .obj file is newer than its source file (incremental build)."""
    output = entry.get("output", "")
    source = entry.get("file", "")
    if not output or not source:
        return False

    # Output is relative to the build directory
    obj_path = os.path.join(BUILD_DIR_LINUX, output.replace("\\", "/"))
    src_path = win_to_linux_path(source)

    try:
        obj_mtime = os.path.getmtime(obj_path)
        src_mtime = os.path.getmtime(src_path)
        return obj_mtime > src_mtime
    except OSError:
        return False


def compile_all(compdb_linux, jobs):
    """Compile all entries from the compile database using parallel workers."""
    with open(compdb_linux) as f:
        commands = json.load(f)

    compile_cmds = [c for c in commands
                    if c.get("command", "").strip()
                    and c.get("output", "").endswith(".obj")]

    # Incremental build: skip entries where .obj is newer than source
    stale = [c for c in compile_cmds if not is_up_to_date(c)]
    skipped = len(compile_cmds) - len(stale)
    if skipped > 0:
        print(f"Skipping {skipped}/{len(compile_cmds)} up-to-date targets")
    compile_cmds = stale

    total = len(compile_cmds)
    if total == 0:
        print("All targets up to date, nothing to compile.")
        return 0
    print(f"Compiling {total} targets with {jobs} parallel workers...")

    progress_lock = threading.Lock()
    progress_counter = [0]  # mutable counter shared across threads
    failed_counter = [0]
    failed_outputs = []
    abort_event = threading.Event()

    with concurrent.futures.ThreadPoolExecutor(max_workers=jobs) as executor:
        futures = []
        for i, entry in enumerate(compile_cmds):
            if abort_event.is_set():
                break
            fut = executor.submit(
                compile_one, i, entry, total,
                progress_lock, progress_counter, failed_counter,
                failed_outputs, abort_event,
            )
            futures.append(fut)

        # Wait for all submitted futures to complete
        for fut in concurrent.futures.as_completed(futures):
            fut.result()  # propagate exceptions

    if abort_event.is_set():
        print("Too many failures, stopping.")
        sys.exit(1)

    failed = failed_counter[0]
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


def link_cef_host():
    """Link nexus_js_cef_host.exe."""
    print("\n=== Linking nexus_js_cef_host.exe ===")

    host_objs = find_obj_files("CMakeFiles/nexus_js_cef_host.dir")
    if not host_objs:
        print("ERROR: No CEF host .obj files found!")
        return False

    print(f"  Found {len(host_objs)} object files")
    obj_list = " ".join(f'"{o}"' for o in host_objs)

    bat_linux = os.path.join(DRIVE_C, "link_cmd.bat")
    bat_content = (
        f"@echo off\n"
        f"call C:\\x64.bat\n"
        f"cd /d {BUILD_DIR_WIN}\n"
        f"link.exe /nologo /machine:x64 /INCREMENTAL:NO /subsystem:windows "
        f"/OUT:nexus_js_cef_host.exe "
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
    """Link nexus_js_loader.dll (no CEF linkage — uses out-of-process host)."""
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
        # Add libraries (no CEF — it runs out-of-process now)
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
    parser = argparse.ArgumentParser(description="Wine/MSVC build helper")
    parser.add_argument("--link-only", action="store_true",
                        help="Skip compilation, only run link steps")
    parser.add_argument("-j", "--jobs", type=int, default=os.cpu_count(),
                        help="Number of parallel compile workers (default: CPU count)")
    args = parser.parse_args()

    if not args.link_only:
        compdb_linux = os.path.join(DRIVE_C, "compdb.json")

        if not os.path.exists(compdb_linux):
            print("ERROR: compdb.json not found. Run ninja -t compdb first.", file=sys.stderr)
            sys.exit(1)

        # Step 1: Compile all source files
        compile_failures = compile_all(compdb_linux, args.jobs)
    else:
        print("=== Link-only mode (skipping compilation) ===")
        compile_failures = 0

    # Non-critical wrapper files may fail (e.g. cef_scoped_library_loader_win.cc,
    # cef_util_win.cc). Continue to linking as long as project source files compiled.
    project_objs = find_obj_files("CMakeFiles/nexus_js_loader.dir")
    sub_objs = find_obj_files("CMakeFiles/nexus_js_subprocess.dir")
    host_objs = find_obj_files("CMakeFiles/nexus_js_cef_host.dir")

    if len(project_objs) < 11:  # We expect 11 plugin obj files
        print(f"\nERROR: Only {len(project_objs)} plugin obj files found (expected 11)")
        sys.exit(1)
    if len(sub_objs) < 4:  # We expect 4 subprocess obj files
        print(f"\nERROR: Only {len(sub_objs)} subprocess obj files found (expected 4)")
        sys.exit(1)
    if len(host_objs) < 6:  # We expect 6 host obj files
        print(f"\nERROR: Only {len(host_objs)} CEF host obj files found (expected 6)")
        sys.exit(1)

    if compile_failures > 0:
        print(f"\n{compile_failures} non-critical compile failure(s), continuing to link...")

    # Check if link outputs already exist and no sources were recompiled
    link_outputs = ["libcef_dll_wrapper.lib", "nexus_js_subprocess.exe",
                    "nexus_js_cef_host.exe", "nexus_js_loader.dll"]
    all_exist = all(os.path.exists(os.path.join(BUILD_DIR_LINUX, f)) for f in link_outputs)
    if all_exist and compile_failures == 0 and not args.link_only:
        # Check if any .obj is newer than the final outputs
        dll_mtime = os.path.getmtime(os.path.join(BUILD_DIR_LINUX, "nexus_js_loader.dll"))
        any_newer = False
        for subdir in ["CMakeFiles/nexus_js_loader.dir", "CMakeFiles/nexus_js_subprocess.dir",
                        "CMakeFiles/nexus_js_cef_host.dir", "CMakeFiles/libcef_dll_wrapper.dir"]:
            for obj_linux in glob.glob(os.path.join(BUILD_DIR_LINUX, subdir, "**/*.obj"), recursive=True):
                if os.path.getmtime(obj_linux) > dll_mtime:
                    any_newer = True
                    break
            if any_newer:
                break
        if not any_newer:
            print("\nAll outputs up to date, skipping link steps.")
            print("\nBuild succeeded! (nothing to do)")
            return

    # Step 2: Create static library
    if not link_static_lib():
        print("\nFATAL: Failed to create libcef_dll_wrapper.lib")
        sys.exit(1)

    # Step 3: Link subprocess exe
    if not link_subprocess():
        print("\nFATAL: Failed to link nexus_js_subprocess.exe")
        sys.exit(1)

    # Step 4: Link CEF host exe
    if not link_cef_host():
        print("\nFATAL: Failed to link nexus_js_cef_host.exe")
        sys.exit(1)

    # Step 5: Link main DLL (no CEF linkage)
    if not link_dll():
        print("\nFATAL: Failed to link nexus_js_loader.dll")
        sys.exit(1)

    # Step 6: Verify outputs exist
    print("\n=== Verifying outputs ===")
    outputs = ["libcef_dll_wrapper.lib", "nexus_js_subprocess.exe",
               "nexus_js_cef_host.exe", "nexus_js_loader.dll"]
    all_ok = True
    for out in outputs:
        path = os.path.join(BUILD_DIR_LINUX, out)
        if os.path.exists(path):
            size = os.path.getsize(path)
            print(f"  {out}: {size:,} bytes")
        else:
            print(f"  {out}: MISSING!")
            all_ok = False

    if not all_ok:
        print("\nBuild FAILED - some outputs are missing")
        sys.exit(1)

    # Step 7: Copy executables into nexus_js_loader/ subfolder
    #         (mirrors the CMake post-build commands that ninja would run)
    print("\n=== Copying executables to subfolder ===")
    subfolder = os.path.join(BUILD_DIR_LINUX, "nexus_js_loader")
    os.makedirs(subfolder, exist_ok=True)
    import shutil
    for exe in ["nexus_js_subprocess.exe", "nexus_js_cef_host.exe"]:
        src = os.path.join(BUILD_DIR_LINUX, exe)
        dst = os.path.join(subfolder, exe)
        shutil.copy2(src, dst)
        print(f"  {exe} -> nexus_js_loader/{exe}")

    print("\nBuild succeeded!")


if __name__ == "__main__":
    main()
