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
#include "ui/boxes/auto_delete_settings.h"
#include "ui/toast/toast.h"
#include "ui/toasts/common_toasts.h"
#include "ui/text/text_utilities.h"
#include "apiwrap.h"
#include "styles/style_chat.h"

namespace HistoryView::Controls {
namespace {

constexpr auto kToastDuration = crl::time(3500);

} // namespace

void ShowAutoDeleteToast(not_null<PeerData*> peer) {
	const auto period = peer->messagesTTL();
	if (!period) {
		Ui::Toast::Show(tr::lng_ttl_about_tooltip_off(tr::now));
		return;
	}

	const auto duration = (period == 5)
		? u"5 seconds"_q
		: (period < 3 * 86400)
		? tr::lng_ttl_about_duration1(tr::now)
		: tr::lng_ttl_about_duration2(tr::now);
	auto rich = Ui::Text::Bold(
		tr::lng_ttl_about_tooltip_on_title(tr::now, lt_duration, duration)
	).append('\n');

	const auto myPeriod = peer->myMessagesTTL();
	rich.append((period == myPeriod)
		? tr::lng_ttl_about_tooltip(tr::now, lt_duration, duration)
		: (myPeriod
			? tr::lng_ttl_about_tooltip_no_longer
			: tr::lng_ttl_about_tooltip_no_cancel)(
				tr::now,
				lt_user,
				peer->shortName(),
				lt_duration,
				duration));
	Ui::ShowMultilineToast({
		.text = std::move(rich),
		.duration = kToastDuration,
	});
}

void AutoDeleteSettingsBox(
		not_null<Ui::GenericBox*> box,
		not_null<PeerData*> peer) {
	struct State {
		TimeId savingPeriod = 0;
		bool savingOneSide = false;
		mtpRequestId savingRequestId = 0;
		QPointer<Ui::GenericBox> weak;
	};
	const auto state = std::make_shared<State>(State{ .weak = box.get() });
	auto callback = [=](TimeId period, bool oneSide) {
		auto &api = peer->session().api();
		if (state->savingRequestId) {
			if (period == state->savingPeriod
				&& oneSide == state->savingOneSide) {
				return;
			}
			api.request(state->savingRequestId).cancel();
		}
		state->savingPeriod = period;
		state->savingOneSide = oneSide;
		using Flag = MTPmessages_SetHistoryTTL::Flag;
		state->savingRequestId = api.request(MTPmessages_SetHistoryTTL(
			MTP_flags((oneSide && peer->isUser())
				? Flag::f_pm_oneside
				: Flag(0)),
			peer->input,
			MTP_int(period)
		)).done([=](const MTPUpdates &result) {
			peer->session().api().applyUpdates(result);
			ShowAutoDeleteToast(peer);
			if (const auto strong = state->weak.data()) {
				strong->closeBox();
			}
		}).fail([=](const RPCError &error) {
			state->savingRequestId = 0;
		}).send();
	};
	Ui::AutoDeleteSettingsBox(
		box,
		peer->myMessagesTTL(),
		peer->peerMessagesTTL(),
		peer->oneSideTTL(),
		(peer->isUser()
			? std::make_optional(peer->shortName())
			: std::nullopt),
		std::move(callback));
}

TTLButton::TTLButton(not_null<QWidget*> parent, not_null<PeerData*> peer)
: _button(parent, st::historyMessagesTTL) {
	_button.setClickedCallback([=] {
		const auto canEdit = peer->isUser()
			|| (peer->isChat()
				&& peer->asChat()->canDeleteMessages())
			|| (peer->isChannel()
				&& peer->asChannel()->canDeleteMessages());
		if (!canEdit) {
			ShowAutoDeleteToast(peer);
			return;
		}
		Ui::show(
			Box(AutoDeleteSettingsBox, peer),
			Ui::LayerOption(0));
	});
	peer->session().changes().peerFlagsValue(
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
