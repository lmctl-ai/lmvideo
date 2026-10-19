# Open items

- Collect and ship the missing upstream notices for every linked Skia-pack library before distributing a compiled binary. The current source-only preparation records the gap in `THIRD_PARTY_NOTICES.md`.
- Consider an in-process libav backend only if avoiding the FFmpeg process becomes a real requirement; it would add headers, ABI choices, and more license inventory.
- Add non-Linux Skia archive wiring when there is a supported host and a reproducible validation path.
- Extend SVG support only in response to a concrete document that the current Skia SVG DOM cannot render.
- A Rust host remains a separate project; it should reuse the schema and runtime conformance contract rather than duplicate native policy.
