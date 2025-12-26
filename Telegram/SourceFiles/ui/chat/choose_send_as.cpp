/*
This file is part of Telegram Desktop,
the official desktop application for the Telegram messaging service.

For license and copyright information please follow this link:
https://github.com/telegramdesktop/tdesktop/blob/master/LEGAL
*/
#include "ui/chat/choose_send_as.h"

#include "boxes/peer_list_box.h"
#include "data/data_group_call.h"
#include "data/data_peer.h"
#include "data/data_channel.h"
#include "data/data_peer_values.h"
#include "history/history.h"
#include "ui/controls/send_as_button.h"
#include "ui/text/text_utilities.h"
#include "ui/painter.h"
#include "window/window_session_controller.h"
#include "main/main_session.h"
#include "main/session/send_as_peers.h"
#include "lang/lang_keys.h"
#include "settings/settings_premium.h"
#include "styles/style_calls.h"
#include "styles/style_chat.h"
#include "styles/style_chat_helpers.h"

namespace Ui {
namespace {

class Row final : public PeerListRow {
public:
	explicit Row(const Main::SendAsPeer &sendAsPeer);

	int paintNameIconGetWidth(
		Painter &p,
		Fn<void()> repaint,
		crl::time now,
		int nameLeft,
		int nameTop,
		int nameWidth,
		int availableWidth,
		int outerWidth,
		bool selected) override;

private:
	bool _premiumRequired = false;

};

class ListController final : public PeerListController {
public:
	ListController(
		std::vector<Main::SendAsPeer> list,
		not_null<PeerData*> selected);

	Main::Session &session() const override;
	void prepare() override;
	void rowClicked(not_null<PeerListRow*> row) override;

	[[nodiscard]] rpl::producer<not_null<PeerData*>> clicked() const;

private:
	std::unique_ptr<Row> createRow(const Main::SendAsPeer &sendAsPeer);

