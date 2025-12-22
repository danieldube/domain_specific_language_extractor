# DSL Extraction Report

## Analysis Header

| Field | Value |
| --- | --- |
| Generated On | <timestamp> |
| Source | <root> |
| Scope Notes | None |

## Canonical Terms (Glossary)

| Term | Kind | Definition | Evidence | Aliases | Usage Count |
| --- | --- | --- | --- | --- | --- |
| use | Action | int Use(const Widget &) | uses Widget | calls Add | int Add(int, int) | <root>/src/main.cpp:7:1-7:74<br><root>/src/main.cpp:7:15-7:21<br><root>/src/main.cpp:7:40-7:71 | Use | 3 |
| add | Action | int Add(int, int) | <root>/src/main.cpp:5:1-5:40<br><root>/src/main.cpp:7:40-7:71 | Add | 2 |
| widget | Entity | Widget | value | value: int | <root>/src/main.cpp:1:1-3:2<br>Widget<br>&nbsp;&nbsp;Range: <root>/src/main.cpp:2:3-2:12<br><root>/src/main.cpp:7:15-7:21 | Widget | 3 |

## External Dependencies

| Name | Kind | Definition | Evidence | Usage Count |
| --- | --- | --- | --- | --- |
| None | - | - | - | - |

## Relationships

| Subject | Verb | Object | Evidence | Notes | Usage Count |
| --- | --- | --- | --- | --- | --- |
| use | calls | add | <root>/src/main.cpp:7:40-7:71 | calls Add | 1 |
| use | uses-type | widget | <root>/src/main.cpp:7:15-7:21 | uses Widget | 1 |

## Workflows

- use workflow
  1. use calls add
  2. use uses-type widget

## Incoherence Report

| Term | Conflict | Examples | Suggested Canonical Form | Details |
| --- | --- | --- | --- | --- |
| None | - | - | - | - |

## Extraction Notes

- Heuristic extraction canonicalized identifiers, synthesized definitions from signatures, and inferred relationships from AST facts.
