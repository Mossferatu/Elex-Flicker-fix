# ELEX Cloud Flicker Fix for DXVK

This directory preserves a narrowly targeted port of the native mod's shader
correction into DXVK. It builds and passes shader regression tests. **The
player reported that the packaged DXVK patch works on a subsequent retest on
2026-10-08.** Earlier tests were inconsistent; their results remain below.
The precise reason for that difference has not been established, and this is
not a claim of compatibility with every driver, scene or ReShade setup.

The release workflow builds `ELEX-Cloud-Flicker-Fix-DXVK-<version>.zip` and
`DXVK-SHA256SUMS.txt`, separately from the native fix. No game shaders are
included. Nothing has been submitted upstream.

## Installation and removal

1. Close ELEX and back up any existing `d3d11.dll`, `dxgi.dll` and `dxvk.conf`
   from `ELEX\system` outside the game. Record which files were absent.
2. Extract the DXVK ZIP and copy its `system` contents beside `ELEX.exe`.
   **Do not combine this with the native fix or another D3D11/DXGI wrapper.**
3. Launch ELEX and load the affected scene. `ELEX_d3d11.log` should contain
   `Applied ELEX cloud reduction barrier fix (sync_g -> sync_g_t)`.
   The activation line alone is not proof of visual success.

The included configuration sets `d3d11.forceComputeLdsBarriers = False`; that
broad workaround is not needed to apply the targeted correction. Confirm the
effective configuration in `ELEX_dxgi.log`. If the game is launched with a
different working directory, set the process-local `DXVK_CONFIG_FILE` variable
to the absolute path of the installed `dxvk.conf`.

To uninstall, close ELEX, remove only the three installed files and restore
your backed-up files. Leave originally absent files absent. Restoring the native
mod's `d3d11.dll` returns to the native renderer.

ReShade wrapper chaining is not supported. ReShade may also load via its global
Vulkan layer independently of local DLLs. Our isolated tests used process-local
`DISABLE_VK_LAYER_reshade_1=1`, verified against ReShade 6.8.0.2155; the successful
later user retest did not include a new module audit. No general ReShade
compatibility claim is made.

## Target source

- DXVK tag: `v3.1.1`.
- DXVK commit: `b1a1c99ab52b687cf950d62c88bc2fa316b41663`.
- Its pinned `dxbc-spirv` submodule:
  `bf14419e5fa7eacb817b7b632f03cb61d61bbad7`.
- DXVK and its submodules retain their own licenses. DXVK's license is included
  as `DXVK-LICENSE`. The original modifications and regression test here are
  covered by this repository's MIT license.

## What the patch does

`elex-cloud-barrier.patch` adds a compute-shader transformation in
`D3D11Device::CreateComputeShader`:

1. Check the known DXBC signature and 11,904-byte length.
2. Validate the complete original bytecode using DXVK's existing SHA1 helper.
   The native mod uses SHA256; this port reuses DXVK's cross-platform helper
   rather than adding a Windows BCrypt dependency.
3. Check the three synchronization tokens and change only `sync_g` at byte
   offset 11576 to `sync_g_t` by adding bit `0x800`.
4. Update the precomputed DXBC checksum and verify the complete corrected SHA1.
5. Pass the corrected bytecode to normal DXVK validation and compilation.

The transformation happens **before** cache-key generation, separating the
corrected shader from the old cache entry:

```text
Original:  cs.0ee378a937d4b0aff96aa1317f12433a
Corrected: cs.99c09ae66c92a6ff58b086d902bceb5d
```

There is no executable-name gate: the complete known shader fingerprint gates
the change. Unrelated shaders and already-corrected shaders are unchanged.
An apparent target with a mismatched full fingerprint is explicitly rejected.
No embedded game shader, changed shader math, global barrier setting, per-frame
wait, or presentation hook is added.

## Observed results, 2026-10-08

Test setup: ELEX Steam build 2617898, RTX 5090, NVIDIA driver 617.42.
The general option `d3d11.forceComputeLdsBarriers` was **False**.
ReShade's global Vulkan layer was disabled for each launch using its declared
process-local opt-out, `DISABLE_VK_LAYER_reshade_1=1`, and absence of ReShade was
checked in the live module list.

