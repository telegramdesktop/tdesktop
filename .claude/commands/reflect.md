---
description: Learn from corrections — examine staged vs unstaged diffs and optionally distill insights into AGENTS.md or REVIEW.md
allowed-tools: Read, Edit, Bash(git diff:*), Bash(git status:*), Bash(git log:*), Bash(ls:*), Bash(python3 .agents/skills/process-inbox/scripts/workspace.py:*), Bash(python .agents/skills/process-inbox/scripts/workspace.py:*), Bash(py -3 .agents/skills/process-inbox/scripts/workspace.py:*), AskUserQuestion
---

# Reflect — Learn from Corrections

You are a reflection agent. Your job is to examine the difference between what an AI agent produced (staged changes) and what the user corrected (unstaged changes), and determine whether any **general, reusable insight** can be extracted and added to the project's coding guidelines.

**CRITICAL: Use extended thinking ultrathink for your analysis. This requires deep, careful reasoning.**

## Arguments

`$ARGUMENTS` = "$ARGUMENTS"

If `$ARGUMENTS` is provided, it is a short or full task name from the external
`ai-tdesktop` workflow. Resolve it with the workspace helper and read that
task's durable context before judging the correction.

If `$ARGUMENTS` is empty, skip the task context step — just work from the diffs alone.

## Context

The workflow is:
1. An AI agent implemented something and its changes were staged (`git add`).
2. The user reviewed and corrected the agent's work. These corrections are unstaged.
3. You are now invoked to reflect on what went wrong and whether it reveals a pattern.

## Step 1: Gather the Diffs and Task Context

Run these commands in parallel:

```bash
git diff --cached    # What the agent wrote (staged)
git diff             # What the user corrected (unstaged, on top of staged)
git status           # Which files are involved
```

If either diff is empty, tell the user and stop. Both diffs must be non-empty for reflection to be meaningful.

### Task context (only if `$ARGUMENTS` is non-empty)

Resolve the task and read its context:

