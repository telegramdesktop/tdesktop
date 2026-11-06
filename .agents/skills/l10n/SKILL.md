---
name: l10n
description: Run one of the localization flows of the l10n-tdesktop overlay - translate a language from scratch and upload it, bring approved languages up to a new release, check an approved language and propose fixes, or fix layouts that findings need. Use when the user invokes $l10n or /l10n, or asks to translate Telegram Desktop into a language, update translations for a release, or re-check a translation on the platform.
---

# Localization Flows

The flows live in the l10n-tdesktop overlay, next to the tools they run. This
skill finds the overlay, picks the flow the request names and hands over to
the flow's instructions.

## Find the overlay

1. `../l10n-tdesktop` next to this checkout's root, if it has
   `flows/README.md`.
2. Otherwise the path `out/CMakeCache.txt` gives for
   `TDESKTOP_L10N_CAPTURE`.
3. Otherwise stop and ask the user where it is. It has no remote: another
   checkout's clone can be cloned or copied next to this one.

## Run the flow

1. Read `<overlay>/flows/README.md` completely, then the file of the flow the
   request names:
   - a language translated from scratch ("full translation to Greek") -
     `new_language.md`;
   - a release update of the approved languages - `release_update.md`;
   - a check of an approved language with fixes as suggestions - `recheck.md`;
   - layout fixes for findings - `code_fixes.md`.
   A request that fits none, or several, is a question for the user.
2. Before the first step, say in a few lines what will happen: the flow, the
   language, the build and release folder it uses, and whether it ends in an
   upload to the platform. Then check the prerequisites of `flows/README.md`
   and fix or report what is missing.
3. Follow the flow's steps in order. Ask only for decisions that are the
   user's; everything else follows the flow.

## Hard rules

- Run data stays in `.local/l10n/` of this checkout. Never write into a
  sibling checkout.
- Stop or start only this checkout's `out/Debug` or `out/capture_bin`
  Telegram. Never touch other Telegram processes or real user data folders.
- The platform token is never read, printed, put on a command line or given
  to an agent. Writes to the platform need the user's go-ahead for that run;
  a request that names the upload is that go-ahead for the flow's own push.
- Commits to tdesktop follow `AGENTS.md`; nothing is pushed unless the user
  asks.
