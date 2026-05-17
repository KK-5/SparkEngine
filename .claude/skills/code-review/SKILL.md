---
name: code-review
description: Use when the user asks to review their code implementation against a previously discussed design plan, or when the user has just written code based on a design the model proposed and wants it reviewed. Also auto-trigger when the user appears to have completed implementing a design previously discussed in the conversation and is ready for review. Evaluates implementation against the original design plan, checks for functional correctness, potential bugs, code style compliance, variable naming accuracy and typos. Only reviews the current implementation — does not propose new designs or large refactors.
---

# code-review

Review the user's current code implementation against the design plan that was previously discussed in this conversation. Focus exclusively on the implementation that corresponds to the current design proposal.

## 1. Gather context

Before reviewing, identify:

- **The design plan**: What design was proposed earlier in this conversation? What were the key decisions, API shapes, class structures, and constraints?
- **The implementation**: What files did the user modify or create? Use `git diff` and `git status` to see the current changes. Read the modified files in full.
- **Scope boundary**: Only review code that relates to the current design proposal. Do not review unrelated changes or pre-existing code.

## 2. Review checklist

Go through each category systematically. Report findings grouped by severity.

### A. Design alignment

- Does the implementation follow the agreed design, or are there intentional deviations?
- For each deviation: is it clearly an improvement, or does it risk breaking the design's intent?
- Are the key abstractions, interfaces, and data flow preserved?

### B. Functional correctness

- Will the code behave correctly for the primary use case?
- Are edge cases handled? (null/empty inputs, boundary values, error paths)
- Are there potential race conditions or thread-safety issues?
- Are resource lifetimes correct? (memory leaks, double-frees, use-after-free)
- For RHI/DX12 code specifically: are D3D12 object lifetimes managed correctly? Are resources released through the proper queues?
- Are there any logic errors or off-by-one mistakes?

### C. Potential bugs

- Null pointer dereferences
- Uninitialized variables
- Missing error handling where it matters (at system boundaries)
- Incorrect assumptions about API contracts
- Signed/unsigned mismatches, truncation, overflow

### D. Code style & conventions (based on CLAUDE.md)

- Uses `eastl::` containers, not `std::` (with accepted exceptions)
- `Ptr<T>` and `UniquePtr<T>` from `Core/Base.h`, not raw `std::shared_ptr`
- Validation gates use `Validation::isEnabled` or `ASSERT`, never bare `assert()`
- DX12 backend headers use `"Foo.h"` for siblings, `<RHI/...>` for cross-module
- Logging uses `LOG_ERROR`/`LOG_WARN` with `[ClassName]` prefix
- Resource handles in render layer are `RHIHandle` with `NullHandle` sentinel
- No render-layer concepts leaking into RHI

### E. Naming & typos

- Variable/function/class names match the project conventions (PascalCase for types, camelCase for variables/functions)
- Are there any obvious spelling mistakes in identifiers?
- Are names descriptive and accurate for what they represent?
- Do names use terminology consistent with the rest of the codebase?
- Are abbreviations used consistently?

## 3. Report format

Present findings in this structure:

```
## Code Review: [brief description of what was reviewed]

### Design Alignment
- [findings]

### Bugs & Functional Issues
- [findings, ordered by severity — critical first]

### Style & Conventions
- [findings]

### Naming & Typos
- [findings]

### Summary
[One sentence verdict + list of must-fix items if any]
```

## 4. Rules

- **Language: Output all review text in Chinese.** Category headers can remain in English, but all findings and descriptions must be in Chinese.
- **Spelling errors: Fix them directly.** When you find a typo in a variable/function/class name, correct it with the Edit tool on the spot — don't just report it.
- **TODO placeholders: Skip them.** If the user has clearly marked code with `[TODO]` or explicit comments stating it's unimplemented, do not flag it as an issue — unless it directly prevents compilation.
- **Only review the current implementation.** Do not propose new designs, alternative architectures, or large refactors.
- **Be specific.** Reference exact file paths and line numbers for every finding.
- **Don't nitpick.** Skip purely cosmetic preferences that don't violate project conventions.
- **If there are no issues in a category, say so briefly** — don't omit the category.
- **Distinguish must-fix from nice-to-have.** Critical bugs and design misalignments are must-fix; style suggestions are nice-to-have.
- **Read the actual code.** Never assume what's in a file — always read it before reporting.
