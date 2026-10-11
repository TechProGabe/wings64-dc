# ROM provenance

The port is built from the North American release only; the decomp
(gcsmith/Pilotwings64Decomp) matches that version at 100% of code bytes.

| field | value |
|---|---|
| title (header) | `Pilot Wings64` |
| game id / region | `NPWE` (USA) |
| format | big-endian `.z64` |
| size | 8,388,608 bytes (8 MiB) |
| SHA-1 | `ec771aedf54ee1b214c25404fb4ec51cfd43191a` |
| MD5 | `8b346182730ceaffe5e2ccf6d223c5ef` |

The decomp expects the file at its repo root as `baserom.us.z64` and checks
the SHA-1 above. This repo expects it at the project root as `baserom.z64`
(gitignored). It is never committed, never copied under a tracked path, and
its on-disk location is never written into a tracked file.
