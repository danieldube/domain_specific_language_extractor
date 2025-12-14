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
commit. Use the platform-specific dependency script before installing the
hooks so the cmake step can locate clang/llvm tooling and libclang headers.

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
