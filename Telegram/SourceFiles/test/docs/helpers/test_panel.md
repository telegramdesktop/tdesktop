# test_panel

[Harness guide](../../README.md) · [Helper index](README.md)

Type: Harness API (may include self-tests).

Source contract: [test_panel.h](../../test_panel.h).

## Use when

Separate-panel discovery and show-cache readiness. Read the contract below and the source header before use.

## Contract and usage

Distinguish a live `Ui::SeparatePanel` from its faded/squeezed show-animation
cache, and answer which top-level `Ui::SeparatePanel`s exist.
`Test::WalkPanels(&scan, query)` is the one walk eight disposable overlays
each wrote by hand: it enumerates them with a caller-supplied `exclude` of
panels the caller already holds — never a type — and a caller-chosen
`Test::PanelLiveness`, where `Settled` is decided by this module's own
`ReadPanelShowState` and not by `isVisible()`, so a panel still painting its
show cache is not counted as one that opened, while `Shown` is kept beside it
as that reading's control. A mark is simply a previous answer:
`Test::PanelList` is `std::vector<QPointer<QWidget>>`, so it keeps nothing
alive and a panel at a reused address cannot read back as one the mark held,
and `query.before` — or `Test::PanelsAdded`, the same implementation — reads
the difference back as pointers and never as a count. A walk that reached no
`Ui::SeparatePanel` of any kind refuses by name instead of answering a zero,
certified through `Test::DiscriminatingScan` whose control is every panel the
walk reached, so "panels reached, none settled" is a certified zero while "no
panel reached" is not; and `Test::PickPanel` refuses an ambiguous identity
ask, naming every candidate it saw, in three refusal sentences no two of which
read alike. `PanelWalkText` / `PanelListText` are the pure, pollable readings
and `PANEL_WALK` the one reporting row. Identifying a panel from its labels is
`FindAll<Ui::FlatLabel>` + `CheckTextReads` composed at the call site; no
label-join idiom is promoted here. Own self-test over a synthetic top-level
panel it creates, shows and takes down itself.
