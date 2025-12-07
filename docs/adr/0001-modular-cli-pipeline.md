# ADR 0001: Modular CLI Analyzer Pipeline

- **Status:** Accepted
- **Date:** 2024-05-05
- **Context:**
  - The project must extract domain-specific language (DSL) concepts from full C++ codebases, detect incoherent usage, and run consistently on Linux and Windows.
  - Portability, maintainability, and CI integration are primary quality goals; distributed orchestration is unnecessary at current scale.
  - We need a structure that isolates clang-based parsing, DSL extraction heuristics, LLM prompting (if used), and reporting so each can evolve independently.

- **Decision:**
  - Build a **single-process CLI tool** organized as a **pipeline of modular stages**:
    - **Source acquisition:** resolve repository root, compilation database, and include paths; output normalized source set.
    - **Parsing and analysis:** run clang tooling to emit an AST index and semantic facts needed for DSL extraction.
    - **DSL extraction:** transform AST facts into DSL terms (entities, actions, relationships) using deterministic rules and optional LLM prompts behind a narrow interface.
    - **Coherence analysis:** identify conflicting or ambiguous DSL usages across the codebase.
    - **Reporting:** emit human-readable Markdown plus machine-readable JSON for CI consumption; exit codes encode pass/fail for incoherence findings.
  - Each stage exposes a clear input/output contract and can be swapped via dependency injection. Stages communicate via in-memory objects and may persist intermediate artifacts (e.g., AST cache) to disk for reuse.
  - Provide extension points for heuristics and prompts as plug-in modules without changing pipeline wiring.

- **Consequences:**
  - **Positive:**
    - High maintainability through isolated, testable stages; easier to extend heuristics or swap prompt strategies.
    - Simple CI adoption via a single binary and deterministic exit codes; no distributed ops burden.
    - Portability is handled within the CLI by configuring clang toolchains per OS; no network dependencies.
  - **Negative/Trade-offs:**
    - Limited horizontal scaling compared to distributed designs; large codebases rely on efficient single-process parallelism.
    - Requires careful definition of stage contracts to avoid tight coupling.
  - **Follow-up:**
    - Define stage interfaces in code, including data contracts for AST facts and DSL term representations.
    - Implement CLI commands/subcommands for analysis, cache management, and report generation.
    - Specify deterministic name/behavior coherence rules (e.g., purity for `is*/has*`, mutation expectations for `set*`, and consistent semantics for repeated DSL terms like `path_length`) so offline analysis can flag misuses without LLMs.
