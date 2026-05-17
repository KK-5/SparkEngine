---
name: auto-commit
description: Use when the user asks to commit changes, save progress, or "提交" the working tree (e.g. "commit this", "提交一下", "save these changes", "把这些改动提交了"). Surveys the working tree, drafts a commit message that matches this repo's style, stages relevant files, creates the commit, and resolves pre-commit hook failures or merge conflicts encountered along the way. Pushes to remote by default unless the user says not to.
---

# auto-commit

End-to-end flow for committing the current working tree to this repository safely. Trigger only when the user has signaled they want to commit — never proactively.

## 1. Survey state (run in parallel)

Run all four in a single batch:

- `git status` — what is tracked / untracked / modified
- `git diff` — unstaged changes
- `git diff --staged` — already-staged changes
- `git log -10 --oneline` — confirm the repo's commit-message style on every run (it can drift)

If `git status` shows a merge / rebase / cherry-pick in progress, jump to **§5 Conflict resolution** before staging anything new.

## 2. Analyze the changes

- Group hunks by intent (feature / fix / refactor / test / docs / chore).
- If changes span multiple unrelated concerns, **ask the user** whether to split into multiple commits before proceeding.
- Flag and exclude likely secrets or local-only files (`.env*`, `*.key`, `credentials.*`, `*.local.json`, build outputs, IDE caches). Warn the user if they appear in `git status` so they can decide.
- **Never commit Claude-related files** (`CLAUDE.md`, `.claude/`, `.claude.json`, etc.). They are local tooling artifacts — exclude silently, do not even ask.
- Skip files the user did not author this session unless they explicitly want them included.

## 3. Draft the commit message

Match the **observed** style from `git log -10 --oneline` rather than imposing a convention:

- This repo currently uses short, lowercase-ish subjects ("render graph builder", "Clean code", "fix: Image view release core dump"). Conventional-commit prefixes appear only on bug fixes (`fix:`). Do not introduce new prefixes.
- Subject ≤ 72 chars, focused on the change's intent.
- Body: only when the change is non-trivial or the *why* is non-obvious. Keep it short and dense — one or two terse lines, no marketing prose. Skip the body entirely for small/cosmetic changes (matches the repo norm).
- **Do NOT append a Co-Authored-By / Claude trailer.** This repo's history has none and the user has explicitly asked to keep it that way.

Pass the message via HEREDOC to preserve formatting:

```bash
git commit -m "$(cat <<'EOF'
<subject>

<optional terse body>
EOF
)"
```

## 4. Stage and commit

- Stage **specific files by name**. Never `git add -A` / `git add .` / `git add -u` — they sweep up files you did not vet.
- Create the commit.
- Run `git status` immediately after to verify the commit landed and the tree is clean (or has the expected leftover unstaged files).
- Push to remote: `git push`. If push fails (non-fast-forward), pull --rebase, resolve conflicts, then push again. Do not force-push unless the user explicitly asks.
- Report the new commit hash + subject back to the user.

## 5. Handling issues

### Pre-commit hook failure

- Hook failure means the commit did **not** happen. Never `--amend` here — `--amend` would modify the previous commit and risk destroying earlier work.
- Read the hook's output. Fix the underlying issue (formatter, linter, missing copyright header, etc.) — do not bypass it.
- Re-stage the fix and create a **new** commit.
- If the same hook fails twice for the same reason, **stop** and ask the user.
- Never use `--no-verify` unless the user explicitly asks for it.

### Merge / rebase / cherry-pick conflicts

- Run `git status` to list conflicted paths.
- Read **the entire** conflicted file before deciding — do not skim the conflict markers.
- For each conflict:
  - If both sides are clearly compatible (e.g. additive changes in different regions), merge them.
  - If one side is obviously stale or already superseded by the other, take the live one — but state which side and why.
  - If the resolution is non-obvious, **stop and ask** the user, showing the conflict region.
- Never run `git checkout --ours` / `--theirs`, `git reset --hard`, or `git rebase --abort` without explicit confirmation — they discard work.
- After resolving, build/test if a quick check exists for the affected area before continuing the merge/rebase.

### Push rejected

- Do **not** force-push. Pull/rebase, resolve conflicts via the flow above, then push again.
- If the user explicitly asks for a force-push, confirm once and use `--force-with-lease`, never bare `--force`. Refuse force-push to `master` / `main` unless re-confirmed.

## 6. Hard rules

- **Push by default** after committing. Only skip push if the user explicitly asks not to.
- **Never `--amend`** a commit — always create a new one if something needs to change after a failed hook.
- **Never `--no-verify`**, `git reset --hard`, `git checkout .`, `git clean -f`, `git branch -D`, or any other destructive op without explicit user instruction in this turn.
- **Never update `git config`** or rewrite history.
- **Never commit `.env` / credential / key files**, even if the user staged them — stop and warn.
- **Never commit Claude-related files** (`CLAUDE.md`, `.claude/`, `.claude.json`). Exclude silently.
- If anything is ambiguous (which files to include, which side of a conflict, whether to split commits), **ask** before acting. The cost of one clarifying question is tiny compared to a bad commit.
