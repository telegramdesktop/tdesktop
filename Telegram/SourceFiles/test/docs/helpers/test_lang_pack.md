# test_lang_pack

[Harness guide](../../README.md) · [Helper index](README.md)

Type: Harness API (may include self-tests).

Source contract: [test_lang_pack.h](../../test_lang_pack.h).

## Use when

Temporary language overrides and restoration. Read the contract below and the source header before use.

## Contract and usage

Install synthetic key/value overrides into the RUNNING language pack and take
them back down symmetrically: `Test::InstallLangPack` freezes the pack
identity, the `serialize()` snapshot and one `LangFrozenValue` per key before
applying one `Instance::applyDifference`, whose two `Expects` it holds by
construction, and `LangPackFixture::remove()` restores through `switchToId` +
`fillFromSerialized` and then one EMPTY difference purely to deliver
`Lang::Updated()`, so a key that was default before the install comes back and
an already-painted reactive label re-reads with no interaction. `frozen(key)`
is the only value-returning read, so an expectation taken after the install —
the one that passes vacuously — is not expressible through it; `checkRestored`
compares live against frozen on both the value and the non-default axis and
prints every reading on the passing verdict. `checkInstalled` certifies the
caller's requested override: with no placeholder the expectation is that text
and the reading is `getValue`; with a placeholder the expectation is the
`QString` `Instance::ParseStrings` returns for that requested text — the
encoding `applyValue` stores — and the reading is still `getValue`, so
`getNonDefaultValue` stays only the non-empty axis. A placeholder
`ParseStrings` omits (unknown, not accepted by that key, repeated, or
malformed) is not certified: `checkInstalled` FAILs by name and the verdict
quotes the raw non-default value and whether `getValue` stayed at the frozen
reading. A `#custom` pack, an empty override list, a key `GetKeyIndex` answers
`kKeysCount` for (a plural phrase's base name included), an empty override
value, a `frozen()` miss, an oracle on a fixture that never installed and an
out-of-order `remove()` are seven named refusals rather than silences. With
its own arm-selectable self-test, whose two `LangRestoreFault` arms
deliberately leave one key installed or suppress the notification, and whose
placeholder arm on `lng_dlg_search_from` shows a `{user}` install PASS and a
deliberate `{amount}` install FAIL.
