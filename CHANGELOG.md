# Changelog

## Unreleased

- Build and attach a separate patched DXVK distribution in the release workflow,
  including x64 D3D11/DXGI DLLs, configuration, licenses and separate checksums.
- Pin DXVK source and Windows build tools; verify compiler download hashes.
- Record the player's successful DXVK retest on 2026-10-08 while preserving
  earlier inconsistent test observations.

## 0.1.0 - 2026-10-06

- Initial cloud-luminance execution-barrier correction for the verified ELEX
  shader, with full original/corrected SHA256 checks.
- Native D3D11 device-creation proxy with activation and error logging.
- Windows build script, public smoke tests, optional private-shader regression
  tests, and an install-ready release archive.
- Verified on ELEX Steam build 2617898 with RTX 5090 and NVIDIA driver 617.14.
- Released source under the MIT License; no game shader bytecode is included.
