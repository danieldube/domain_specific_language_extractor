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
*To be detailed (e.g., tooling, standards, compliance, platform constraints).* 

## 3. System Scope and Context
*To be detailed (business context and technical context diagrams, interfaces, and external dependencies).* 

## 4. Solution Strategy
*To be detailed (overall architectural and technology decisions, use of clang AST and LLM integration strategies).* 

## 5. Building Block View
*To be detailed (static structure, components, and responsibilities).* 

## 6. Runtime View
*To be detailed (key scenarios and their runtime interactions).* 

## 7. Deployment View
*To be detailed (environments, nodes, and deployment topologies for Linux and Windows).* 

## 8. Crosscutting Concepts
*To be detailed (logging, configuration, security, error handling, LLM usage).* 

## 9. Architecture Decisions
*To be detailed (ADR references and status).* 

## 10. Quality Requirements
*To be detailed (quality tree and scenarios aligned with architecture characteristics).* 

## 11. Risks and Technical Debt
*To be detailed (open risks, mitigations, and technical debt items).* 

## 12. Glossary
*To be detailed (domain terms, acronyms, and definitions).* 