	std::vector<Main::SendAsPeer> _list;
	not_null<PeerData*> _selected;
	rpl::event_stream<not_null<PeerData*>> _clicked;

};

Row::Row(const Main::SendAsPeer &sendAsPeer)
: PeerListRow(sendAsPeer.peer)
, _premiumRequired(sendAsPeer.premiumRequired) {
}

int Row::paintNameIconGetWidth(
		Painter &p,
		Fn<void()> repaint,
		crl::time now,
		int nameLeft,
		int nameTop,
		int nameWidth,
		int availableWidth,
		int outerWidth,
		bool selected) {
	if (_premiumRequired && !peer()->session().premium()) {
		const auto &icon = st::emojiPremiumRequired;
		availableWidth -= icon.width();
		const auto x = nameLeft + std::min(nameWidth, availableWidth);
		icon.paint(p, x, nameTop, outerWidth);
		return icon.width();
	}
	return PeerListRow::paintNameIconGetWidth(
		p,
		std::move(repaint),
		now,
		nameLeft,
		nameTop,
		nameWidth,
		availableWidth,
		outerWidth,
		selected);
}

ListController::ListController(
	std::vector<Main::SendAsPeer> list,
	not_null<PeerData*> selected)
: PeerListController()
, _list(std::move(list))
, _selected(selected) {
	Data::AmPremiumValue(
		&selected->session()
	) | rpl::skip(1) | rpl::on_next([=] {
		const auto count = delegate()->peerListFullRowsCount();
		for (auto i = 0; i != count; ++i) {
			delegate()->peerListUpdateRow(
				delegate()->peerListRowAt(i));
		}
	}, lifetime());
}

Main::Session &ListController::session() const {
	return _selected->session();
}

std::unique_ptr<Row> ListController::createRow(
		const Main::SendAsPeer &sendAsPeer) {
	auto result = std::make_unique<Row>(sendAsPeer);
	if (sendAsPeer.peer->isSelf()) {
		result->setCustomStatus(
			tr::lng_group_call_join_as_personal(tr::now));
	} else if (sendAsPeer.peer->isMegagroup()) {
		result->setCustomStatus(tr::lng_send_as_anonymous_admin(tr::now));
	} else if (const auto channel = sendAsPeer.peer->asChannel()) {
		result->setCustomStatus(tr::lng_chat_status_subscribers(
			tr::now,
			lt_count,
			channel->membersCount()));
	}
	return result;
}

void ListController::prepare() {
	delegate()->peerListSetSearchMode(PeerListSearchMode::Disabled);
	for (const auto &sendAsPeer : _list) {
		auto row = createRow(sendAsPeer);
		const auto raw = row.get();
		delegate()->peerListAppendRow(std::move(row));
		if (sendAsPeer.peer == _selected) {
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
		const style::ChooseSendAs &st,
		std::vector<Main::SendAsPeer> list,
		not_null<PeerData*> chosen,
		Fn<bool(not_null<PeerData*>)> done) {
	Expects(ranges::contains(list, chosen, &Main::SendAsPeer::peer));
	Expects(done != nullptr);

	box->setWidth(st::groupCallJoinAsWidth);
	box->setTitle(tr::lng_send_as_title());
	box->addRow(object_ptr<Ui::FlatLabel>(
		box,
		tr::lng_group_call_join_as_about(),
		st.label));

	auto &lifetime = box->lifetime();
	const auto delegate = lifetime.make_state<
		PeerListContentDelegateSimple
	>();
	const auto controller = lifetime.make_state<ListController>(
		list,
		chosen);
	controller->setStyleOverrides(&st.list, nullptr);

	controller->clicked(
	) | rpl::on_next([=](not_null<PeerData*> peer) {
		const auto weak = base::make_weak(box);
		if (done(peer) && weak) {
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
		const style::ChooseSendAs &st,
		rpl::producer<PeerData*> active,
		std::shared_ptr<ChatHelpers::Show> show) {
	using namespace rpl::mappers;
	const auto current = button->lifetime().make_state<
		rpl::variable<PeerData*>
	>(std::move(active));
	button->setClickedCallback([=, &st] {
		const auto peer = current->current();
		if (!peer) {
			return;
		}
		const auto key = Main::SendAsKey{ peer, Main::SendAsType::Message };
		const auto session = &peer->session();
		const auto &list = session->sendAsPeers().list(key);
		if (list.size() < 2) {
			return;
		}
		const auto done = [=](not_null<PeerData*> sendAs) {
			const auto i = ranges::find(
				list,
				sendAs,
				&Main::SendAsPeer::peer);
			if (i != end(list)
				&& i->premiumRequired
				&& !sendAs->session().premium()) {
				Settings::ShowPremiumPromoToast(
					show,
					tr::lng_send_as_premium_required(
						tr::now,
						lt_link,
						tr::link(
							tr::bold(
								tr::lng_send_as_premium_required_link(
									tr::now))),
						tr::marked),
					u"send_as"_q);
				return false;
			}
			session->sendAsPeers().saveChosen(peer, sendAs);
			return true;
		};
		show->show(Box(
			Ui::ChooseSendAsBox,
			st,
			list,
			session->sendAsPeers().resolveChosen(peer),
			done));
	});

	const auto size = st.button.size;
	auto userpic = current->value(
	) | rpl::filter([=](PeerData *peer) {
		return peer && peer->isChannel();
	}) | rpl::map([=](not_null<PeerData*> peer) {
		const auto channel = peer->asChannel();

		auto updates = rpl::single(
			rpl::empty
		) | rpl::then(channel->session().sendAsPeers().updated(
		) | rpl::filter(
			_1 == Main::SendAsKey(channel, Main::SendAsType::Message)
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
				size * style::DevicePixelRatio());
		}) | rpl::flatten_latest();
	}) | rpl::flatten_latest();

	std::move(
		userpic
	) | rpl::on_next([=](QImage &&userpic) {
		button->setUserpic(std::move(userpic));
	}, button->lifetime());
}

void SetupSendAsButton(
		not_null<SendAsButton*> button,
		const style::ChooseSendAs &st,
		std::shared_ptr<Data::GroupCall> videoStream,
		std::shared_ptr<ChatHelpers::Show> show) {
	const auto peer = videoStream->peer();
	const auto type = Main::SendAsType::VideoStream;
	const auto key = Main::SendAsKey{ peer, type };
	button->setClickedCallback([=, &st] {
		const auto session = &peer->session();
		const auto &list = session->sendAsPeers().list(key);
		if (list.size() < 2) {
			return;
		}
		const auto done = [=](not_null<PeerData*> sendAs) {
			videoStream->saveSendAs(sendAs);
			return true;
		};
		show->show(Box(
			Ui::ChooseSendAsBox,
			st,
			list,
			videoStream->resolveSendAs(),
			done));
	});

	const auto size = st.button.size;
	auto userpic = videoStream->sendAsValue(
	) | rpl::distinct_until_changed(
	) | rpl::map([=](not_null<PeerData*> chosen) {
		return Data::PeerUserpicImageValue(
			chosen,
			size * style::DevicePixelRatio());
	}) | rpl::flatten_latest();

	std::move(
		userpic
	) | rpl::on_next([=](QImage &&userpic) {
		button->setUserpic(std::move(userpic));
	}, button->lifetime());
}

void SetupSendAsButton(
		not_null<SendAsButton*> button,
		const style::ChooseSendAs &st,
		not_null<Window::SessionController*> window) {
	auto active = window->activeChatValue(
	) | rpl::map([=](Dialogs::Key key) {
		return key.history() ? key.history()->peer.get() : nullptr;
	});
	SetupSendAsButton(button, st, std::move(active), window->uiShow());
}

} // namespace Ui
