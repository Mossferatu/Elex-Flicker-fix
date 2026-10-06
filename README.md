# ELEX Cloud Flicker Fix

A small, native DirectX 11 mod that fixes rapid dark flickering caused by an
unstable cloud-lighting calculation in **ELEX**.

The affected scene flickered on an NVIDIA RTX 5090 with driver **617.14**.
The player reported that drivers newer than **581.80** triggered the issue.
This mod fixed the tested scene **without rolling back the driver**.
Not every intervening driver has been independently tested.

## What it changes

- Adds one missing thread-group execution barrier in the cloud-luminance shader.
- Preserves the shader's lighting, cloud and weather calculations.
- Uses Windows' native D3D11/DXGI. No DXVK or RenderDoc is needed.
- Does not modify saves, game configuration, drivers, the registry, HDR or G-SYNC.
- Does not add per-frame GPU waits or intercept presentation.

**No game shaders, extracted assets or game executables are included.**
The DLL transforms the game's own shader in memory after verifying its exact
fingerprint. Original game files are not overwritten.

## Requirements and compatibility

Verified configuration:

| Component | Tested version |
| --- | --- |
| Game | ELEX, Steam app 411300 |
| Steam build | 2617898 |
| Executable | 1.0.2981.0, change 427388, x64 |
| OS / graphics API | Windows, native DirectX 11 |
| GPU | NVIDIA GeForce RTX 5090 |
| NVIDIA driver | 617.14 |
| Mod | 0.1.0 |

Only the exact known shader is supported. Other game builds, GPUs and drivers
are **not verified**. This is not an ELEX II mod or a general NVIDIA driver fix.

Do not overwrite an existing `d3d11.dll` supplied by DXVK, ReShade or another
wrapper. Wrapper chaining is not implemented. This proxy exposes the two
device-creation exports needed by the tested ELEX path, not the entire D3D11 API.
No multiplayer or anti-cheat compatibility is claimed.

## Install

1. Close ELEX.
2. Extract the release ZIP.
3. Copy **`system\d3d11.dll`** into your game's **`ELEX\system`** folder,
   alongside `ELEX.exe`. Stop if a `d3d11.dll` already exists there; resolve
   the conflicting wrapper first rather than overwriting it.
4. Launch ELEX normally through Steam and load your save.
5. Check `ELEX\system\elex-cloud-fix.log` for:

   ```text
   APPLIED #1: cloud luminance shader; SHA256 verified; sync_g -> sync_g_t at byte 11576; native creation=0x00000000
   ```

A startup or "scanning fingerprints" line **does not** prove the correction
was applied. The `APPLIED` line must appear in the section for the current launch.
The log is appended across launches; each startup line includes a process ID.

Once installed, the mod loads automatically on subsequent launches.
No mod manager, additional loader or Python installation is required to play.

## Uninstall

Close ELEX and remove **only this mod's** `ELEX\system\d3d11.dll`.
You may also remove its `elex-cloud-fix.log`. There are no configuration or
save edits to undo.

## Troubleshooting

- **No log:** check that the DLL is beside `ELEX.exe`, not in the game root.
  Confirm the file was actually extracted and that the game can write there.
- **Log exists but no current `APPLIED`:** the target shader may not have been
  created yet, the game build may differ, or another wrapper may interfere.
  Load a save and recheck. Do not assume the fix is active.
- **Error dialog:** validation, hooking or native shader creation failed.
  Check the log; close ELEX and remove this mod if necessary.
- **Still flickering:** report the game build, GPU, driver, whether `APPLIED`
  appears, and how to reproduce it. Different flickering causes may need
  different fixes. Redact identifying information from any shared logs.
- **Permission error:** the mod needs permission to write its log beside the
  DLL. Resolve the folder permission issue rather than disabling system security.

## How the fix works

ELEX computes mean cloud scattering using a 256-thread shared-memory reduction.
The reduction loop contains a shared-memory barrier (`sync_g`) without a
thread-group execution barrier. On the tested system, the output alternated
between two lighting states and affected the world's lighting probes.

The proxy intercepts `ID3D11Device::CreateComputeShader`. For the known
11,904-byte shader only, it checks the container signature, complete SHA256
and expected instruction tokens, then adds flag `0x800` at byte offset `11576`:
`sync_g` becomes `sync_g_t`.

The DXBC checksum is replaced with its independently calculated corrected
value. The complete corrected blob must also match its expected SHA256 before
being passed to native D3D11. Unrelated shaders are passed through unchanged.
Fingerprints and checksum values are metadata, not embedded shader bytecode.

## Verification and limitations

- **Controlled GPU replay:** 64 baseline, 64 corrected, 64 restored replays of
  the same captured frame. Baseline and restoration reproduced the dark state;
  all corrected replays retained normal lighting. Corrected sampled RGB mean
  spread was below 0.0001 on a 0-255 scale.
- **Native tests:** hardware and WARP devices, repeated corrected shader
  creation, exact one-instruction-bit transformation, rejection of altered or
  truncated target input, and device/swap-chain forwarding.
