/*
This file is part of Telegram Desktop,
the official desktop application for the Telegram messaging service.

For license and copyright information please follow this link:
https://github.com/telegramdesktop/tdesktop/blob/master/LEGAL
*/
#include "api/api_chat_filters_remove_manager.h"

#include "api/api_chat_filters.h"
#include "apiwrap.h"
#include "data/data_chat_filters.h"
#include "data/data_peer.h"
#include "data/data_session.h"
#include "lang/lang_keys.h"
#include "main/main_session.h"
#include "ui/boxes/confirm_box.h"
#include "ui/ui_utility.h"
#include "window/window_controller.h"
#include "window/window_session_controller.h"
#include "styles/style_layers.h"

namespace Api {
namespace {

void RemoveChatFilter(
		not_null<Main::Session*> session,
		FilterId filterId,
		std::vector<not_null<PeerData*>> leave) {
	const auto api = &session->api();
	session->data().chatsFilters().apply(MTP_updateDialogFilter(
		MTP_flags(MTPDupdateDialogFilter::Flag(0)),
		MTP_int(filterId),
		MTPDialogFilter()));
	if (leave.empty()) {
		api->request(MTPmessages_UpdateDialogFilter(
			MTP_flags(MTPmessages_UpdateDialogFilter::Flag(0)),
			MTP_int(filterId),
			MTPDialogFilter()
		)).send();
	} else {
		api->request(MTPchatlists_LeaveChatlist(
			MTP_inputChatlistDialogFilter(MTP_int(filterId)),
			MTP_vector<MTPInputPeer>(ranges::views::all(
				leave
			) | ranges::views::transform([](not_null<PeerData*> peer) {
				return MTPInputPeer(peer->input);
			}) | ranges::to<QVector<MTPInputPeer>>())
		)).done([=](const MTPUpdates &result) {
			api->applyUpdates(result);
		}).send();
	}
}

} // namespace

RemoveComplexChatFilter::RemoveComplexChatFilter() = default;

void RemoveComplexChatFilter::request(
		QPointer<Ui::RpWidget> widget,
		base::weak_ptr<Window::SessionController> weak,
		FilterId id) {
	const auto session = &weak->session();
	const auto &list = session->data().chatsFilters().list();
	const auto i = ranges::find(list, id, &Data::ChatFilter::id);
	const auto filter = (i != end(list)) ? *i : Data::ChatFilter();
	const auto has = filter.hasMyLinks();
	const auto confirm = [=](Fn<void()> action, bool onlyWhenHas = false) {
		if (!has && onlyWhenHas) {
			action();
			return;
		}
		weak->window().show(Ui::MakeConfirmBox({
			.text = (has
				? tr::lng_filters_delete_sure()
				: tr::lng_filters_remove_sure()),
			.confirmed = [=](Fn<void()> &&close) { close(); action(); },
			.confirmText = (has
				? tr::lng_box_delete()
				: tr::lng_filters_remove_yes()),
			.confirmStyle = &st::attentionBoxButton,
		}));
	};
	const auto simple = [=] {
		confirm([=] { RemoveChatFilter(session, id, {}); });
	};
	const auto suggestRemoving = Api::ExtractSuggestRemoving(filter);
	if (suggestRemoving.empty()) {
		simple();
		return;
	} else if (_removingRequestId) {
		if (_removingId == id) {
			return;
		}
		session->api().request(_removingRequestId).cancel();
	}
	_removingId = id;
	_removingRequestId = session->api().request(
		MTPchatlists_GetLeaveChatlistSuggestions(
			MTP_inputChatlistDialogFilter(
				MTP_int(id)))
	).done(crl::guard(widget, [=, this](const MTPVector<MTPPeer> &result) {
		_removingRequestId = 0;
		const auto suggestRemovePeers = ranges::views::all(
			result.v
		) | ranges::views::transform([=](const MTPPeer &peer) {
			return session->data().peer(peerFromMTP(peer));
		}) | ranges::to_vector;
		const auto chosen = crl::guard(widget, [=](
				std::vector<not_null<PeerData*>> peers) {
			RemoveChatFilter(session, id, std::move(peers));
		});
		confirm(crl::guard(widget, [=] {
			Api::ProcessFilterRemove(
				weak,
				filter.title(),
				filter.iconEmoji(),
				suggestRemoving,
				suggestRemovePeers,
				chosen);
		}), true);
	})).fail(crl::guard(widget, [=, this] {
		_removingRequestId = 0;
		simple();
	})).send();
}

} // namespace Api
