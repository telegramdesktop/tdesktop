/*
This file is part of Telegram Desktop,
the official desktop application for the Telegram messaging service.

For license and copyright information please follow this link:
https://github.com/telegramdesktop/tdesktop/blob/master/LEGAL
*/
#include "ui/chat/choose_send_as.h"

#include "boxes/peer_list_box.h"
#include "data/data_peer.h"
#include "data/data_channel.h"
#include "data/data_peer_values.h"
#include "history/history.h"
#include "ui/controls/send_as_button.h"
#include "window/window_session_controller.h"
#include "main/main_session.h"
#include "main/session/send_as_peers.h"
#include "lang/lang_keys.h"
#include "styles/style_calls.h"
#include "styles/style_boxes.h"
#include "styles/style_chat.h"

namespace Ui {
namespace {

class ListController final : public PeerListController {
public:
	ListController(
		std::vector<not_null<PeerData*>> list,
		not_null<PeerData*> selected);

	Main::Session &session() const override;
	void prepare() override;
	void rowClicked(not_null<PeerListRow*> row) override;

	[[nodiscard]] rpl::producer<not_null<PeerData*>> clicked() const;

private:
	std::unique_ptr<PeerListRow> createRow(not_null<PeerData*> peer);

	std::vector<not_null<PeerData*>> _list;
	not_null<PeerData*> _selected;
	rpl::event_stream<not_null<PeerData*>> _clicked;

};

ListController::ListController(
	std::vector<not_null<PeerData*>> list,
	not_null<PeerData*> selected)
: PeerListController()
, _list(std::move(list))
, _selected(selected) {
}

Main::Session &ListController::session() const {
	return _selected->session();
}

std::unique_ptr<PeerListRow> ListController::createRow(
		not_null<PeerData*> peer) {
	auto result = std::make_unique<PeerListRow>(peer);
	if (peer->isSelf()) {
		result->setCustomStatus(
			tr::lng_group_call_join_as_personal(tr::now));
	} else if (peer->isMegagroup()) {
		result->setCustomStatus(tr::lng_send_as_anonymous_admin(tr::now));
	} else if (const auto channel = peer->asChannel()) {
		result->setCustomStatus(tr::lng_chat_status_subscribers(
			tr::now,
			lt_count,
			channel->membersCount()));
	}
	return result;
}

void ListController::prepare() {
	delegate()->peerListSetSearchMode(PeerListSearchMode::Disabled);
	for (const auto &peer : _list) {
		auto row = createRow(peer);
		const auto raw = row.get();
		delegate()->peerListAppendRow(std::move(row));
		if (peer == _selected) {
			delegate()->peerListSetRowChecked(raw, true);
			raw->finishCheckedAnimation();
		}
	}
	delegate()->peerListRefreshRows();
}

void ListController::rowClicked(not_null<PeerListRow*> row) {
	const auto peer = row->peer();
	if (peer == _selected) {
		return;
	}
	_clicked.fire_copy(peer);
}

rpl::producer<not_null<PeerData*>> ListController::clicked() const {
	return _clicked.events();
}

} // namespace

void ChooseSendAsBox(
		not_null<GenericBox*> box,
		std::vector<not_null<PeerData*>> list,
		not_null<PeerData*> chosen,
		Fn<void(not_null<PeerData*>)> done) {
	Expects(ranges::contains(list, chosen));
	Expects(done != nullptr);

	box->setWidth(st::groupCallJoinAsWidth);
	box->setTitle(tr::lng_send_as_title());
	const auto &labelSt = st::confirmPhoneAboutLabel;
	box->addRow(object_ptr<Ui::FlatLabel>(
		box,
		tr::lng_group_call_join_as_about(),
		labelSt));

	auto &lifetime = box->lifetime();
	const auto delegate = lifetime.make_state<
		PeerListContentDelegateSimple
	>();
	const auto controller = lifetime.make_state<ListController>(
		list,
		chosen);
	controller->setStyleOverrides(
		&st::peerListJoinAsList,
		nullptr);

	controller->clicked(
	) | rpl::start_with_next([=](not_null<PeerData*> peer) {
		const auto weak = MakeWeak(box);
		done(peer);
		if (weak) {
			box->closeBox();
		}
	}, box->lifetime());

	const auto content = box->addRow(
		object_ptr<PeerListContent>(box, controller),
		style::margins());
	delegate->setContent(content);
	controller->setDelegate(delegate);
	box->addButton(tr::lng_box_done(), [=] { box->closeBox(); });
}

void SetupSendAsButton(
		not_null<SendAsButton*> button,
		rpl::producer<PeerData*> active,
		not_null<Window::SessionController*> window) {
	using namespace rpl::mappers;
	const auto current = button->lifetime().make_state<
		rpl::variable<PeerData*>
	>(std::move(active));
	button->setClickedCallback([=] {
		const auto peer = current->current();
		if (!peer) {
			return;
		}
		const auto session = &peer->session();
		const auto &list = session->sendAsPeers().list(peer);
		if (list.size() < 2) {
			return;
		}
		const auto done = [=](not_null<PeerData*> sendAs) {
			session->sendAsPeers().saveChosen(peer, sendAs);
		};
		window->show(Box(
			Ui::ChooseSendAsBox,
			list,
			session->sendAsPeers().resolveChosen(peer),
			done));
	});

	auto userpic = current->value(
	) | rpl::filter([=](PeerData *peer) {
		return peer && peer->isMegagroup();
	}) | rpl::map([=](not_null<PeerData*> peer) {
		const auto channel = peer->asMegagroup();

		auto updates = rpl::single(
			rpl::empty_value()
		) | rpl::then(channel->session().sendAsPeers().updated(
		) | rpl::filter(
			_1 == channel
		) | rpl::to_empty);

		return rpl::combine(
			std::move(updates),
			channel->adminRightsValue()
		) | rpl::map([=] {
			return channel->session().sendAsPeers().resolveChosen(channel);
		}) | rpl::distinct_until_changed(
		) | rpl::map([=](not_null<PeerData*> chosen) {
			return Data::PeerUserpicImageValue(
				chosen,
				st::sendAsButton.size * style::DevicePixelRatio());
		}) | rpl::flatten_latest();
	}) | rpl::flatten_latest();

	std::move(
		userpic
	) | rpl::start_with_next([=](QImage &&userpic) {
		button->setUserpic(std::move(userpic));
	}, button->lifetime());
}

void SetupSendAsButton(
		not_null<SendAsButton*> button,
		not_null<Window::SessionController*> window) {
	auto active = window->activeChatValue(
	) | rpl::map([=](const Dialogs::Key &key) {
		return key.history() ? key.history()->peer.get() : nullptr;
	});
	SetupSendAsButton(button, std::move(active), window);
}

} // namespace Ui
