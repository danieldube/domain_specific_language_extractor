# Domain-Specific Language Representation

This project extracts a Domain Specific Language (DSL) from C++ programs by interpreting the names and implementations described in the source code. The extracted language must be easy for humans to read and useful for day-to-day development. The representation described below is the canonical format that every analyzer run should emit.

## Output Format

* **Medium:** Markdown document stored alongside analysis artifacts (for automation) and viewable in plain text (for humans). Tools may additionally emit a machine-friendly JSON file that mirrors the Markdown structure, but the Markdown is normative.
* **Sections:** Ordered to support quick scanning from concepts to relationships to quality notes.

### 1. Analysis Header

| Field | Meaning |
| --- | --- |
| `Generated On` | Timestamp and analyzer version. |
| `Source` | Repository or file set that was analyzed. |
| `Scope Notes` | Filters or exclusions applied during analysis. |

### 2. Canonical Terms (Glossary)

Each domain term is a normalized name derived from identifiers in the codebase. Terms are grouped by **kind** to match the way they appear in code and to align with how humans discuss the system.

| Column | Meaning |
| --- | --- |
| `Term` | Canonical, human-readable spelling. Derived from identifier names and comments. |
| `Kind` | `Entity` (nouns such as classes/structs), `Action` (verbs such as functions/methods), `Modifier` (adjectives such as flags/traits), or `Concept` (higher-level namespaces/modules). |
| `Definition` | Short description synthesized from the implementation, not just the name. Clarifies the behavior or role implied by the code. |
| `Evidence` | References to code locations (e.g., `path:line-range`) that justify the definition. |
| `Aliases` | Alternative spellings or abbreviations found in code. The canonical term must remain stable. |
| `Usage Count` | Number of references in the analyzed scope to show term prevalence. |

### 3. Relationships

Relationships explain how terms interact. They provide the connective tissue of the DSL so humans can reason about workflows and data flow.

| Column | Meaning |
| --- | --- |
| `Subject` | Canonical term initiating the relationship (often an `Action` or `Entity`). |
| `Verb` | Relationship type, such as `creates`, `reads`, `updates`, `deletes`, `depends on`, `publishes`, or `consumes`. |
| `Object` | Canonical term receiving the relationship. |
| `Evidence` | Code locations supporting the relationship (calls, field accesses, return types, etc.). |
| `Notes` | Disambiguating details or preconditions discovered in code. |

### 4. Workflows (Optional but Recommended)

Brief, ordered lists that describe common sequences of relationships (e.g., how a request flows through the system). Workflows help readers connect individual terms into domain scenarios.

### 5. Incoherence Report

This section flags inconsistent or conflicting language usage.

| Column | Meaning |
| --- | --- |
| `Term` | Canonical term involved in the incoherence. |
| `Conflict` | Description of the conflict (e.g., same term used for different concepts, or different terms used for the same concept). |
| `Examples` | Code locations showing the conflicting usages. |
| `Suggested Canonical Form` | Recommendation for resolving the conflict. |

### 6. Extraction Notes

Short list of heuristics or special cases encountered during extraction (e.g., heuristics for templated types, handling of operator overloads). This helps future maintainers understand why specific terms were normalized a certain way.

## Authoring Guidance

* Favor concise definitions that capture intent and behavior, not just type shapes.
* Keep evidence links minimal but sufficient—prefer one or two definitive locations over many minor references.
* When conflicts are found, propose a single canonical term and explain the rationale in the incoherence report.
* Ensure the Markdown tables render within standard terminal widths when possible; prefer wrapping long text with line breaks.
* The JSON mirror should follow the same field names to simplify automation across Linux and Windows environments.
