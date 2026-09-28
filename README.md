# onitama-tb

Endgame tablebase generator for Onitama (work in progress).

## Build

Requires Bazel 9.2+, Clang 23 with libc++, and `lld-23`.

```bash
bazel build //tb:onitama_tb --config=dbg
bazel run //tb:onitama_tb --config=rel
```

Optional: install `tools/bazel` as a wrapper (see [on3tama](https://github.com)) to default `--config=dbg`.

Compile commands for clangd:

```bash
bazel run @hedron_compile_commands//:refresh_all
```

## Layout

- `onitama_tb/` — C++26 modules (`tb:tablebase` holds the generator logic from the former `TableBase.hpp`).
