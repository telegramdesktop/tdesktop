---
description: Prepare changelog, set version, and commit a new release
allowed-tools: Read, Bash, Edit, Grep, AskUserQuestion
---

# Release — Changelog, Set Version, Commit

Full release flow: generate changelog entry, run `set_version`, and commit.

**Arguments:** `$ARGUMENTS` = "$ARGUMENTS"

Parse `$ARGUMENTS` for two optional parts (in any order):
- A **version number** like `6.7` or `6.7.0` — if provided, use it as the new version exactly.
- The word **"beta"** — if present, mark the release as beta.

If no version number is given, auto-increment the patch component (see step 2).

## Steps

### 1. Check git status is clean

Run `git status --porcelain`. If there are any uncommitted changes, **stop** and ask the user to commit or discard them before proceeding. Do not continue until status is clean.

### 2. Read the current changelog

Read `changelog.txt` from the repository root. Note the **latest version number** on the first line (e.g. `6.6.3 beta (12.03.26)`). Parse its major.minor.patch components.

### 3. Determine the new version number

- **If a version was provided in arguments**, use it directly (append `.0` if only major.minor was given).
- **If no version was provided**, auto-increment from the latest changelog version:
  - If it was a beta, and the new release is **not** beta, reuse the same version number but drop "beta".
  - If the new release is beta and the latest was also beta with the same major.minor, bump patch.
  - Otherwise bump the patch component by 1.
- Present the chosen version to the user and ask for confirmation before proceeding. If the user suggests a different version, use that instead.

### 4. Fetch tags and determine the last release tag

Run `git fetch origin --tags` first to ensure all tags from the public repository are available locally. Then run `git tag --sort=-v:refname` and find the most recent `v*` tag. This is the baseline for the diff.

### 5. Collect commits

Run `git log <last-tag>..HEAD --oneline` to get all commits since the last release.

### 6. Write the changelog entry

Analyze every commit message. Group them mentally into features, improvements, and bug fixes. Then produce **brief, user-facing bullet points** following these rules:

- **Style:** Match the existing changelog tone exactly — short, imperative sentences starting with a verb (Fix, Add, Allow, Show, Improve, Support…). Keep the trailing periods (the existing changelog uses them).
- **Brevity:** Each bullet should be one short sentence, around 80 characters when possible. No implementation details. No commit hashes.
- **Selection:** Only include changes that matter to end users. Skip CI, build infra, submodule bumps, code style, refactors, and intermediate WIP commits. Collapse many related commits (e.g. a dozen image-editor commits) into one or two bullets.
- **Ordering:** Features first, then improvements, then bug fixes.
- **Quantity:** Aim for 4-12 bullets total depending on the amount of changes.

### 7. Format and insert into changelog.txt

Use this exact format (date is today in DD.MM.YY):

```
<version> [beta ](DD.MM.YY)

- Bullet one.
- Bullet two.
```

Prepend the new entry at the very top of `changelog.txt`, separated by a blank line from the previous first entry. Use the Edit tool.

### 8. Wait for approval

After writing the entry to `changelog.txt` (step 7), tell the user the changelog has been updated and ask them to review it in the IDE. They can edit it directly and tell you to continue, or tell you what to change in chat. Do **not** print the full entry in chat — the file itself is the review surface.

**Do NOT proceed until the user explicitly approves.**

### 9. Run set_version

Once approved, run the `set_version` script from the repository root. On Windows:

```
.\Telegram\build\set_version.bat <version_arg>
```

Where `<version_arg>` is formatted as the `set_version` script expects:
- Stable: `6.7.0` or `6.7`
- Beta: `6.7.0.beta`

Verify the script exits successfully (exit code 0). If it fails, show the error and stop.

### 10. Commit

Stage all changes and create a commit. The commit message format:

**First line:**
- For stable: `Version <major>.<minor>.` if patch is 0, otherwise `Version <major>.<minor>.<patch>.`
- For beta: `Beta version <major>.<minor>.<patch>.`

**Then an empty line, then the changelog bullets.** Copy bullet lines from the changelog as-is. Only wrap lines that exceed 72 characters; shorter lines must stay on a single line. When wrapping is needed, break at logically correct places (between words/phrases) and indent continuation lines with two spaces.

Example commit message:
```
Beta version 6.6.3.

- Drawing tools in image editor
  (brush, marker, eraser, arrow).
- Draw-to-reply button in media viewer.
- Trim recorded voice messages.
- Fix reorder freeze in chats list.
```

Use a HEREDOC to pass the message to `git commit -a`.

### 11. Done

Run `git log -1` to show the resulting commit and confirm success.
