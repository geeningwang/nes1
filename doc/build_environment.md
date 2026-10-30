# Setting up a Windows build environment for nes1

How to go from a clean Windows machine (Windows 10/11 or Windows Server 2019/2022, x64) to a built and tested `nes1.exe`. All steps can be done from an elevated PowerShell prompt, so they work over RDP, SSH, or a startup script.

Requirements: about 10 GB of free disk space, internet access, and an administrator account.

## 1. Install the toolchain

nes1 only needs the MSVC v143 compiler (Visual Studio 2022) and the Windows SDK. XAudio2 ships with the Windows SDK, so there are no other dependencies. Full Visual Studio is not required; the Build Tools are enough.

```powershell
$ProgressPreference = 'SilentlyContinue'
[Net.ServicePointManager]::SecurityProtocol = [Net.SecurityProtocolType]::Tls12
New-Item -ItemType Directory -Force C:\work | Out-Null

Invoke-WebRequest -Uri 'https://aka.ms/vs/17/release/vs_BuildTools.exe' -OutFile C:\work\vs_BuildTools.exe

$p = Start-Process C:\work\vs_BuildTools.exe -Wait -PassThru -ArgumentList `
  '--quiet','--wait','--norestart','--nocache',
  '--add','Microsoft.VisualStudio.Workload.VCTools',
  '--add','Microsoft.VisualStudio.Component.Windows11SDK.22621',
  '--includeRecommended'
$p.ExitCode   # 0 = success, 3010 = success but reboot required
```

The install takes roughly 5-10 minutes. Exit code `3010` is fine; reboot before building if you get it.

If Visual Studio 2022 (Community or higher) is already installed, make sure the "Desktop development with C++" workload and a Windows 10/11 SDK are selected instead.

## 2. Get the source

With git:

```powershell
git clone https://github.com/geeningwang/nes1 C:\work\nes1
```

Without git:

```powershell
Invoke-WebRequest -Uri 'https://github.com/geeningwang/nes1/archive/refs/heads/main.zip' -OutFile C:\work\nes1.zip
Expand-Archive C:\work\nes1.zip -DestinationPath C:\work -Force
Rename-Item C:\work\nes1-main C:\work\nes1
```

The rest of this document assumes the source is in `C:\work\nes1`.

## 3. Build

Command line (no Visual Studio UI needed):

```powershell
$msbuild = & "${env:ProgramFiles(x86)}\Microsoft Visual Studio\Installer\vswhere.exe" `
  -latest -products * -requires Microsoft.Component.MSBuild `
  -find 'MSBuild\**\Bin\MSBuild.exe' | Select-Object -First 1

& $msbuild C:\work\nes1\src\nes1.sln /p:Configuration=Debug /p:Platform=x64 /m /nologo /v:m
```

On success the last lines look like `nes1.vcxproj -> C:\work\nes1\src\x64\Debug\nes1.exe`, and `$LASTEXITCODE` is `0`.

Notes:

- The project uses `PlatformToolset` v143 and `WindowsTargetPlatformVersion` 10.0, so VS 2022 Build Tools and any Windows 10/11 SDK work.
- Use the **x64** platform; the project is configured for x64.
- Build output goes to `src\x64\Debug\` (gitignored).
- Alternatively, open `src\nes1.sln` in Visual Studio and build **x64 | Debug**.

## 4. Verify with the headless test modes

The headless modes need no display, so they work on a server or a non-interactive session. Run them from any directory; paths are passed on the command line.

```powershell
$exe = 'C:\work\nes1\src\x64\Debug\nes1.exe'

# CPU test ROM: 300 frames, snapshot every 30 frames -> 10 .bmp + 10 .txt
New-Item -ItemType Directory -Force C:\work\out_nestest | Out-Null
& $exe 'C:\work\nes1\test\nestest.nes' --frametest C:\work\out_nestest 300 30

