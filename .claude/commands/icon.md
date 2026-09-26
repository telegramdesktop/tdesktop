---
description: Generate an SVG icon from a design mockup using vectosolve vectorization
allowed-tools: Read, Write, Edit, Glob, Grep, Bash, Agent, AskUserQuestion, TodoWrite, mcp__vectosolve__vectorize
---

# Icon - SVG Icon Generation from Design Mockup

You generate production-quality SVG icons for Telegram Desktop by vectorizing design mockup screenshots using the vectosolve MCP service, then post-processing the result to match the Telegram icon format.

**Arguments:** `$ARGUMENTS` = "$ARGUMENTS"

If `$ARGUMENTS` is empty, ask the user to describe the icon they want and paste a cropped screenshot of it.

## Overview

The workflow takes a cropped screenshot of an icon from a design mockup (grabbed from the clipboard), vectorizes it via the vectosolve MCP, then post-processes the SVG (recolor to white-on-transparent, restructure to minimal format, set 24x24 output size).

Working directory: `.ai/icon_{name}/` with iterations labeled by letter (`a/`, `b/`, ...), each containing `source.png`. Output SVGs are in the icon root: `a.svg`, `b.svg`, etc.

Follow-ups are supported: `/icon {icon_name} <description>` continues from where the previous run left off.

## Phase 0: Setup

**Record the current time** (using `date` or equivalent) as `$START_TIME`.

### Step 0a: Clipboard grab (MUST be the VERY FIRST action)

If there is an image attached to the user's message:

1. Generate a random 8-character hex string for `HASH` (use `openssl rand -hex 4` or similar).
2. **IMMEDIATELY** — before any other processing — run this Bash command to save the clipboard image:
   ```bash
   HASH=$(openssl rand -hex 4) && if [[ "$OSTYPE" == darwin* ]]; then bash .claude/grab_clipboard.sh ".ai/icon_${HASH}.png"; else powershell -ExecutionPolicy Bypass -File .claude/grab_clipboard.ps1 ".ai/icon_${HASH}.png"; fi
   ```
   On macOS `.claude/grab_clipboard.sh` is used; on Windows `.claude/grab_clipboard.ps1`. Both grab the current clipboard image and save it to the specified path.

3. If the command fails (exit 1 / no image on clipboard):
   - Tell the user: **"Clipboard doesn't contain an image. Please copy the icon area first, then retry."** (On macOS: Cmd+Ctrl+Shift+4 to snip to clipboard; on Windows: Win+Shift+S.)
   - **STOP IMMEDIATELY. Do NOT continue.** You cannot use the image pasted in the conversation — it exists only as pixels in the chat, not as a file you can send to vectosolve. The clipboard grab is the ONLY way to get the image to disk. Do not attempt any workaround.

4. Read back the saved `.ai/icon_HASH.png` using the Read tool.
5. Compare it visually with the image pasted in the conversation. They should depict the same thing.
   - If they look **completely different**: delete `.ai/icon_HASH.png` and fail:
     > "The clipboard image doesn't match what you pasted. Please re-copy and retry."
   - If they look the same (or close enough): proceed. Store the temp path.

If NO image is attached to the message, skip this step entirely.

### Step 0b: Fail-fast — verify vectosolve MCP

Check that the `mcp__vectosolve__vectorize` tool is available by looking at your available tools list. If it is NOT available, fail immediately with:

> vectosolve MCP is not configured. Set it up with:
> ```
> claude mcp add vectosolve --scope user -e VECTOSOLVE_API_KEY=vs_xxx -- npx @vectosolve/mcp
> ```
> Then restart Claude Code.

### Step 0c: Follow-up detection

Extract the first word/token from `$ARGUMENTS` (everything before the first space or newline). Call it `FIRST_TOKEN`.

Run these TWO commands using the Bash tool, **IN PARALLEL**:
1. `ls .ai/` — to see all existing icon project names
2. `ls .ai/icon_{FIRST_TOKEN}/context.md` — to check if this specific icon project exists

