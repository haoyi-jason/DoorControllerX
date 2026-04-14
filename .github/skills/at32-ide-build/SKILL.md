---
name: at32-ide-build
description: "Use when building, cleaning, or diagnosing AT32_IDE / AT32 WorkBench embedded projects. Handles Debug/Release builds, AT32IDE bundled toolchain setup, arm-none-eabi issues, undefined reference linker failures, .project linkedResources, and generated subdir.mk mismatches. Keywords: AT32_IDE build, AT32 WorkBench, arm-none-eabi, undefined reference, subdir.mk, .project, .cproject, linkedResources."
---

# AT32_IDE Build

Use this skill for AT32 IDE / Eclipse CDT projects that generate makefiles under `project/AT32_IDE/Debug` or `project/AT32_IDE/Release`.

## Goals

- Build the selected configuration with the AT32IDE bundled GNU Arm toolchain.
- Diagnose compile, link, and generated-makefile problems.
- Fix the real project metadata when source files are missing from IDE builds.
- Keep the workflow portable across AT32 projects.

## Default Toolchain

Prefer the bundled AT32IDE tools on Windows:

- `D:\AT32IDE\platform\tools\gcc-arm-none-eabi-10.3-2021.10\bin`
- `D:\AT32IDE\platform\tools\Build Tools\bin`

If those paths do not exist, search for equivalent `arm-none-eabi-gcc.exe` and `make.exe` under the AT32IDE installation before assuming the toolchain is missing.

## Discovery

1. Find the workspace project that contains `project/AT32_IDE`.
2. Check whether `Debug` or `Release` exists under that folder.
3. Read these files when troubleshooting build composition:
   - `project/AT32_IDE/.project`
   - `project/AT32_IDE/.cproject`
   - `project/AT32_IDE/<Config>/user/subdir.mk`
   - `project/src/*.c`
4. Treat `subdir.mk`, `sources.mk`, and `objects.mk` as generated outputs unless the user explicitly wants a one-off patch.

## Build Commands

Use PowerShell and prepend the AT32IDE tool paths for the current command.

### Debug

```powershell
$env:PATH = "D:\AT32IDE\platform\tools\gcc-arm-none-eabi-10.3-2021.10\bin;D:\AT32IDE\platform\tools\Build Tools\bin;$env:PATH"
Set-Location "<workspace>\<project>\project\AT32_IDE\Debug"
make all 2>&1
```

### Release

```powershell
$env:PATH = "D:\AT32IDE\platform\tools\gcc-arm-none-eabi-10.3-2021.10\bin;D:\AT32IDE\platform\tools\Build Tools\bin;$env:PATH"
Set-Location "<workspace>\<project>\project\AT32_IDE\Release"
make all 2>&1
```

### Clean Then Rebuild

```powershell
$env:PATH = "D:\AT32IDE\platform\tools\gcc-arm-none-eabi-10.3-2021.10\bin;D:\AT32IDE\platform\tools\Build Tools\bin;$env:PATH"
Set-Location "<workspace>\<project>\project\AT32_IDE\Debug"
make clean 2>&1
make all 2>&1
```

## Diagnosis Rules

### If `arm-none-eabi-gcc` is not found

- Confirm the AT32IDE bundled toolchain path exists.
- Confirm `make.exe` exists in `Build Tools\bin`.
- Temporarily prepend both directories to `PATH` for the build command.
- Do not rewrite the project to absolute compiler paths unless the user asks.

### If the linker reports `undefined reference` to a function implemented in `project/src`

1. Confirm the function is defined in a `.c` file under `project/src`.
2. Check whether that source file appears in `project/AT32_IDE/.project` as a linked resource, usually under a `user/<file>.c` name.
3. Check whether the same source appears in `project/AT32_IDE/<Config>/user/subdir.mk` in:
   - `C_SRCS`
   - `OBJS`
   - `C_DEPS`
   - a compile rule for `user/<file>.o`
4. If the source exists in `project/src` but not in `.project`, fix `.project` first. That is the root cause for IDE builds.
5. Patch generated `subdir.mk` only to keep the current configuration buildable immediately, and note that the IDE may overwrite it.

### If command-line `make` works but AT32_IDE still fails

- Assume the IDE regenerated its makefiles from stale metadata.
- Re-check `.project` linked resources.
- Tell the user to refresh, clean, and rebuild after metadata fixes.

### If headers compile in VS Code but not in AT32_IDE

- Verify include paths and defines in `.cproject`.
- Compare with a known-good AT32 project when needed.
- Avoid editing generated makefiles to solve missing include path issues unless it is clearly a temporary workaround.

## Fix Strategy

Prefer this order:

1. Fix missing linked resources in `.project`.
2. Fix configuration settings in `.cproject` if include paths, symbols, or linker options are wrong.
3. Update generated make fragments only when necessary for the current local build.
4. Rebuild and verify the ELF and HEX are produced.

## Expected Output

When the build succeeds, report:

- whether Debug or Release was built
- whether ELF and HEX were produced
- size output if available
- any metadata files that were corrected

When the build fails, report:

- the first meaningful compiler or linker error
- whether the cause is toolchain, source registration, include path, or linker config
- the exact file that should be fixed next

## Notes

- `project/AT32_IDE/.project` controls linked source membership for Eclipse-managed builds.
- `project/AT32_IDE/<Config>/user/subdir.mk` is generated and may be overwritten.
- For AT32 projects, missing `user/*.c` links in `.project` commonly explain linker errors where the function definition exists but the object is absent from the link line.
