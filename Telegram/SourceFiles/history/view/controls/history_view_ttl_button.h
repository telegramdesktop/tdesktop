/*
This file is part of Telegram Desktop,
the official desktop application for the Telegram messaging service.

For license and copyright information please follow this link:
https://github.com/telegramdesktop/tdesktop/blob/master/LEGAL
*/
#pragma once

#include "ui/widgets/buttons.h"

namespace Ui {
class GenericBox;
} // namespace Ui

namespace HistoryView::Controls {

void AutoDeleteSettingsBox(
	not_null<Ui::GenericBox*> box,
	not_null<PeerData*> peer);

class TTLButton final {
public:
	TTLButton(not_null<QWidget*> parent, not_null<PeerData*> peer);

	[[nodiscard]] not_null<PeerData*> peer() const {
		return _peer;
	}

	void show();
	void hide();
	void move(int x, int y);

	[[nodiscard]] int width() const;

private:
	const not_null<PeerData*> _peer;
	Ui::IconButton _button;

};

} // namespace HistoryView::Controls