**Evaluate the results:**
- If command 2 **succeeds** (context.md exists): this is a **follow-up**. The icon name is `FIRST_TOKEN`. The follow-up description is everything in `$ARGUMENTS` after `FIRST_TOKEN`.
- If command 2 **fails** (not found): this is a **new icon**. The full `$ARGUMENTS` is the icon description.

### Step 0d: New icon setup

1. Parse `$ARGUMENTS` to determine:
   - **Icon description**: what the icon should depict
   - **Icon type**: default is `menu` (24x24 menu/button icon). User may specify otherwise.
   - **Target subfolder**: `menu/` by default, or another subfolder if specified.

2. Choose an icon file name:
   - Lowercase letters and underscores only — **NO hyphens**
   - Match existing naming conventions (check `Telegram/Resources/icons/{subfolder}/`)
   - Must NOT conflict with existing icons
   - Must NOT collide with existing `.ai/icon_{name}/` directories

3. Create `.ai/icon_{name}/` and `.ai/icon_{name}/a/`.

4. Write `.ai/icon_{name}/context.md` with:
   ```
   ## Icon: {icon_name}
   Type: {menu/other}
   Target: Telegram/Resources/icons/{subfolder}/{icon_name}.svg

   ## Original Request
   {full $ARGUMENTS text}

   ## Follow-ups
   (none yet)
   ```

5. Set `LETTER` to `a`.

### Step 0e: Follow-up setup

1. Read `.ai/icon_{name}/context.md` to get the icon type, subfolder, and full history.
2. Find the latest existing letter folder in `.ai/icon_{name}/` (highest letter).
3. Set `LETTER` to the next letter after the latest.
4. Create `.ai/icon_{name}/{LETTER}/`.
5. Update `.ai/icon_{name}/context.md` — append the follow-up description to the `## Follow-ups` section:
   ```
   ### Follow-up (starting at letter {LETTER})
   {follow-up description}
   ```

### Step 0f: Place source image

If a clipboard image was grabbed in Step 0a:
1. Copy (or move) `.ai/icon_HASH.png` → `.ai/icon_{name}/source.png` (overwrite if exists — this is always the latest source).
2. Copy it to `.ai/icon_{name}/{LETTER}/source.png` (archive per-iteration source).
3. Delete the temp `.ai/icon_HASH.png` if it was copied (not moved).

If NO image was grabbed:
- **New icon with no image**: Ask the user to provide a screenshot. STOP.
- **Follow-up with no image**: The existing `source.png` in the icon root carries forward. Copy it to `.ai/icon_{name}/{LETTER}/source.png`. If no source.png exists at all, ask the user for an image.

### Step 0g: Verify renderer

Locate the render tool (`codegen_style` with `--render-svg` mode):

```bash
if [[ "$OSTYPE" == darwin* ]]; then
    ls out/Telegram/codegen/codegen/style/Debug/codegen_style
else
    ls out/Telegram/codegen/codegen/style/Debug/codegen_style.exe
fi
```

If missing, build it: `cmake --build out --config Debug --target codegen_style`

Test on a known good SVG (use the appropriate binary path for the OS):
```bash
CODEGEN=$(if [[ "$OSTYPE" == darwin* ]]; then echo out/Telegram/codegen/codegen/style/Debug/codegen_style; else echo out/Telegram/codegen/codegen/style/Debug/codegen_style.exe; fi)
$CODEGEN --render-svg Telegram/Resources/icons/menu/tag_add.svg .ai/icon_{name}/test_render.png 512
```

If works → delete test render, set `RENDER_AVAILABLE = true`. If fails → `RENDER_AVAILABLE = false`.

## Phase 1: Vectorize & Post-process

### Step 1a: Call vectosolve

Use the `mcp__vectosolve__vectorize` tool with `file_path` set to the **absolute path** of `.ai/icon_{name}/{LETTER}/source.png`.

**If this fails, STOP IMMEDIATELY.** Do NOT try to generate the SVG manually or by any other means. Report the error to the user and let them fix the issue (bad API key, no credits, network error, etc.).

Save the returned SVG content to `.ai/icon_{name}/{LETTER}/raw_vectosolve.svg`.

The MCP tool calls the vectosolve API ($0.20/call). The API key is stored in `~/.claude.json` MCP config (never in the repository).

### Step 1b: Post-process the SVG

