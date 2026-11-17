# Release notes

## QtProbe 1.0.0

Changes since QtProbe 0.1.0.

- When compatible SDDS libraries are available at build
  time, the File menu and export button can save the selected PV's retained
  scalar history, current array, or current snapshot as an SDDS file.
- For Channel Access arrays above the retention limit, QtProbe
  requests only the retained prefix and flags possible truncation when the
  response fills that request. QtProbe rejects pvAccess updates that require more
  than 8 MiB of copied text, clears the displayed value, and disables writes
  until a valid update arrives.
- QtProbe limits saved and opened session files to 4 MiB and
  rejects session files that list more than 100 PVs.
- The shared Makefile rules have been synchronized with the
  companion applications. The macOS build avoids linking the math library
  twice.

[QtProbe 0.1.0 on GitHub](https://github.com/rtsoliday/probe/releases/tag/v0.1.0)