1. Run `python3 .agents/skills/process-inbox/scripts/workspace.py resolve --name "$ARGUMENTS"` (use the host's Python 3 command).
2. Read the resolved task's `task.md` and `work/context.md` from the returned AI slot worktree.
3. When `project` is non-null, also read `projects/<project>/project.md`, or
   `projects/archive/<project>/project.md` when the project has been archived.

This helps you distinguish between:
- **Task-specific mistakes** — the agent misunderstood this particular feature's requirements or made a wrong choice within the specific problem. These are NOT documentation-worthy.
- **General convention mistakes** — the agent did something that violates a pattern the codebase follows broadly, regardless of which feature is being implemented. These ARE potentially documentation-worthy.

Having the task context makes this distinction much sharper. Without it, you might mistake a task-specific correction for a general pattern or vice versa.

## Step 2: Read the Current Guidelines

Read both files:
- `AGENTS.md` — development guidelines: build system, coding style, API usage patterns, UI styling, localization, rpl, architectural conventions, "how to do things"
- `REVIEW.md` — mechanical style and formatting rules: brace placement, operator position, type checks, variable initialization, call formatting

Read them carefully. You need to know exactly what's already documented to avoid duplicates and detect contradictions.

## Step 3: Analyze the Corrections

Now think deeply. For each correction the user made, ask yourself:

1. **What did the agent do wrong?** Understand the specific mistake.
2. **Why was it wrong?** Identify the underlying principle.
3. **Is this already covered by AGENTS.md or REVIEW.md?** Check carefully:
   - If the existing rule's scope, title, and examples **clearly cover** this exact scenario and the agent just ignored it — that's not a documentation problem. Skip it.
   - If the existing rule **technically applies** but its scope is too narrow, its examples don't illustrate this usage pattern, or its wording would reasonably lead an agent to think it doesn't apply here — **the rule needs improvement**. Treat this as a potential insight (broaden the scope, add examples, adjust wording). A rule that agents repeatedly violate is an ineffective rule.
4. **Is this specific to this particular task, or is it general?** Most corrections are task-specific ("wrong variable here", "this should call that function instead"). These are NOT documentation-worthy. Only patterns that would apply across many different tasks are worth capturing.
5. **Would documenting this actually help a future agent?** Some things are too context-dependent or too obvious to be useful as a written rule. Be honest about this.

## Step 4: Decision

After analysis, you MUST reach one of these conclusions:

### Conclusion A: No actionable insight

The corrections are purely task-specific, or the existing documentation clearly and specifically covers the exact scenario and the agent simply ignored it. Say what the corrections were and why no doc changes are needed. Then **stop**.

### Conclusion B: New insight found

You can articulate a **concise, general rule** that:
- Applies broadly (not just to this one task)
- Is not already documented
- Would genuinely help a future agent avoid the same class of mistake
- Can be expressed in a few sentences with a clear code example

If you have a new insight, proceed to Step 5.

### Conclusion C: Existing rule needs improvement

A rule already exists in AGENTS.md or REVIEW.md, but its **scope is too narrow**, its **examples don't cover** the pattern the agent encountered, or its **wording** would reasonably lead an agent to think the rule doesn't apply. The agent's mistake is evidence the rule isn't effective.

This is NOT the same as Conclusion A. The test: would a careful agent, reading the existing rule, clearly know it applies to this specific situation? If no — the rule needs to be broadened, its examples expanded, or its title/scope adjusted. Proceed to Step 5.

**Common signs of an ineffective rule:**
- The rule's title or scope restricts it to a context narrower than the actual principle (e.g., "in localization calls" when the pattern applies generally)
- The examples only show one usage pattern, and the agent encountered a different one
- The wording describes *what* to use but not *when* — so agents only apply it in situations that look like the examples

## Step 5: Categorize and Check for Contradictions

### Where does it belong?

- **REVIEW.md** — if it's a mechanical/style rule: formatting, naming, syntax preferences, call structure, brace/operator placement, type usage patterns. Rules that can be checked by looking at code locally without understanding the broader feature.
- **AGENTS.md** — if it's an architectural/behavioral guideline: how to use APIs, where to place code, design patterns, build conventions, module organization, reactive patterns (rpl), localization usage, style system usage. Rules that require understanding the broader context.

### Does it contradict existing content?

Read the target file again carefully. Check if:
1. The new insight **contradicts** an existing rule — if so, do NOT just append or just remove. Instead, use AskUserQuestion to present both the existing rule and the new insight to the user, explain the contradiction, and ask how to reconcile them.
2. The new insight **overlaps** with an existing rule — if so, consider whether the existing rule should be extended/refined rather than adding a separate entry.
3. The new insight is **complementary** — it adds something new without conflicting. This is the simplest case.

## Step 6: Propose the Change

**Do NOT silently edit the files.** First, present your proposed change to the user:

- Quote the exact text you want to add or modify
- Explain which file and where in the file
- Explain why this is general enough to document
- If modifying existing text, show the before and after

Use AskUserQuestion to get the user's approval before making any edit.

Only after the user approves, apply the edit using the Edit tool.

## Rules

- **Keep docs lean and high-signal.** Don't add vague or overly specific rules. But don't default to inaction either — if the user had to manually fix something that a better-worded rule would have prevented, improving that rule is high-signal work.
- **Never dump corrections verbatim.** The goal is distilled principles, not a changelog of mistakes.
- **One insight per reflection, maximum.** If you think you see multiple insights, pick the strongest one. You can always run `/reflect` again next time.
- **Keep the same style.** Match the formatting, tone, and level of detail of the target file. REVIEW.md uses specific before/after code examples. AGENTS.md uses explanatory sections with code snippets.
- **Don't add "don't do X" rules.** Frame rules positively: "do Y" is better than "don't do X." Show the right way, not just the wrong way.
- **No meta-commentary.** Don't add notes like "Added after reflection on..." — the rule should read as if it was always there.
