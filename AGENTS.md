# AGENTS.md

A universal operating agreement for code and teams. Binding in all repositories. Non‑compliance is a defect.

## 1. Purpose

Deliver maintainable systems with minimal surprise. Optimize for readability, changeability, and verified correctness.

## 2. Scope

Applies to all code, tests, scripts, data definitions, pipelines, and configuration. Language‑agnostic. Prefer explicit policy over individual preference.

## 3. Non‑Negotiables

* **Clean Code by Robert C. Martin** is normative. When in doubt, choose the option that yields the clearest, smallest, and most intention‑revealing code.
* **Everything is tested.** No untested logic. Tests are version‑controlled assets.
* **Modern design first.** Prefer composable, decoupled, testable designs over inheritance‑heavy or framework‑entangled designs.
* **Automated enforcement.** Linters, formatters, type checkers, and CI gates are mandatory.
* **Pre‑commit usage is mandatory.** All commits must pass pre‑commit checks locally before pushing.

## 4. Clean Code Core Rules

1. **Meaningful names**

    * Reveal intent. No abbreviations that hide meaning. Avoid encodings and Hungarian notation.
    * Choose pronounceable, searchable names. One concept, one name.
2. **Small functions**

    * Do one thing. ≤ 20 lines typical, often much smaller. Extract nested logic.
    * Use descriptive parameters. Prefer objects over long parameter lists.
3. **Single Responsibility**

    * Each module/class/function has one reason to change. Split policies from mechanics. Separate I/O from computation.
4. **Expressive code over comments**

    * Prefer self‑explanatory names and structure. Comments only when they add essential intent, warning, or domain context. No redundant or outdated comments.
5. **Error handling**

    * Fail fast. No silent catches. Wrap external failures with context. Prefer explicit result types or exceptions over sentinel values.
6. **No duplication**

    * Factor shared logic. Consolidate knowledge in one place. DRY beats WET.
7. **Formatting**

    * Consistent, automated formatting. Short files. Narrow scopes. Early returns preferred.
8. **Data and state**

    * Minimize mutable state. No global writable state. Prefer pure functions and immutable data structures when practical.
9. **Dependencies**

    * Invert dependencies at boundaries. Depend on abstractions, not concretions. Constructor or functional injection only.
10. **Boundaries**

* Isolate external systems behind ports/adapters. Validate at boundaries. Keep domain pure.

## 5. Modern Design Preferences

* **Composition over inheritance.** Use interfaces/traits and small objects. Inheritance only for true is‑a relationships without behavior surprises.
* **Hexagonal / Clean Architecture.** Domain core independent of frameworks and I/O. Adapters for UI, DB, network, hardware.
* **Small modules and packages.** High cohesion, low coupling. Public surface is minimal and deliberate.
* **Explicit contracts.** Types, schemas, and interfaces are the API. Use versioned contracts and backward compatibility policies.
* **Idempotency and determinism.** Side‑effecting operations are explicit. Deterministic core logic to simplify testing.
* **Concurrency by design.** Prefer message passing and immutability. Avoid shared mutable state and ad‑hoc locking.
* **Configuration as data.** No logic in configs. All config is validated, documented, and overridable per environment.
* **Security by default.** Least privilege, input validation, output encoding, secrets managed via vaults, reproducible builds.

## 6. Testing Policy (All Code Is Tested)

* **TDD is preferred.** Write a failing test, then the simplest passing code, then refactor.
* **Test pyramid.** Many fast unit tests, fewer integration tests, minimal end‑to‑end tests. Keep feedback cycles short.
* **Coverage.** Line coverage ≥ 90% for domain modules, ≥ 80% repo‑wide. Critical paths 100%. Mutation score targets defined per repo; aim ≥ 70%.
* **Kinds of tests.**

    * Unit: pure, isolated, no I/O. Millisecond runtime.
    * Property‑based: invariants and generators for core logic.
    * Contract/consumer‑driven: validate interfaces between services.
    * Integration: real adapters and DB with ephemeral environments.
    * E2E/acceptance: user‑visible workflows in the thinnest slice.
    * Regression: capture fixed bugs with minimal repros.
