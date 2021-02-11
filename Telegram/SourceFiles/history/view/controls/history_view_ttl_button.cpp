/*
This file is part of Telegram Desktop,
the official desktop application for the Telegram messaging service.

For license and copyright information please follow this link:
https://github.com/telegramdesktop/tdesktop/blob/master/LEGAL
*/
#include "history/view/controls/history_view_ttl_button.h"

#include "data/data_peer.h"
#include "data/data_chat.h"
#include "data/data_channel.h"
#include "data/data_changes.h"
#include "main/main_session.h"
#include "lang/lang_keys.h"
#include "boxes/peers/edit_peer_info_box.h"
#include "ui/layers/generic_box.h"
#include "ui/toast/toast.h"
#include "apiwrap.h"
#include "styles/style_chat.h"

namespace HistoryView::Controls {

TTLButton::TTLButton(not_null<QWidget*> parent, not_null<PeerData*> peer)
: _button(parent, st::historyMessagesTTL) {
	_button.setClickedCallback([=] {
		const auto canEdit = peer->isUser()
			|| (peer->isChat()
				&& peer->asChat()->canDeleteMessages())
			|| (peer->isChannel()
				&& peer->asChannel()->canDeleteMessages());
		if (!canEdit) {
			const auto duration = (peer->messagesTTL() < 3 * 86400)
				? tr::lng_ttl_about_duration1(tr::now)
				: tr::lng_ttl_about_duration2(tr::now);
			Ui::Toast::Show(tr::lng_ttl_about_tooltip(
				tr::now,
				lt_duration,
				duration));
			return;
		}
		const auto callback = crl::guard(&peer->session(), [=](
			TimeId period) {
			using Flag = MTPmessages_SetHistoryTTL::Flag;
			peer->session().api().request(MTPmessages_SetHistoryTTL(
				MTP_flags(peer->oneSideTTL()
					? Flag::f_pm_oneside
					: Flag(0)),
				peer->input,
				MTP_int(period)
			)).done([=](const MTPUpdates &result) {
				peer->session().api().applyUpdates(result);
			}).fail([=](const RPCError &error) {
			}).send();
		});
		Ui::show(
			Box(
				AutoDeleteSettingsBox,
				peer->myMessagesTTL(),
				callback),
			Ui::LayerOption(0));
	});
	peer->session().changes().peerUpdates(
		peer,
		Data::PeerUpdate::Flag::MessagesTTL
	) | rpl::start_with_next([=] {
		const auto ttl = peer->messagesTTL();
		if (ttl < 3 * 86400) {
			_button.setIconOverride(nullptr, nullptr);
		} else {
			_button.setIconOverride(
				&st::historyMessagesTTL2Icon,
				&st::historyMessagesTTL2IconOver);
		}
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
