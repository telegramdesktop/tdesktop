/*
This file is part of Telegram Desktop,
the official desktop application for the Telegram messaging service.

For license and copyright information please follow this link:
https://github.com/telegramdesktop/tdesktop/blob/master/LEGAL
*/
#include "history/view/controls/history_view_ttl_button.h"

#include "data/data_changes.h"
#include "data/data_peer.h"
#include "main/main_session.h"
#include "menu/menu_ttl_validator.h"
#include "ui/text/format_values.h"
#include "ui/text/text_utilities.h"
#include "styles/style_chat.h"

namespace HistoryView::Controls {

TTLButton::TTLButton(
	not_null<Ui::RpWidget*> parent,
	std::shared_ptr<Ui::Show> show,
	not_null<PeerData*> peer)
: _peer(peer)
, _button(parent, st::historyMessagesTTL) {

	const auto validator = TTLMenu::TTLValidator(std::move(show), peer);
	_button.setClickedCallback([=] {
		if (!validator.can()) {
			validator.showToast();
			return;
		}
		validator.showBox();
	});

	peer->session().changes().peerFlagsValue(
		peer,
		Data::PeerUpdate::Flag::MessagesTTL
	) | rpl::start_with_next([=] {
		_button.setText(Ui::FormatTTLTiny(peer->messagesTTL()));
	}, _button.lifetime());
}

void TTLButton::show() {
	_button.show();
}

void TTLButton::hide() {
	_button.hide();
}

void TTLButton::move(int x, int y) {
	_button.move(x, y);
}

int TTLButton::width() const {
	return _button.width();
}

} // namespace HistoryView::Controls