* **Test quality.** Tests read like documentation. One assertion concept per test. Deterministic, independent, and named for behavior.
* **Fixtures.** Minimal, explicit, and local to tests. Builders/factories over shared mutable fixtures.
* **Performance tests.** Budgets defined and enforced in CI for hot paths.

## 7. Tooling and Automation

* **Formatters.** Enforced at commit via pre‑commit hooks and CI.
* **Linters/static analysis.** Zero new warnings policy. Security scanners enabled.
* **Type checking.** Strong typing favored. Treat type errors as build failures.
* **CI gates.** Build, test, lint, type‑check, and security scan on every change to main and PRs. Green CI required to merge.
* **Reproducibility.** Locked dependencies, hermetic builds, pinned toolchains, containerized dev and CI where possible.

## 8. Branching, Reviews, and Merging

* **Small PRs.** ≤ 300 lines changed preferred. Large changes must be broken down.
* **Review focus.** Design clarity, correctness, tests, naming, and public surface. Nits auto‑fixed by tools, not reviewers.
* **Definition of done.**

    * All tests pass with coverage targets.
    * New code has unit tests. Changed behavior has updated tests.
    * Public APIs documented. Changelog updated if applicable.
    * Security and performance checks green.
* **Merging.** Fast‑forward or squash. Keep history linear. Rebase to resolve conflicts.

## 9. Documentation

* **Living docs.** README for getting started, AGENTS.md for standards, ADRs for significant decisions.
* **API docs.** Generated from code where possible. Examples included and tested.
* **Runbooks.** Production runbooks for operations and incident response.

## 10. Observability and Operations

* **Metrics, logs, traces.** Emitted at boundaries with correlation. PII safe by default.
* **Feature flags.** Dark launch and gradual rollout. Flags owned and retired on schedule.
* **Error budgets and SLOs.** Defined per service. Changes that threaten SLOs require explicit approval.

## 11. Data and Migrations

* **Schema governance.** Versioned schemas with backward compatibility policy. Migrations are reversible and tested.
* **Data ownership.** Clear owners for datasets. Access audited. Retention defined.

## 12. Dependencies and Versions

* **Minimal dependencies.** Prefer standard library and small audited libs. No unmaintained or unlicensed deps.
* **Updates.** Regular dependency update cadence with automated PRs and smoke tests.

## 13. Anti‑Patterns (Avoid)

* God classes and long functions.
* Hidden temporal coupling and boolean parameter bloat.
* Overuse of inheritance and singletons.
* Magic numbers and stringly‑typed logic.
* Commented‑out code and TODOs without owners and dates.
* Framework‑driven domain logic. Keep domain independent.

## 14. Example Pull Request Checklist

* [ ] Names reveal intent
* [ ] Functions small and single purpose
* [ ] No duplication introduced
* [ ] Boundaries isolated (ports/adapters)
* [ ] Tests added or updated; pyramid respected
* [ ] Coverage and mutation targets met
* [ ] Public API documented and versioned
* [ ] Lint, type, security, and CI green
* [ ] Performance budgets respected
* [ ] ADR added/updated if design significant

## 15. Governance

* Violations are defects. Create an issue with label `standards` and a failing test where possible.
* Disagreements resolved via ADRs referencing these principles first, specific technology second.
* This document is versioned. Changes require review by code owners and an ADR.

---

**Summary:** Write the clearest, smallest code that expresses intent, with tests that prove it. Design for change. Automate enforcement. Prefer composition, isolation, and explicit contracts.

---

## 16. C++14 Build, Linting, and Test Commands

### 16.1 Toolchain baseline

* **Compilers:** GCC ≥ 7 or Clang ≥ 6 with full C++14 support.
* **Build:** CMake ≥ 3.16, Ninja recommended.
* **Quality:** clang-format, clang-tidy, cppcheck (optional).
* **Speed:** ccache recommended.
* **Debugging:** Sanitizers available on Clang/GCC.

