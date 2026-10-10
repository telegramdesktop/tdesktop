/*
This file is part of Telegram Desktop,
the official desktop application for the Telegram messaging service.

For license and copyright information please follow this link:
https://github.com/telegramdesktop/tdesktop/blob/master/LEGAL
*/
#pragma once

namespace Ui {

class RoundButton;

// The box-footer "this button is working" appearance, in one place.
//
// st::defaultBoxButton and the styles derived from it paint no disabled
// state of their own, so a footer button that is working has to dim its
// own label: the override is that button's own st().textFg at half alpha,
// which is why a button on attentionBoxButton or defaultActiveButton keeps
// dimming its own colour, and the clear is std::nullopt so the style's
// normal and hover colours come back rather than being pinned to one of
// them. The button also stops taking the mouse - but that is not a
// refusal: Enter in a field the box connected to the handler, and a key
// release on a focused button, still reach that handler, so the handler
// itself has to refuse while its flow is in flight.
//
// SetButtonBusy() is that appearance plus setDisabled(), for a button
// whose state comes back. SetButtonDimmed() alone is for one that is
// permanently unavailable and must stay enabled. A null button - one
// whose QPointer the layer teardown has already cleared - is a no-op in
// both.
void SetButtonDimmed(Ui::RoundButton *button, bool dimmed);
void SetButtonBusy(Ui::RoundButton *button, bool busy);

} // namespace Ui
