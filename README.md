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

## Architecture Documentation
The Arc42 design document lives in [`docs/arc42.md`](docs/arc42.md). Consult it
before making significant changes so the architecture goals, scope, and
quality characteristics remain visible for every task.

## Pre-commit setup
Pre-commit checks are mandatory. Install the hooks and run them before every
commit.

### Linux (Ubuntu 20.04)
1. Create and activate a virtual environment:

   ```bash
   python3.8 -m venv .venv
   source .venv/bin/activate
   ```

2. Install dependencies and set up the git hook:

   ```bash
   pip install --upgrade pip pre-commit
   pre-commit install
   pre-commit install-hooks
   ```

3. Run the full suite on all files:

   ```bash
   pre-commit run --all-files
   ```

4. To enable clang-tidy, generate `build/compile_commands.json` once:

   ```bash
   cmake -S . -B build -G Ninja -DCMAKE_EXPORT_COMPILE_COMMANDS=ON
   ```

### Windows (PowerShell)
1. Create and activate a virtual environment:

   ```powershell
   py -3.8 -m venv .venv
   .\.venv\Scripts\Activate.ps1
   ```

2. Install dependencies and set up the git hook:

   ```powershell
   pip install --upgrade pip pre-commit
   pre-commit install
   pre-commit install-hooks
   ```

3. Run the full suite on all files:

   ```powershell
   pre-commit run --all-files
   ```

4. To enable clang-tidy, generate `build/compile_commands.json` once with your
   generator of choice (for example Ninja or Visual Studio) before running the
   hook.
