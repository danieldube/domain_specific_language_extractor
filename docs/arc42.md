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
- **Business context:** The CLI serves developers and reviewers running DSL
  coherence checks locally or inside CI. Inputs are source trees (typically a
  Git checkout) plus optional configuration; outputs are Markdown/JSON reports
  and a process exit code that CI uses to gate merges. Results can be
  re-emitted later via the `report` command without re-running analysis.
- **Technical context:**
  - **Primary actors:** Developers invoking `dsl-extract` locally; CI agents
    executing the same commands on pull requests; reviewers reading generated
    reports; DevOps engineers wiring the tool into pipelines.
  - **External systems:** clang/LLVM toolchain for AST indexing, the local
    filesystem for walking sources and caching facts, and (optionally) an LLM
    provider behind a strategy interface. No network calls are required in the
    default deterministic mode, keeping CI runs offline-friendly.
  - **Interfaces:** CLI flags and YAML config; deterministic exit codes (`0`
    success, `2` incoherence findings, `1` fatal errors) consumed by CI; report
    artifacts consumed by humans and automated checks.
  - **Context reference:** See Section 5 building blocks and the runtime
    sequence diagram (`docs/diagrams/arc42-section6-runtime.puml`) for how the
    actors interact with pipeline stages.

## 4. Solution Strategy
- Adopt a **single-process CLI pipeline** (ADR 0001) with modular stages: source acquisition, clang-based parsing, DSL extraction, coherence analysis, and reporting.
- Use clang tooling to build an AST index and semantic facts; avoid custom parsers.
- Represent DSL terms (entities, actions, relationships) in a stable internal model that can be serialized to JSON.
- Keep potential LLM usage behind an interface so deterministic heuristics remain the default and tests remain reproducible.
- Provide extension points (plug-in modules) for extraction heuristics and coherence rules without altering pipeline wiring.

## 5. Building Block View

### 5.1 High-Level Components
- **CLI Frontend:** argument parsing, configuration loading, and command dispatch (e.g., `analyze`, `report`, `cache clean`).
  The `report` subcommand re-emits cached Markdown/JSON reports from a prior
  `analyze` run without reprocessing the source tree, optionally targeting a new
  output directory.
- **Source Acquisition:** resolves repository root, validates the project layout, normalizes source file paths, and filters out generated/build artifacts. The `CMakeSourceAcquirer` is an adapter for CMake-based projects but remains interchangeable with other acquirers without exposing build-system details.
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
- **AnalysisConfig:** provides `root_path`, preferred output `formats`, optional
  `scope_notes`, structured `logging` config, AST cache preferences, and an
  injectable logger for stage-level observability.
- **SourceAcquisitionResult:** normalized `files` list (deduplicated, sorted) and a normalized `project_root` shared across pipeline stages.
- **AST Fact Model:** normalized representation of declarations, definitions, symbol references, and comments.
- **DSL Term Model:** typed objects for entities, actions, relationships, and provenance metadata (file, line, symbol origin).
- **Findings Model:** coherence issues with severity, description, and source locations.

### 5.4 Source Acquisition Contract
- **Inputs:**
  - `AnalysisConfig.root_path` pointing to the project under analysis.
- **Processing rules:**
  - Fail fast if the root is missing or not compatible with the chosen acquirer (for example, missing `CMakeLists.txt` when using the CMake adapter).
  - Walk the project tree, include only C/C++ sources and headers, and skip generated artifacts (such as the configured build directory).
  - Normalize and sort paths for deterministic output.
- **Outputs:**
  - Deterministic list of absolute source/header paths ready for AST indexing.
  - Normalized root path shared across pipeline stages.

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
2. **Source Acquisition** loads configuration, resolves the repository root, and returns a normalized file list for analysis.
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
- **Configuration:** paths to toolchain and include directories provided via CLI args or config file.
- **Data:** optional on-disk cache for AST facts stored under project `.dsl_cache/` to speed reruns; cleaned via CLI command.
- Deployment diagram available at `docs/diagrams/arc42-section7-deployment.puml`.

## 8. Crosscutting Concepts
- **Configuration:** YAML config plus CLI overrides; typed parsing via
  `yaml-cpp` isolates the reader from the analysis core and rejects unknown keys
  early; defaults favor deterministic analysis.