# Real game ROM: 600 frames, snapshot every 60 frames
New-Item -ItemType Directory -Force C:\work\out_mappy | Out-Null
& $exe 'C:\work\nes1\test\mappy\Mappy (Japan).nes' --frametest C:\work\out_mappy 600 60
```

Expected: the run prints `Frametest done: 10 screenshots saved to <outdir>`, exits with code 0, and takes only a few seconds. Each snapshot is a `nes1_frame_NNNN.bmp` plus a `nes1_frame_NNNN.txt` containing CPU/PPU register state.

Common failure: `error in opening file.` means the ROM path is wrong. The path contains spaces and parentheses, so quote it. The Mappy ROM location depends on the revision; check with `Get-ChildItem C:\work\nes1\test -Recurse -Filter *.nes`.

Other headless modes (see the main `readme.md`): `--autotest <outdir>` and `--scanlinetrace <outpath> <frame>`.

To compare snapshots against FCEUX reference output, install FCEUX (step 6) and see [how_to_dense_report.md](how_to_dense_report.md). For whole-machine state dumps from both emulators, see [state_dump.md](state_dump.md) and step 7.

## 5. Run interactively

Interactive mode opens a Win32 window, so it needs a desktop session (for example RDP or the console), not a service or SSH session:

```powershell
& 'C:\work\nes1\src\x64\Debug\nes1.exe' 'C:\work\nes1\test\mappy\Mappy (Japan).nes'
```

Controls: arrow keys = D-pad, `F` = A, `D` = B, `S` = Select, `Enter` = Start. Audio uses XAudio2, so the machine needs an audio device or the Windows Audio service running (on cloud VMs without a sound device the window still works, but audio output may be unavailable).

## 6. Install FCEUX (reference emulator for comparison)

The comparison scripts under `test\` generate reference frames with FCEUX 2.6.6 (64-bit, `fceux64.exe`). The official prebuilt Windows zip is enough; no installer or build is needed.

```powershell
$dir = 'C:\work\fceux-2.6.6'
Invoke-WebRequest -Uri 'https://github.com/TASEmulators/fceux/releases/download/v2.6.6/fceux-2.6.6-win64.zip' -OutFile C:\work\fceux.zip
Expand-Archive C:\work\fceux.zip -DestinationPath $dir -Force

# Put fceux64.exe on the machine PATH (open a new shell afterwards)
$mp = [Environment]::GetEnvironmentVariable('Path','Machine')
if ($mp -notlike "*$dir*") { [Environment]::SetEnvironmentVariable('Path', "$mp;$dir", 'Machine') }

Test-Path "$dir\fceux64.exe"   # True
```

The zip is about 4.7 MB and contains `fceux64.exe`, `lua5.1.dll`, `lua51.dll`, `7z_64.dll`, and the `luaScripts`, `palettes` and `tools` folders. Do not use the `win64-QtSDL` zip (80 MB); the scripts target the standard build.

Running FCEUX with a Lua script (this is how the reference frames are produced):

```powershell
fceux64.exe -lua "C:\work\nes1\test\mappy\frame_level\mappy_fceux_dense.lua" "C:\work\nes1\test\mappy\Mappy (Japan).nes"
```

Notes:

- **FCEUX needs a desktop session.** It is a GUI program. In a non-interactive session (service, scheduled task with no logged-on user, startup script) it starts and stays running but does not execute the Lua script and produces no output. Run it from an RDP or console session, and use `Start-Process ... -PassThru` with a timeout if you script it.
- **Paths in the repo scripts.** Several scripts and Lua files hardcode `C:\Work\nes1\...` (Windows paths are case-insensitive, so `C:\work\nes1` matches) and `run_fceux.ps1` hardcodes `C:\Work\fceux-2.6.6\vc\x64\Release\fceux64.exe`. Either keep the source at `C:\work\nes1`, or edit the paths at the top of those scripts. If the FCEUX path in `run_fceux.ps1` does not match, copy `fceux64.exe` and its DLLs to that folder or edit the script.
- Only the download, extraction, PATH change and process launch were verified on a clean machine; running a Lua script needs the interactive session described above.

## 7. Build the patched FCEUX (headless state dumps)

The stock FCEUX from step 6 cannot run without a desktop. For automated nes1-vs-FCEUX comparison the repo carries a patched FCEUX 2.6.6 under `tool\fceux-2.6.6` that adds a headless `--dump` mode (see [state_dump.md](state_dump.md)). Build it from source with the same Build Tools:

```powershell
$msbuild = & "${env:ProgramFiles(x86)}\Microsoft Visual Studio\Installer\vswhere.exe" `
  -latest -products * -requires Microsoft.Component.MSBuild -find 'MSBuild\**\Bin\MSBuild.exe' | Select-Object -First 1

& $msbuild C:\work\nes1\tool\fceux-2.6.6\vc\vc14_fceux.vcxproj `
  /p:Configuration=Release /p:Platform=x64 /p:PlatformToolset=v143 /t:Build /m /nologo /v:minimal
