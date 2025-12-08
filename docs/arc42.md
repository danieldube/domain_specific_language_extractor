# Arc42 Architecture Documentation

## 1. Introduction and Goals

### 1.1 Scope
*The program extracts a domain-specific language (DSL) from complete C++ codebases by analyzing all defined names and their implemented meanings. It identifies incoherent DSL usage within the program and supports analysis on both Linux and Windows environments using clang ASTs.*

### 1.2 Stakeholders
- Developers and architects maintaining C++ projects.
- Reviewers and QA engineers validating DSL coherence on pull requests.
- Tooling and DevOps engineers integrating DSL analysis into CI pipelines.

### 1.3 User Stories
- As a developer, I want to extract the domain specific language from my program.
- As a reviewer, I want to run the tool on a pull request to see conflicts to the current domain specific language.
- As a maintainer, I want to know whether my code base is coherent and easy to understand.

### 1.4 Architecture Characteristics (Quality Goals)
- **Accuracy of DSL extraction:** Derived DSL must reflect names and implementation semantics across the full codebase.
- **Consistency detection:** Highlight incoherent or conflicting DSL usage to improve understandability.
- **Portability:** Runs on Linux and Windows with consistent behavior and tooling support.
- **Integration readiness:** Supports execution in CI and pull request workflows.
- **Performance and scalability:** Handles complete C++ programs using clang AST analysis without prohibitive runtime costs.
- **Maintainability and extensibility:** Clear modular design to evolve analysis rules and LLM prompting.
- **Observability:** Transparent reporting of detected DSL elements and conflicts for user review.

## 2. Architecture Constraints
- Must run as a self-contained CLI on Linux and Windows, relying on clang/LLVM toolchain available on both platforms.
- No external network dependencies during analysis (offline-friendly for CI).
- Prefer deterministic, testable modules with clear contracts to support maintainability and extensibility.
- Output must be consumable by CI (non-zero exit on incoherence findings, machine-readable JSON, Markdown for humans).
- Repository uses pre-commit, clang-format, and clang-tidy; new code must conform.

## 3. System Scope and Context
*To be detailed (business context and technical context diagrams, interfaces, and external dependencies).*

## 4. Solution Strategy
- Adopt a **single-process CLI pipeline** (ADR 0001) with modular stages: source acquisition, clang-based parsing, DSL extraction, coherence analysis, and reporting.
- Use clang tooling to build an AST index and semantic facts; avoid custom parsers.
- Represent DSL terms (entities, actions, relationships) in a stable internal model that can be serialized to JSON.
- Keep potential LLM usage behind an interface so deterministic heuristics remain the default and tests remain reproducible.
- Provide extension points (plug-in modules) for extraction heuristics and coherence rules without altering pipeline wiring.

## 5. Building Block View

### 5.1 High-Level Components
- **CLI Frontend:** argument parsing, configuration loading, and command dispatch (e.g., `analyze`, `report`, `cache clean`).
- **Source Acquisition:** resolves repository root, compilation database (`compile_commands.json`), include paths, and normalization of source files to analyze.
- **Parsing & AST Indexer:** wraps clang tooling to produce a semantic index (symbols, types, call graph, comments); caches results for reuse.
- **DSL Extraction Engine:** converts AST facts into DSL terms (domain entities, actions, relationships) using deterministic heuristics; optionally enriches via LLM strategies behind a small interface.
- **Coherence Analyzer:** detects conflicting or ambiguous DSL usage across modules and files; maps findings to locations.
- **Reporting Module:** renders Markdown and JSON outputs; manages exit codes based on findings severity; supports CI-friendly summaries.
- **Plugin Interfaces:** contracts for extraction heuristics, coherence rules, and report renderers to allow future additions.

