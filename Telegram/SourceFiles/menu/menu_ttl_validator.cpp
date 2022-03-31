/*
This file is part of Telegram Desktop,
the official desktop application for the Telegram messaging service.

For license and copyright information please follow this link:
https://github.com/telegramdesktop/tdesktop/blob/master/LEGAL
*/
#include "menu/menu_ttl_validator.h"

#include "apiwrap.h"
#include "data/data_channel.h"
#include "data/data_chat.h"
#include "data/data_peer.h"
#include "lang/lang_keys.h"
#include "main/main_session.h"
#include "menu/menu_ttl.h"
#include "ui/layers/generic_box.h"
#include "ui/layers/show.h"
#include "ui/text/text_utilities.h"
#include "ui/toast/toast.h"
#include "ui/toasts/common_toasts.h"
#include "ui/text/format_values.h"
#include "styles/style_chat.h"
#include "styles/style_menu_icons.h"

namespace TTLMenu {
namespace {

constexpr auto kToastDuration = crl::time(3500);

void ShowAutoDeleteToast(
		not_null<QWidget*> parent,
		not_null<PeerData*> peer) {
	const auto period = peer->messagesTTL();
	if (!period) {
		Ui::Toast::Show(parent, tr::lng_ttl_about_tooltip_off(tr::now));
		return;
	}

	const auto duration = (period == 5)
		? u"5 seconds"_q
		: Ui::FormatTTL(period);
	const auto text = peer->isBroadcast()
		? tr::lng_ttl_about_tooltip_channel(tr::now, lt_duration, duration)
		: tr::lng_ttl_about_tooltip(tr::now, lt_duration, duration);
	Ui::ShowMultilineToast({
		.parentOverride = parent,
		.text = { text },
		.duration = kToastDuration,
	});
}

} // namespace

TTLValidator::TTLValidator(
	std::shared_ptr<Ui::Show> show,
	not_null<PeerData*> peer)
: _peer(peer)
, _show(std::move(show)) {
}

Args TTLValidator::createArgs() const {
	const auto peer = _peer;
	const auto show = _show;
	struct State {
		TimeId savingPeriod = 0;
		mtpRequestId savingRequestId = 0;
	};
	const auto state = std::make_shared<State>();
	auto callback = [=, toastParent = show->toastParent()](TimeId period) {
		auto &api = peer->session().api();
		if (state->savingRequestId) {
			if (period == state->savingPeriod) {
				return;
			}
			api.request(state->savingRequestId).cancel();
		}
		state->savingPeriod = period;
		state->savingRequestId = api.request(MTPmessages_SetHistoryTTL(
			peer->input,
			MTP_int(period)
		)).done([=](const MTPUpdates &result) {
			peer->session().api().applyUpdates(result);
			ShowAutoDeleteToast(toastParent, peer);
			state->savingRequestId = 0;
#if 0
			if (const auto strong = state->weak.data()) {
				strong->closeBox();
			}
#endif
		}).fail([=] {
			state->savingRequestId = 0;
		}).send();
	};
	auto about = peer->isUser()
		? tr::lng_ttl_edit_about(lt_user, rpl::single(peer->shortName()))
		: peer->isBroadcast()
		? tr::lng_ttl_edit_about_channel()
		: tr::lng_ttl_edit_about_group();
	const auto ttl = peer->messagesTTL();
	return { std::move(show), ttl, std::move(about), std::move(callback) };
}

bool TTLValidator::can() const {
	return (_peer->isUser()
			&& !_peer->isSelf()
			&& !_peer->isNotificationsUser())
		|| (_peer->isChat()
			&& _peer->asChat()->canDeleteMessages())
		|| (_peer->isChannel()
			&& _peer->asChannel()->canDeleteMessages());
}

void TTLValidator::showToast() const {
	ShowAutoDeleteToast(_show->toastParent(), _peer);
}

const style::icon *TTLValidator::icon() const {
	return &st::menuIconTTL;
}

void TTLValidator::showBox() const {
	_show->showBox(Box(TTLBox, createArgs()));
}

} // namespace TTLMenu