| Check | Result |
| --- | --- |
| Windows x64 DXVK build | Passed |
| Output versus independently corrected DXBC | Byte-for-byte identical |
| Instruction difference | Exactly one bit, apart from container checksum |
| Original/corrected DXBC checksum validation | Passed |
| Corrupted/truncated target rejection | Passed |
| Unrelated input and repeated application | Passed |
| Original/corrected DXVK cache keys | Distinct |
| Corrected shader creation through DXVK | Passed |
| Unrelated compute dispatch | Expected output, 42 |
| Dumped Vulkan shader | All three workgroup execution barriers present |
| Normal gameplay, first test | Flicker remained |
| Same build with RenderDoc attached | User reported flicker gone |
| Same build without RenderDoc, repeated | Flicker returned |
| Later player retest of the packaged DXVK DLLs | Player confirmed the patch works |

The offline SPIR-V dump contained three `OpControlBarrier` instructions with
workgroup execution and memory scopes. Therefore, the simple hypothesis that
DXVK discarded the added execution barrier is not supported by that dump.

RenderDoc's overlay identified the D3D11 capture layer while DXVK/Vulkan was
active. No frame capture was taken during this experiment, so there is **no
captured Vulkan-frame analysis** to attribute a second shader or driver bug.
Instrumentation changed the observed behavior; the remaining cause is unknown.

The user's working native release was restored after testing. An earlier,
broader `forceComputeLdsBarriers=True` experiment also gave inconsistent results
and is not a confirmed fix.

## Automated build and packaging

The repository's release workflow builds both the native and DXVK packages.
DXVK is cloned at the exact commit listed above, its pinned submodules are
initialized, and the patch is applied before compilation. It does not copy
prebuilt DLLs out of the ignored `dist` directory.

With the tools listed below installed, run from the repository root:

```powershell
.\dxvk\build.ps1 -ToolchainDirectory 'C:\Tools\llvm-mingw-20261006-ucrt-x86_64' `
  -GlslangDirectory 'C:\Tools\glslang' -Version 0.1.0
```

Meson and Ninja must be on `PATH`. This script clones source but does not install
tools. The workflow installs pinned tools separately and verifies archive
SHA256 values. An existing pinned checkout may be supplied with
`-SourceDirectory`; use a dedicated checkout, not a game installation.
Outputs in `dist` are the versioned DXVK ZIP and `DXVK-SHA256SUMS.txt`.
The ZIP includes `system\d3d11.dll`, `system\dxgi.dll`, `system\dxvk.conf`,
this README, the source patch, DLL hashes and upstream license notices.
CI checks compilation and x64 PE headers, not visual correctness on a GPU.

## Reproduce the source build manually

Use a separate DXVK checkout, not the installed game's directory:

```powershell
git clone --branch v3.1.1 --recurse-submodules https://github.com/doitsujin/dxvk.git dxvk
Set-Location .\dxvk
git apply --check <path-to-elex-cloud-barrier.patch>
git apply <path-to-elex-cloud-barrier.patch>
```

The build was tested on Windows using:

- LLVM-MinGW `20261006`, UCRT x86_64, Clang `23.1.3`.
- Meson `1.12.1`.
- Ninja `1.13.2`.
- glslang `16.6.0`.

With their executables on the current process's `PATH`:

```powershell
$env:CC = 'clang'
$env:CXX = 'clang++'
meson setup build-elex --buildtype=release -Denable_d3d8=false -Denable_d3d9=false
meson compile -C build-elex -j 6
```

The upstream build emitted compiler warnings but completed successfully.
Outputs are `build-elex\src\d3d11\d3d11.dll` and
`build-elex\src\dxgi\dxgi.dll`. Do not install these over another wrapper without
backing it up. They are not drop-in companions to the native mod's `d3d11.dll`.

## Optional regression test with private inputs

`regression.cpp` uses DXVK's own parser, checksum validation and cache-key code.
From the patched DXVK checkout, compile it against the freshly built libraries:

```powershell
clang++ -std=c++17 -O2 -static -DNOMINMAX -D_WIN32_WINNT=0xa00 `
  -I. -Iinclude -Iinclude\vulkan\include -Iinclude\spirv\include `
  -Isubprojects\dxbc-spirv <path-to-regression.cpp> `
  src\dxvk\dxvk_shader_key.cpp build-elex\src\util\libutil.a `
  build-elex\subprojects\dxbc-spirv\libdxbc_spv.a -o regression.exe
.\regression.exe <private-original.dxbc> <private-corrected.dxbc>
```

Private inputs must come from the user's own game and remain outside the repo.
The corrected reference may be generated by the repository's
`tools\patch_shader.py`. Do not upload shader bytecode, SPIR-V dumps, captures,
game assets, machine-specific logs, or compiler distributions.

## Remaining validation

Broader testing should establish which conditions explain the earlier flicker
and the successful later retest. Keep exact-shader matching, cache separation,
and uninstrumented gameplay checks. Do not infer success solely from a patch
activation log or behavior observed with capture instrumentation attached.