The vectosolve SVG will have colors from the mockup, arbitrary dimensions, and possibly a non-square aspect ratio from a non-square screenshot crop. Post-processing fixes this by adjusting the **viewBox** — leave path coordinates untouched.

**Do NOT transform path coordinates.** Vectosolve's paths are correct — the only thing wrong is the framing. All geometry adjustments are done by manipulating the `viewBox` and the `width`/`height` attributes.

#### Sub-step 1: Read the request and determine parameters

Before touching the SVG, determine these from the user's request and context.md:

1. **Output size** (`OUT_W × OUT_H`): default is `24px × 24px` for menu icons. The user may request different dimensions (e.g., 36×36, 48×48, or non-square). Always check the request.
2. **Content padding**: default is ~2px equivalent on each side at the output scale (so content fills roughly (OUT_W-4) × (OUT_H-4)). The user may request different padding or edge-to-edge.
3. **Centering**: default is centered both horizontally and vertically. The user may request specific alignment (e.g., "align to bottom").

#### Sub-step 2: Parse the raw SVG

1. Extract the `viewBox`: `viewBox="VB_X VB_Y VB_W VB_H"` (typically `0 0 W H`).
2. Identify ALL paths. Classify each:
   - **Background**: a rect or path spanning the full viewBox (first path that's a simple rectangle matching the viewBox bounds). **Remove it entirely.**
   - **Content**: the actual icon shapes. **Keep these, paths unchanged.**
3. If paths have `transform="translate(TX,TY)"` attributes, that's fine — keep them as-is. The viewBox framing will work regardless.

#### Sub-step 3: Compute the content bounding box

Estimate the bounding box of the content paths (after removing the background). You can either:
- Eyeball it from the path coordinates (look at first/last M commands and extremes of curves)
- Or for precision, write a quick script to parse the paths and find min/max X/Y

Call the result: `CX_MIN, CY_MIN, CX_MAX, CY_MAX`. Content dimensions: `CW = CX_MAX - CX_MIN`, `CH = CY_MAX - CY_MIN`.

#### Sub-step 4: Compute the new viewBox

The viewBox determines what part of the SVG coordinate space maps to the output rectangle. By expanding the viewBox beyond the content bounds, we add padding. By making the viewBox aspect ratio match the output aspect ratio, we prevent stretching.

1. **Output aspect ratio**: `OUT_AR = OUT_W / OUT_H` (for 24×24 this is 1.0).
2. **Padding in SVG coordinates**: we want ~2px padding at output scale. The scale factor is `OUT_W / VB_CONTENT_W` approximately, so padding in SVG coords = `2 * (CW / (OUT_W - 4))` (or similar — the exact formula depends on which dimension is dominant). Simpler approach: aim for content to occupy ~83% of the viewBox (≈ 20/24), so:
   - `PADDED_W = CW / 0.83`
   - `PADDED_H = CH / 0.83`
3. **Match output aspect ratio**: the viewBox aspect ratio must equal `OUT_AR` to avoid stretching.
   - If `PADDED_W / PADDED_H > OUT_AR`: width is dominant → `VB_W = PADDED_W`, `VB_H = VB_W / OUT_AR`
   - If `PADDED_W / PADDED_H < OUT_AR`: height is dominant → `VB_H = PADDED_H`, `VB_W = VB_H * OUT_AR`
   - If equal: `VB_W = PADDED_W`, `VB_H = PADDED_H`
4. **Center the content** in the new viewBox:
   - `VB_X = CX_MIN - (VB_W - CW) / 2`
   - `VB_Y = CY_MIN - (VB_H - CH) / 2`
   - (Adjust if the user requested non-centered alignment)

The new viewBox is: `viewBox="VB_X VB_Y VB_W VB_H"`.

#### Sub-step 5: Recolor to white-on-transparent

- Replace ALL `fill` color values (anything that isn't `none`) with `#FFFFFF`.
- Remove ALL `stroke` and `stroke-width` attributes entirely.
- Remove `opacity` attributes if present.

#### Sub-step 6: Determine path composition

Look at the icon's visual structure and decide how paths should combine:
- **Outlined shape** (e.g., circle outline with something inside): combine outer + inner cutout into one `<path>` with `fill-rule="evenodd"`.
- **Separate distinct parts** (e.g., magnifying glass + checkmark): keep as separate `<path>` elements.
- **Filled shape with cutout** (e.g., filled circle with checkmark punched out): combine into one path with `fill-rule="evenodd"`.

#### Sub-step 7: Assemble final SVG

```xml
<?xml version="1.0" encoding="UTF-8"?>
<svg width="{OUT_W}px" height="{OUT_H}px" viewBox="{VB_X} {VB_Y} {VB_W} {VB_H}" xmlns="http://www.w3.org/2000/svg">
    <g stroke="none" fill="none" fill-rule="evenodd">
        <path d="..." fill="#FFFFFF"></path>
    </g>
</svg>
```

- `width`/`height` = the output size from the request (default `24px`/`24px`).
- `viewBox` = the computed viewBox from Sub-step 4. The SVG renderer maps this coordinate region to the output size.
- Path `d` attributes are **unchanged** from vectosolve output (just background removed, colors replaced).
- No `<title>`, `id`, `xmlns:xlink`, `version`, `class`, `style`, XML comments, `<metadata>`, or `preserveAspectRatio`.
- No `<circle>`, `<rect>`, `<line>` — only `<path>`.

Write the final SVG to `.ai/icon_{name}/{LETTER}.svg`.

### Step 1c: Render

If `RENDER_AVAILABLE`:
```bash
$CODEGEN --render-svg ".ai/icon_{name}/{LETTER}.svg" ".ai/icon_{name}/render_{LETTER}.png" 512
```

Read the render to visually verify the result.

## Phase 2: Review

After rendering, assess the result:

1. **Recognizable?** The icon should be clearly identifiable as the intended symbol.
2. **Scale reasonable?** Should fill the space appropriately with ~2-3px padding.
3. **Clean lines?** No broken paths, artifacts, or unwanted elements.
4. **Correct colors?** All white on transparent (no leftover colors from the mockup).

If the result looks good → proceed to Phase 3 (Output).

If there are fixable issues (stray element, missed color, etc.) → fix the SVG directly, re-render, and re-check.

If the result is poor (vectosolve couldn't handle the input well) → report to the user and suggest:
- Trying a cleaner/larger crop of the icon
- Providing a different screenshot
- Following up: `/icon {icon_name} <description of what to change>`

## Phase 3: Output

1. Read the `Target:` line from `.ai/icon_{name}/context.md` to get the output path.

2. Copy the final SVG to that target path (e.g., `Telegram/Resources/icons/menu/{icon_name}.svg`).

3. Update `.ai/icon_{name}/context.md` — append to the end:
   ```
   ## Latest Output
   Letter: {LETTER}
   Written to: {target_path}
   ```

4. Report to the user:
   - Final icon file path
   - Number of vectosolve calls made (cost at $0.20/call)
   - Suggest verifying visually
   - Working directory `.ai/icon_{name}/` has all iterations
   - Elapsed time since `$START_TIME` (format `Xm Ys`)
   - Follow-up: `/icon {icon_name} <description of what to change>`

## Text-only Follow-ups (no new image)

When a follow-up has no attached image, the user wants to refine the existing SVG based on text feedback. In this case:

1. Skip Phase 1 (no vectosolve call needed).
2. Read the latest SVG (`.ai/icon_{name}/{prev_letter}.svg`).
3. Read the latest render if available.
4. Apply the user's requested changes by editing the SVG directly.
5. Save as `.ai/icon_{name}/{LETTER}.svg`.
6. Render, review, and output as normal (Phases 1c → 3).

If the changes are too complex for manual SVG editing, suggest the user provide a new screenshot instead.

## Error Handling

- If clipboard grab fails → tell user to re-copy and retry.
- If vectosolve returns an error → report it and suggest a different/cleaner screenshot.
- If vectosolve returns SVG that can't be parsed → save raw output for debugging, report to user.
- If the render helper fails → set `RENDER_AVAILABLE = false`, continue with SVG-only review.
- If post-processing produces a broken SVG → fall back to the raw vectosolve output and do lighter cleanup.
