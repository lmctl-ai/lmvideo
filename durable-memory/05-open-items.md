# Open items

- Review the release license bundle (`scripts/stage-release-notices.sh`) whenever the Skia pack is re-pinned; ICU, Expat and libjpeg-turbo texts are kept in `licenses/` because the pack does not carry them.
- Consider an in-process libav backend only if avoiding the FFmpeg process becomes a real requirement; it would add headers, ABI choices, and more license inventory.
- Windows: the Skia pack exists (`windows-Release-x64`) but the Makefile, static linking and CI are Linux/macOS only.
- In-process FFmpeg (libav) backend: see 06-ffmpeg-in-process-feasibility.md for the plan, costs and the H.264/LGPL decisions.
- Extend SVG support only in response to a concrete document that the current Skia SVG DOM cannot render.
- A Rust host remains a separate project; it should reuse the schema and runtime conformance contract rather than duplicate native policy.
