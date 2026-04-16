# Commit Guidelines

Living document for this repository. We keep adding to it as we encounter new
situations. Last updated: 2026-08-26.

## Message format

```
[<Area>]- <Imperative summary>
```

- **Tag** in square brackets, capitalized, one of the standard tags below.
- **Separator** is exactly `]- ` (bracket, hyphen, space).
- **Summary**: imperative mood ("Add", "Fix", "Remove" — not "Added",
  "Fixes"), no trailing period, ideally ≤ 72 characters.
- Body (optional): blank line after the subject, then wrapped prose explaining
  *why* if the diff doesn't make it obvious. Not required for small changes.

Examples:

```
[Compiler]- Implement next-use register allocation
[Sim]- Validate energy report regex against real Avrora output
[Docs]- Update README for spec v2 milestone order
```

## Standard tags

| Tag | Scope |
|-----|-------|
| `[Docs]` | README, spec, REPORT.md, SOURCES.md, this folder |
| `[Compiler]` | Anything under `compiler/` |
| `[Sim]` | Anything under `sim/` |
| `[Export]` | Anything under `export/` or `models/` |
| `[Build]` | CMake files, .gitignore, toolchain config |
| `[Test]` | Test-only changes (if mixed with source, use the source's tag) |
| `[Cost]` | `cost_table.toml` / `SOURCES.md` cost entries |
| `[Spec]` | Changes to `energy_aware_compiler_spec_v2.md` |

If a change spans areas, split it into multiple commits where practical;
otherwise pick the dominant area.

## Staging discipline

- One logical change per commit. Never `git add .` blindly — review
  `git status` and stage intentionally.
- Build artifacts (`compiler/build/`, `*.o`, `*.elf`) never get committed;
  they are gitignored.
- Generated outputs (emitted `.s`, ELF files, Avrora reports) stay out of git
  unless checked in deliberately as test fixtures, and then only under a
  clearly named fixtures directory.

## Rules

- Never amend a pushed commit. Fix forward with a new commit.
- Never skip hooks (`--no-verify`) without an explicit reason recorded in the
  commit body.
- No secrets, keys, tokens, or absolute local paths in commits or diffs.
- Every energy cost number committed to `cost_table.toml` must carry its
  source citation in the same commit (spec §3 hard constraint).

## Log

| Date | Addition |
|------|----------|
| 2026-08-26 | Initial guidelines written, based on existing `[Docs]` commit style. |