### 16.2 CMake project defaults

Add these to the top-level `CMakeLists.txt`:

```cmake
cmake_minimum_required(VERSION 3.16)
project(<project_name> LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 14)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)

# Warnings are errors in CI. Locally you may relax by toggling via option.
option(STRICT_WARNINGS "Treat warnings as errors" ON)

if (MSVC)
  add_compile_options(/W4)
  if (STRICT_WARNINGS)
    add_compile_options(/WX)
  endif()
else()
  add_compile_options(-Wall -Wextra -Wpedantic)
  if (STRICT_WARNINGS)
    add_compile_options(-Werror)
  endif()
endif()

# Enable compile_commands.json for tooling
set(CMAKE_EXPORT_COMPILE_COMMANDS ON)
```

Sanitizers for debug builds:

```cmake
option(ENABLE_ASAN "Enable AddressSanitizer" ON)
option(ENABLE_UBSAN "Enable UndefinedBehaviorSanitizer" ON)
if (CMAKE_CXX_COMPILER_ID MATCHES "Clang|GNU" AND CMAKE_BUILD_TYPE STREQUAL "Debug")
  if (ENABLE_ASAN)
    add_compile_options(-fsanitize=address -fno-omit-frame-pointer)
    add_link_options(-fsanitize=address)
  endif()
  if (ENABLE_UBSAN)
    add_compile_options(-fsanitize=undefined -fno-omit-frame-pointer)
    add_link_options(-fsanitize=undefined)
  endif()
endif()
```

### 16.3 Build commands

Configure and build:

```bash
# From repo root
cmake -S . -B build -G Ninja -DCMAKE_BUILD_TYPE=Debug
cmake --build build -j
```

Release build:

```bash
cmake -S . -B build-release -G Ninja -DCMAKE_BUILD_TYPE=Release
cmake --build build-release -j
```

### 16.4 Test commands with CTest

Register tests in CMake:

```cmake
include(CTest)
enable_testing()
# Example:
# add_executable(example_test tests/example_test.cpp)
# add_test(NAME example_test COMMAND example_test)
```

Run tests:

```bash
# All tests with verbose failure output
ctest --test-dir build --output-on-failure -j
```

Filter tests:

```bash
ctest --test-dir build -R "<regex>" --output-on-failure
```

### 16.6 Formatting

One style per repo. Enforce with clang-format.

Format all files:

```bash
pre-commit run clang-format -a
```

### 16.7 Coverage (optional)

GCC/Clang example:

```bash
cmake -S . -B build-cov -G Ninja   -DCMAKE_BUILD_TYPE=Debug   -DCMAKE_CXX_FLAGS="--coverage" -DCMAKE_EXE_LINKER_FLAGS="--coverage"
cmake --build build-cov -j
ctest --test-dir build-cov --output-on-failure
# Using gcovr to report:
gcovr -r . build-cov --exclude-directories tests --xml -o coverage.xml
gcovr -r . build-cov --exclude-directories tests --branches --html-details -o coverage.html
```

---

## 17. Pre‑commit: mandatory local checks

### 17.1 Setup

```bash
pipx install pre-commit           # or: pip install --user pre-commit
pre-commit install                # installs git hooks
pre-commit run --all-files        # run on entire repo
```

### 17.2 CI
* CI must run `pre-commit run --all-files` as a gate.

### 17.3 Quick CI snippet

```bash
pre-commit install
pre-commit run --all-files
```

### 17.4 Commit checklist

Every commit must be cut only after local checks are green. Run the full unit test suite and `pre-commit run --all-files`, and fix all findings before committing.

---

## 18. Quickstart

```bash
# 1) Configure build with warnings and sanitizers
cmake -S . -B build -G Ninja -DCMAKE_BUILD_TYPE=Release

# 2) Run format + tidy via pre-commit on all files
pre-commit run --all-files

# 3) Build
cmake --build build -j

# 4) Test
ctest --test-dir build --output-on-failure -j
```
