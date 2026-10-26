# test_open_handoff

[Harness guide](../../README.md) · [Helper index](README.md)

Type: Harness API (may include self-tests).

Source contract: [test_open_handoff.h](../../test_open_handoff.h).

## Use when

Observe document-open handoff without OS launch. Read the contract below and the source header before use.

## Contract and usage

Inspect and assert the document-open branch without handing anything to the
OS.

## Launch classification

`DescribeOpenHandoff()` copies the launch fields from the product
`Core::DetectLaunchType()` result and passes that same value to
`Data::LauncherWouldWarn()`, so it reports the classification
`LaunchWithWarning()` uses instead of re-deriving it:

- `nameType` is the effective type: the name classification, or `Executable`
  or `Unknown` when the file content chose the warning.
- `extension` is the visible lowercase extension of the real path;
  `forcesOpenWith` is derived from it.
- `ipRevealing` marks IP-revealing content. It selects the IP-address warning
  and its global preference only while `ipRevealWarning` is on; otherwise the
  `nameType` warning and its remembered-extension rules apply.
- `byContent` marks a non-IP warning derived from the content. Such a warning
  ignores remembered extensions and offers no remember checkbox.

Content decides only on the all-other platform and only when no MIME glob
matches the file name; elsewhere the result is the name classification plus
the exact legacy MIME check. `OPEN_HANDOFF:` logs each field, `byContent`
included; `OpenHandoffExpected` can assert `nameType` and `launcherWouldWarn`.

## Preconditions and limits

This is an observation, not an execution of `Data::ResolveDocument()`. It does
not show a box or launch anything, so it proves neither the dialog text,
buttons and checkbox nor the launch decision after a click; use the real open
path with the launch fuse ([test_launch_fuse](test_launch_fuse.md)) for those.
The classification performs the product's own MIME lookup, which can read a
bounded header of the file, so do not call the helper inside a launch I/O
measurement window.
