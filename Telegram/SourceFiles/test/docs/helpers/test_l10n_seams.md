# test_l10n_seams

[Harness guide](../../README.md) · [Helper index](README.md)

Type: Internal dependency.
Source contract: [test_l10n_seams.h](../../test_l10n_seams.h).

## Use when

Only the localization capture in `test/l10n` (built with
`-D TDESKTOP_L10N_CAPTURE=ON`, see `test/l10n/README.md`) installs these. It
needs to know which phrases a screen read, and it forces limit values from the
app config so that every plural form of a language appears on some screen.
An overlay that wants a temporary translation uses
[test_lang_pack](test_lang_pack.md) instead.

## Preconditions and limits

`NoteLangRead` is called from `Lang::details::Current()`, which every `tr::`
phrase goes through, so the observer sees reads of phrases the screen built,
including ones never painted (accessibility names, hidden rows). Separating
what a screen shows from background reads is the capture's job.

`OverrideAppConfigDouble` is asked by `Main::AppConfig::getDouble()`, which
also serves the integer and limit getters. Values read through other getters
(strings, booleans, arrays) are not overridden.

## Minimal use

```cpp
SetLangReadObserver([](ushort key) { Reads().insert(key); });
SetAppConfigDoubleOverride([](const QString &key) {
	return (key == u"channels_limit_default"_q)
		? std::optional<double>(3)
		: std::nullopt;
});
// ... open screens ...
SetLangReadObserver(nullptr);
SetAppConfigDoubleOverride(nullptr);
```

## Readings and refusals

The observer receives the generated key index; names come from
`lang_auto.indices` of the same build. Plural phrases report their base index
plus the form shift.

## Lifetime and cleanup

Both installers store a plain function pointer and keep it until it is
replaced or cleared with `nullptr`. Clear both in the scenario's finish, or
later reads in the same process keep calling the observer.

## Verification

No self-test. A capture run of any catalog screen reports the keys it read in
`l10n_screens.jsonl`; an empty `reads` list on a screen with text means the
observer was not installed.