```

The output is `tool\fceux-2.6.6\vc\x64\Release\fceux64.exe`. A first build takes a few minutes. The `PlatformToolset=v143` override is needed because the project targets VS 2019 by default; the compiler warnings it prints are pre-existing.

**`scmrev.h` not found.** A pre-build step runs `vc\defaultconfig\make_scmrev.h.js`, which needs `git` on the PATH to generate `vc\userconfig\scmrev.h`. If git is not installed the step fails silently and compilation stops with `C1083: Cannot open include file: 'scmrev.h'`. Either install git, or create the file by hand:

```powershell
Set-Content C:\work\nes1\tool\fceux-2.6.6\vc\userconfig\scmrev.h @'
#define SCM_REV_STR "local"
#define SCM_DESC_STR "2.6.6-nes1"
#define SCM_BRANCH_STR "local"
#define SCM_IS_MASTER 0
'@ -Encoding ascii
```

The file is a build artifact; do not commit it.

Smoke test the build (no window or desktop is needed; this works from SSH, a service or a startup script):

```powershell
$rom = 'C:\work\nes1\test\mappy\Mappy (Japan).nes'
$p = Start-Process C:\work\nes1\tool\fceux-2.6.6\vc\x64\Release\fceux64.exe `
       -ArgumentList "`"$rom`"",'--dump','C:\work\out_fceux','--frames','10','--dump-frames','1-10' -Wait -PassThru -NoNewWindow
$p.ExitCode                            # 0
Get-ChildItem C:\work\out_fceux      # 10 frame_f*.txt files + rom.txt, events.txt, run.txt
```

FCEUX prints `fceux dump done: 10 frames` and exits after a few seconds. Launch it with `Start-Process -Wait` as above so the script waits for it and gets its exit code.

## 8. Install Python (for the comparison tool)

`tool\compare_dumps.py` compares two dump folders. It uses only the Python 3 standard library, so the portable "embeddable" Python is enough: no installer, no pip, no PATH change.

```powershell
$ProgressPreference = 'SilentlyContinue'
[Net.ServicePointManager]::SecurityProtocol = [Net.SecurityProtocolType]::Tls12
Invoke-WebRequest -Uri 'https://www.python.org/ftp/python/3.12.8/python-3.12.8-embed-amd64.zip' -OutFile C:\work\python.zip
Expand-Archive C:\work\python.zip -DestinationPath C:\work\python -Force
C:\work\python\python.exe --version     # Python 3.12.8
```

If a full Python 3 is already installed and on the PATH, use that instead.

## 9. Run both emulators and compare

`tool\run_compare.ps1` runs nes1 and the patched FCEUX with identical `--dump` arguments and then runs the comparison, all on the same machine, so the output folders never need to be copied anywhere:

```powershell
cd C:\work\nes1
powershell -ExecutionPolicy Bypass -File tool\run_compare.ps1 `
    -Rom "test\mappy\Mappy (Japan).nes" -Tag mappy `
    -DumpArgs "--frames 120 --dump-frames 1-120 --dump-scanline-frames 5 --dump-pc C06F --pc-max 5"
```

- Output goes to `C:\work\<Tag>\nes1` and `C:\work\<Tag>\fceux` (change the root with `-OutRoot`). Existing folders for that tag are replaced.
- It finds `nes1.exe` at `src\x64\Debug`, `fceux64.exe` at `tool\fceux-2.6.6\vc\x64\Release`, and Python on the PATH or at `C:\work\python\python.exe`. Override with `-Nes1`, `-Fceux`, `-Python`.
- `-DumpArgs` is split on spaces, so option values must not contain spaces. The options are listed in [state_dump.md](state_dump.md).
- The report is printed to the console: the first diverging event and a per-field summary. Use `-Max N` to also print the first N differing events in detail.
- The exit code is 0 if the dumps are identical, 1 if they differ, and 2 if an emulator or a path failed.

To run the steps by hand, or to compare folders from earlier runs:

```powershell
C:\work\python\python.exe C:\work\nes1\tool\compare_dumps.py C:\work\mappy\nes1 C:\work\mappy\fceux --summary
```

## Troubleshooting

| Symptom | Cause / fix |
|---------|-------------|
| `MSB8020: build tools for v143 cannot be found` | The VC++ toolset is not installed; rerun step 1 with the `VCTools` workload. |
| `MSB8036: Windows SDK not found` | Install a Windows SDK component (step 1) or retarget the solution to the installed SDK version. |
| `vswhere` returns nothing | Build Tools are not installed or need a reboot (installer exit code 3010). |
| `error in opening file.` | ROM path wrong or not quoted. |
| Frametest produces 0 files | The ROM failed to load; see above. The exit code is still 0, so check the output folder. |
| FCEUX launches but the Lua script never runs / no output | No interactive desktop session; run it from RDP or the console (step 6). |
| `C1083: Cannot open include file: 'scmrev.h'` building FCEUX | git is not on the PATH; create `scmrev.h` by hand (step 7). |
| `run_compare.ps1`: `not found: ...fceux64.exe` / `nes1.exe` / `python.exe` | Build nes1 (step 3), build FCEUX (step 7) or install Python (step 8), or pass the path with `-Nes1`, `-Fceux`, `-Python`. |
