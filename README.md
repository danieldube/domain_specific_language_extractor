# Domain-Specific-Language-Extractor
This program extracts a domain specific language from C++ source code.

## Conventions
- Filenames use lowercase snake_case to keep includes predictable and tooling-friendly.

## DSL Representation
The analyzer emits its findings as a human-readable Markdown report (with an
optional JSON mirror) following the structure documented in
[`docs/dsl_representation.md`](docs/dsl_representation.md). The report
captures canonical terms, relationships between them, common workflows, and
incoherence warnings so developers can quickly understand and refine the
domain language used across the codebase.

## CLI usage
The CLI entrypoint is `dsl-extract`. The primary command is `analyze`, and it
defaults to that behavior when no subcommand is provided:

```
dsl-extract analyze --root <path> [--build <dir>] [--format markdown,json] \
  [--out <dir>] [--scope-notes <text>]
```

- `--out` directs report outputs to a specific directory; omit it to keep the
  legacy behavior of writing under the analysis root.
- `--format` accepts a comma-separated list (supported: `markdown`, `json`),
  defaulting to Markdown only when unspecified.
- Placeholder commands (`report`, `cache clean`) are reserved for future
  extensions and currently emit guidance via the CLI help text.
- Exit codes: `0` when analysis finishes without coherence findings, `2` when
  incoherence findings are present, and `1` for fatal errors such as missing
  inputs or unsupported commands.

## Architecture Documentation
The Arc42 design document lives in [`docs/arc42.md`](docs/arc42.md). Consult it
before making significant changes so the architecture goals, scope, and
quality characteristics remain visible for every task.

## Pre-commit and test gate (mandatory)
All commits must pass the local checks before they land in the repository. The
expected flow is:

1. Install toolchain dependencies so the CMake configure step can find
   `libclang` and related LLVM tools.
2. Run the pre-commit hooks on the full tree.
3. Build and run the unit tests.

Skipping any of these steps causes the most common failures on CI (for example,
`cmake -S . -B build -G Ninja -DCMAKE_BUILD_TYPE=Debug` will fail without the
`libclang-dev` headers installed). Use the platform-specific dependency script
before installing the hooks so the configure step always succeeds.

### Linux and macOS
1. Install system dependencies (clang/clang-tidy/clang-format, libclang
   headers, cmake, and ninja):

   ```bash
   ./scripts/install_dev_dependencies.sh
   ```

2. Create and activate a virtual environment:

   ```bash
   python3 -m venv .venv
   source .venv/bin/activate
   ```

3. Install Python tooling and set up the git hook:

   ```bash
   pip install --upgrade pip pre-commit
   pre-commit install
   ```

4. Run the full suite on all files:

   ```bash
   pre-commit run --all-files
   ```

5. Build and execute the test suite:

   ```bash
   cmake -S . -B build -G Ninja -DCMAKE_BUILD_TYPE=Debug
   cmake --build build -j
   ctest --test-dir build --output-on-failure -j
   ```

### Windows (PowerShell)
1. Install system dependencies:

   ```powershell
   ./scripts/install_dev_dependencies.ps1
   ```

2. Create and activate a virtual environment:

   ```powershell
   py -3 -m venv .venv
   .\.venv\Scripts\Activate.ps1
   ```

3. Install Python tooling and set up the git hook:

   ```powershell
   pip install --upgrade pip pre-commit
   pre-commit install
   ```

4. Run the full suite on all files:

   ```powershell
   pre-commit run --all-files
   ```

5. Build and execute the test suite:

   ```powershell
   cmake -S . -B build -G Ninja -DCMAKE_BUILD_TYPE=Debug
   cmake --build build -j
   ctest --test-dir build --output-on-failure -j
   ```