- **Unrelated shader:** an original test shader still dispatched and produced
  its expected output, 42.
- **Live game:** the player confirmed that the flickering was gone.
- **Paused scene:** 620 captured frames showed stable world lighting.
- **Fresh launch:** the correction reapplied automatically and another
  620-frame gameplay recording showed no previous large brightness jumps.

These are tests of one affected scene and configuration, not exhaustive tests
of all weather, locations or drivers. No formal performance benchmark was run.
Windows Auto HDR was left unchanged; capture measurements have limitations
and were supplemented by the player's direct visual confirmation.

The shader fix was isolated after DXVK, configuration changes and broad GPU
synchronization workarounds failed. Those experiments are not part of this mod.

## Build from source

Use Windows PowerShell and [Zig](https://ziglang.org/download/).
The verified toolchain was official **Zig 0.17.0**, targeting
`x86_64-windows-gnu`. Other versions may work but are not verified.
The scripts do not download or install a compiler automatically.

From the repository root:

```powershell
.\build.ps1 -Zig 'C:\Tools\zig\zig.exe' -RunTests
```

If Zig is on `PATH`, omit `-Zig`. Compilation uses `-Wall -Wextra -Werror`,
optimization, and stripped release output. No proprietary SDK or game files
are required to build the DLL or run the public smoke tests.
Smoke tests require a functioning D3D11 hardware device, Windows WARP and
Windows' `d3dcompiler_47.dll`.

Outputs:

- `build\d3d11.dll`: compiled mod.
- `build\smoke.exe`: test executable when `-RunTests` is used.
- `dist\ELEX-Cloud-Flicker-Fix-0.1.0.zip`: install-ready release.
- `dist\SHA256SUMS.txt`: DLL and ZIP checksums. The DLL entry refers to the
  file inside the ZIP.

Build and distribution outputs are intentionally ignored by Git. Publish the
ZIP and checksum file as release attachments rather than committing binaries
or debug symbols to the source history. Do not run build scripts in an
installed game's `system` directory.

### Automated releases

Create and publish a GitHub release with a version tag such as **`v0.1.0`**
(or `0.1.0`). Tags must use `major.minor.patch`, optionally followed by a
hyphenated prerelease identifier such as `v0.2.0-beta.1`.
The **Build release distribution** workflow builds on Windows using pinned
**Zig 0.15.2**, verifies the compiler download's SHA256, and attaches the
versioned ZIP and `SHA256SUMS.txt` to that release. Wait for the workflow to
finish before downloading the attachments; draft releases do not trigger it.

You can also run the workflow manually from the Actions tab to download a
`release-dist` artifact without publishing a release. Manual builds use the
default package version, `0.1.0`. Locally, use `.\build.ps1 -Version 0.2.0`
to change the package version.

The hosted build does not run the smoke tests: they require a functioning
D3D11 hardware device, which hosted runners do not guarantee. Run
`.\build.ps1 -RunTests` on a suitable Windows machine before publishing;
successful CI packaging is not confirmation that the in-game fix works.

### Optional private shader regression test

The public test explicitly skips target-shader tests unless private shader
inputs are supplied. It still tests native device/swap-chain creation and an
unrelated compute dispatch. A passing public smoke test is not a flicker test.

Developers with a locally extracted copy of the supported shader can use
Python 3 to independently calculate its checksum and corrected form:

```powershell
python .\tools\patch_shader.py .\private\original.dxbc .\private\corrected.dxbc
.\build.ps1 -RunTests -OriginalShader .\private\original.dxbc -CorrectedShader .\private\corrected.dxbc
```

Supply the original shader yourself; extraction is not part of this repository.
The helper accepts only the verified fingerprint and refuses to overwrite an
existing output file. The full test additionally checks the exact transformation
and native creation of the corrected shader on hardware and WARP.

**Never commit or upload `.dxbc` files, RenderDoc captures, shader disassembly,
game backups or extracted textures.** The ignore rules are an extra safeguard,
not permission to redistribute game content.

## License and credits

Original mod source and tools are available under the **MIT License**; see
`LICENSE`. ELEX and NVIDIA names belong to their respective owners.
This is an unofficial community fix, not affiliated with or endorsed by them.

Created with assistance from an **AI assistant using Copilot SDK in VS Code**.
The player supplied the affected hardware, reproducible scene and direct
in-game confirmation. No generated artwork or audio is included.

Tools and technical references:

- [RenderDoc](https://renderdoc.org/), used locally to isolate and replay the fault.
- [Zig](https://ziglang.org/), used to build the native DLL and tests.
- [Microsoft shader synchronization instruction documentation](https://learn.microsoft.com/en-us/windows/win32/direct3dhlsl/sync--sm5---asm-).
- [RenderDoc's DXBC container implementation](https://github.com/baldurk/renderdoc/blob/v1.46/renderdoc/driver/shaders/dxbc/dxbc_container.cpp),
  used as a reference for the DXBC checksum padding format.

These tools are not bundled. The checksum helper is an original implementation
of the MD5 compression algorithm with DXBC-specific final padding.