#### 5.1.1 Name/Behavior Coherence Rules (LLM-free)
- **Intent heuristics:** enforce naming conventions against observed behavior: `get*` must not mutate state; `set*` should assign or mutate; `is*/has*` must return boolean and remain side-effect free; resource verbs (`open/close/init/teardown`) must align with lifecycle events in the call graph.
- **Consistency across occurrences:** symbols sharing a DSL term (e.g., `path_length`) must express comparable semantics. The Coherence Analyzer compares AST facts (call targets, operations, side effects) across implementations to flag divergence—such as one `path_length` derived from vector norms vs. another obtained via `estimate_overall_path_length(path)`—without any LLM involvement.
- **Fact sources:** relies on the AST index for symbol definitions, control-flow summaries (mutations, returns, exceptions), call graph edges, and basic type info to run the rules deterministically.

### 5.2 Data Structures
- **AST Fact Model:** normalized representation of declarations, definitions, symbol references, and comments.
- **DSL Term Model:** typed objects for entities, actions, relationships, and provenance metadata (file, line, symbol origin).
- **Findings Model:** coherence issues with severity, description, and source locations.

### 5.3 Module Dependencies
- CLI Frontend depends on Source Acquisition and Reporting.
- Source Acquisition feeds Parsing & AST Indexer.
- Parsing & AST Indexer feeds DSL Extraction Engine.
- DSL Extraction Engine feeds Coherence Analyzer and Reporting.
- Reporting consumes DSL Term Model and Findings Model.
- See `docs/diagrams/arc42-section5-building-blocks.puml` for the corresponding PlantUML component diagram.

## 6. Runtime View

### Scenario: Analyze Repository for DSL Coherence
1. **CLI Frontend** parses arguments (e.g., `dsl-extract analyze --format markdown,json --out reports/`).
2. **Source Acquisition** loads configuration, resolves repository root, and reads `compile_commands.json`; outputs normalized file list and compilation settings.
3. **Parsing & AST Indexer** runs clang tooling to build the AST fact model; caches intermediate artifacts if enabled.
4. **DSL Extraction Engine** transforms AST facts into DSL terms using configured heuristics or plug-ins.
5. **Coherence Analyzer** evaluates DSL terms to find conflicts or ambiguities; produces findings.
6. **Reporting Module** renders Markdown and JSON; sets exit code (0 if no issues, non-zero otherwise) for CI.
7. Optional: **Cache Manager** can persist AST facts to accelerate subsequent runs.
- Sequence diagram available at `docs/diagrams/arc42-section6-runtime.puml`.

## 7. Deployment View
- **Packaging:** distribute as a single CLI (static or self-contained) with dependencies on clang/LLVM runtimes.
- **Environments:**
  - **Developer machines:** Linux and Windows with local clang toolchain; optional cache directory per repo.
  - **CI agents:** run CLI in build pipelines; artifacts (JSON/Markdown) stored as pipeline outputs.
- **Configuration:** paths to toolchain, include directories, and compilation database provided via CLI args or config file.
- **Data:** optional on-disk cache for AST facts stored under project `.dsl-cache/` to speed reruns; cleaned via CLI command.
- Deployment diagram available at `docs/diagrams/arc42-section7-deployment.puml`.

## 8. Crosscutting Concepts
- **Configuration:** YAML/TOML config plus CLI overrides; validation at startup; defaults favor deterministic analysis.
- **Logging & Observability:** structured logging with verbosity flags; timing metrics per stage; summary of file counts and findings.
- **Error Handling:** fail fast with contextual errors; non-zero exits on fatal parsing errors or incoherence findings.
- **Caching:** optional AST cache keyed by toolchain/version and source hash to avoid full reparse.
- **Extensibility:** plug-in registry for extraction heuristics, coherence rules, and report renderers; interfaces versioned.
- **LLM Usage:** optional strategy behind a small interface; must allow deterministic fallback; prompts and outputs logged for audit when enabled.
- **Testing:** unit tests per stage with mock contracts; integration tests run full pipeline on sample C++ fixtures.

## 9. Architecture Decisions
- **ADR 0001:** Adopt modular single-process CLI pipeline with staged analysis and plug-in extension points (`docs/adr/0001-modular-cli-pipeline.md`).

## 10. Quality Requirements
*To be detailed (quality tree and scenarios aligned with architecture characteristics).*

## 11. Risks and Technical Debt
*To be detailed (open risks, mitigations, and technical debt items).*

## 12. Glossary
*To be detailed (domain terms, acronyms, and definitions).*