- **Logging & Observability:** structured logging with verbosity flags; timing metrics per stage; summary of file counts and findings.
- **Error Handling:** fail fast with contextual errors; non-zero exits on fatal parsing errors or incoherence findings.
- **Caching:** optional AST cache keyed by toolchain/version and source hash to avoid full reparse.
- **Extensibility:** plug-in registry for extraction heuristics, coherence rules, and report renderers; interfaces versioned.
- **LLM Usage:** optional strategy behind a small interface; must allow deterministic fallback; prompts and outputs logged for audit when enabled.
- **Testing:** unit tests per stage with mock contracts; integration tests run full pipeline on sample C++ fixtures.

## 9. Architecture Decisions
- **ADR 0001:** Adopt modular single-process CLI pipeline with staged analysis and plug-in extension points (`docs/adr/0001-modular-cli-pipeline.md`).

## 10. Quality Requirements
- **Quality tree anchors:** Accuracy of DSL extraction, deterministic results
  across platforms, CI-friendly UX (clear exit codes, structured logs), and
  maintainability via modular stages and plug-in interfaces.
- **Scenarios:**
  - *Correctness in CI:* Given a pull request with mixed naming semantics,
    `dsl-extract analyze` returns exit code `2` and emits Markdown/JSON reports
    listing incoherence findings within 10 minutes for a 50k-LOC C++ project.
  - *Deterministic reruns:* Re-running `dsl-extract analyze` on unchanged input
    produces byte-identical findings and reports; with `--cache-ast`, AST
    parsing time drops on subsequent runs without altering results.
  - *Portability:* The same configuration executed on Linux and Windows yields
    equivalent findings and exit codes, assuming matching clang versions and
    include paths.
  - *Observability:* Enabling verbose logging surfaces per-stage timings and
    file counts; fatal errors include contextual messages (e.g., missing
    toolchain or invalid config key) to unblock users without log spelunking.
  - *Extensibility safety:* Adding a new extraction heuristic or coherence rule
    via the plug-in interface cannot corrupt the existing DSL term model; unit
    and integration tests validate the new rule before release.
  - *UX/readability:* Reports include canonical DSL terms, relationships, and
    conflict summaries with source locations so reviewers can navigate issues
    quickly from CI artifacts.

## 11. Risks and Technical Debt
- **clang/LLVM compatibility drift:** Toolchain version mismatches across
  developer machines and CI can change AST details and results. Mitigation:
  document supported versions, pin CI images, and validate via regression
  fixtures.
- **Large-repo performance:** Very large codebases can still exceed time
  budgets even with caching. Mitigation: profile parsing hot spots, allow build
  directory exclusion, and plan for incremental analysis strategies.
- **Windows parity gaps:** Limited automated coverage on Windows can mask
  portability issues. Mitigation: expand CI matrix with Windows runners and add
  platform-specific integration tests.
- **Cache invalidation correctness:** AST cache relies on toolchain and source
  hashes; subtle misses could reuse stale data. Mitigation: strengthen cache
  keys, record metadata in cache manifests, and expose a `clean` command (already
  present) in CI workflows.
- **Plug-in API stability:** Extension interfaces are young; breaking changes
  could block third-party rules. Mitigation: version plug-in contracts and
  document migration guides.
- **LLM strategy uncertainty:** Optional LLM enrichment can introduce
  nondeterminism or privacy concerns. Mitigation: keep deterministic mode
  default, require explicit opt-in, and log prompts/outputs when enabled.

## 12. Glossary
- **AST cache:** On-disk storage of clang-derived facts keyed by source hash
  and toolchain version to accelerate repeat analyses.
- **Coherence finding:** A reported inconsistency between DSL intent and
  implementation semantics, produced by the Coherence Analyzer and surfaced in
  reports with severity and locations.
- **DSL term:** Canonicalized domain entity, action, or relationship extracted
  from names, signatures, and behavior; the primary unit of DSL representation
  in reports.
- **Analysis pipeline:** Ordered stages of source acquisition, AST indexing,
  DSL extraction, coherence analysis, and reporting executed by the CLI.
- **Report formats:** Markdown and JSON outputs that mirror each other to serve
  both reviewers and automated checks; regenerated by the `report` subcommand
  without re-running analysis.
- **Scope notes:** Optional user-provided text attached to a run to describe
  the analysis context (e.g., CI job, branch, or ticket).
