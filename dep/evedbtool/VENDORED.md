# EVEDBTool in EVEmu

This is a copy of https://github.com/EvEmu-Project/EVEDBTool at tag 0.0.6 (commit 2ab3897). It's kept here so that
EVEmu builds its own database tool instead of downloading a release binary.

## Changes from upstream

- `go.mod` declares `go 1.19`, not `go 1.13`, so that the vendored modules compile. 1.19 is Debian 12's Go,
  as used in the Docker build. `go mod tidy` also re-sorted the `require` blocks and trimmed `go.sum`.
  The dependency versions are unchanged.
- All modules are vendored in `vendor/`, so building needs no network.
- The upstream `.vscode/`, `.gitignore` and `bin/` were not copied.

## Building

CMake builds it with the rest of EVEmu: the target is `evedbtool`, and the binary goes to
`<build>/dep/evedbtool/bin/evedbtool`, then to `<prefix>/bin/evedbtool` on install. It needs Go 1.19 or later.
Without Go the target is skipped. The `Makefile` is upstream's and still works for cross-compiling
(`make linux-arm64`, `make windows-amd64`).

## Updating

Copy the new upstream sources over these, keep `go 1.19` or later in `go.mod`, then run
`go mod tidy && go mod vendor`, rebuild, and run `evedbtool install` against a throwaway database.
