/*
This file is part of Telegram Desktop,
the official desktop application for the Telegram messaging service.

For license and copyright information please follow this link:
https://github.com/telegramdesktop/tdesktop/blob/master/LEGAL
*/
#include "info/boosts/giveaway/giveaway_list_controllers.h"

#include "apiwrap.h"
#include "data/data_channel.h"
#include "data/data_peer.h"
#include "data/data_session.h"
#include "data/data_user.h"
#include "lang/lang_keys.h"
#include "main/main_session.h"
#include "ui/boxes/confirm_box.h"
#include "ui/effects/ripple_animation.h"
#include "ui/painter.h"
#include "styles/style_giveaway.h"

namespace Giveaway {
namespace {

class ChannelRow final : public PeerListRow {
public:
	using PeerListRow::PeerListRow;

	QSize rightActionSize() const override;
	QMargins rightActionMargins() const override;
	void rightActionPaint(
		Painter &p,
		int x,
		int y,
		int outerWidth,
		bool selected,
		bool actionSelected) override;

	void rightActionAddRipple(
		QPoint point,
		Fn<void()> updateCallback) override;
	void rightActionStopLastRipple() override;

private:
	std::unique_ptr<Ui::RippleAnimation> _actionRipple;

};

QSize ChannelRow::rightActionSize() const {
	return QSize(
		st::giveawayGiftCodeChannelDeleteIcon.width(),
		st::giveawayGiftCodeChannelDeleteIcon.height()) * 2;
}

QMargins ChannelRow::rightActionMargins() const {
	const auto itemHeight = st::giveawayGiftCodeChannelsPeerList.item.height;
	return QMargins(
		0,
		(itemHeight - rightActionSize().height()) / 2,
		st::giveawayRadioPosition.x() / 2,
		0);
}

void ChannelRow::rightActionPaint(
		Painter &p,
		int x,
		int y,
		int outerWidth,
		bool selected,
		bool actionSelected) {
	if (_actionRipple) {
		_actionRipple->paint(
			p,
			x,
			y,
			outerWidth);
		if (_actionRipple->empty()) {
			_actionRipple.reset();
		}
	}
	const auto rect = QRect(QPoint(x, y), ChannelRow::rightActionSize());
	(actionSelected
		? st::giveawayGiftCodeChannelDeleteIconOver
		: st::giveawayGiftCodeChannelDeleteIcon).paintInCenter(p, rect);
}

void ChannelRow::rightActionAddRipple(
		QPoint point,
		Fn<void()> updateCallback) {
	if (!_actionRipple) {
		auto mask = Ui::RippleAnimation::EllipseMask(rightActionSize());
		_actionRipple = std::make_unique<Ui::RippleAnimation>(
			st::defaultRippleAnimation,
			std::move(mask),
			std::move(updateCallback));
	}
	_actionRipple->add(point);
}

void ChannelRow::rightActionStopLastRipple() {
	if (_actionRipple) {
		_actionRipple->lastStop();
	}
}

} // namespace

AwardMembersListController::AwardMembersListController(
	not_null<Window::SessionNavigation*> navigation,
	not_null<PeerData*> peer)
: ParticipantsBoxController(navigation, peer, ParticipantsRole::Members) {
}

void AwardMembersListController::rowClicked(not_null<PeerListRow*> row) {
	const auto checked = !row->checked();
	if (checked
		&& _checkErrorCallback
		&& _checkErrorCallback(delegate()->peerListSelectedRowsCount())) {
		return;
	}
	delegate()->peerListSetRowChecked(row, checked);
}

std::unique_ptr<PeerListRow> AwardMembersListController::createRow(
		not_null<PeerData*> participant) const {
	const auto user = participant->asUser();
	if (!user || user->isInaccessible() || user->isBot() || user->isSelf()) {
		return nullptr;
	}
	return std::make_unique<PeerListRow>(participant);
}

base::unique_qptr<Ui::PopupMenu> AwardMembersListController::rowContextMenu(
		QWidget *parent,
		not_null<PeerListRow*> row) {
	return nullptr;
}

void AwardMembersListController::setCheckError(Fn<bool(int)> callback) {
	_checkErrorCallback = std::move(callback);
}

MyChannelsListController::MyChannelsListController(
	not_null<PeerData*> peer,
	std::shared_ptr<Ui::Show> show,
	std::vector<not_null<PeerData*>> selected)
: PeerListController(
	std::make_unique<PeerListGlobalSearchController>(&peer->session()))
, _peer(peer)
, _show(show)
, _selected(std::move(selected)) {
}

std::unique_ptr<PeerListRow> MyChannelsListController::createSearchRow(
		not_null<PeerData*> peer) {
	if (const auto channel = peer->asChannel()) {
		return createRow(channel);
	}
	return nullptr;
}

std::unique_ptr<PeerListRow> MyChannelsListController::createRestoredRow(
		not_null<PeerData*> peer) {
	if (const auto channel = peer->asChannel()) {
		return createRow(channel);
	}
	return nullptr;
}

void MyChannelsListController::rowClicked(not_null<PeerListRow*> row) {
	const auto channel = row->peer()->asChannel();
	const auto checked = !row->checked();
	if (checked
		&& _checkErrorCallback
		&& _checkErrorCallback(delegate()->peerListSelectedRowsCount())) {
		return;
	}
	if (checked && channel && channel->username().isEmpty()) {
		_show->showBox(Box(Ui::ConfirmBox, Ui::ConfirmBoxArgs{
			.text = tr::lng_giveaway_channels_confirm_about(),
			.confirmed = [=](Fn<void()> close) {
				delegate()->peerListSetRowChecked(row, checked);
				close();
			},
			.confirmText = tr::lng_filters_recommended_add(),
			.title = tr::lng_giveaway_channels_confirm_title(),
		}));
	} else {
		delegate()->peerListSetRowChecked(row, checked);
	}
}

Main::Session &MyChannelsListController::session() const {
	return _peer->session();
}

void MyChannelsListController::prepare() {
	delegate()->peerListSetSearchMode(PeerListSearchMode::Enabled);
	const auto api = _apiLifetime.make_state<MTP::Sender>(
		&session().api().instance());
	api->request(
		MTPstories_GetChatsToSend()
	).done([=](const MTPmessages_Chats &result) {
		_apiLifetime.destroy();
		const auto &chats = result.match([](const auto &data) {
			return data.vchats().v;
		});
		auto &owner = session().data();
		for (const auto &chat : chats) {
			if (const auto peer = owner.processChat(chat)) {
				if (!peer->isChannel() || (peer == _peer)) {
					continue;
				}
				if (!delegate()->peerListFindRow(peer->id.value)) {
					if (const auto channel = peer->asChannel()) {
						auto row = createRow(channel);
						const auto raw = row.get();
						delegate()->peerListAppendRow(std::move(row));
						if (ranges::contains(_selected, peer)) {
							delegate()->peerListSetRowChecked(raw, true);
							_selected.erase(
								ranges::remove(_selected, peer),
								end(_selected));
						}
					}
				}
			}
		}
		for (const auto &selected : _selected) {
			if (const auto channel = selected->asChannel()) {
				auto row = createRow(channel);
				const auto raw = row.get();
				delegate()->peerListAppendRow(std::move(row));
				delegate()->peerListSetRowChecked(raw, true);
			}
		}
		delegate()->peerListRefreshRows();
		_selected.clear();
	}).send();
}

void MyChannelsListController::setCheckError(Fn<bool(int)> callback) {
	_checkErrorCallback = std::move(callback);
}

std::unique_ptr<PeerListRow> MyChannelsListController::createRow(
		not_null<ChannelData*> channel) const {
	if (channel->isMegagroup()) {
		return nullptr;
	}
	auto row = std::make_unique<PeerListRow>(channel);
	row->setCustomStatus(tr::lng_chat_status_subscribers(
		tr::now,
		lt_count,
		channel->membersCount()));
	return row;
}

SelectedChannelsListController::SelectedChannelsListController(
	not_null<PeerData*> peer)
: _peer(peer) {
	PeerListController::setStyleOverrides(
		&st::giveawayGiftCodeChannelsPeerList);
}

void SelectedChannelsListController::setTopStatus(rpl::producer<QString> s) {
	_statusLifetime = std::move(
		s
	) | rpl::start_with_next([=](const QString &t) {
		if (delegate()->peerListFullRowsCount() > 0) {
			delegate()->peerListRowAt(0)->setCustomStatus(t);
		}
	});
}

void SelectedChannelsListController::rebuild(
		std::vector<not_null<PeerData*>> selected) {
	while (delegate()->peerListFullRowsCount() > 1) {
		delegate()->peerListRemoveRow(delegate()->peerListRowAt(1));
	}
	for (const auto &peer : selected) {
		delegate()->peerListAppendRow(createRow(peer->asChannel()));
	}
	delegate()->peerListRefreshRows();
}

auto SelectedChannelsListController::channelRemoved() const
-> rpl::producer<not_null<PeerData*>> {
	return _channelRemoved.events();
}

void SelectedChannelsListController::rowClicked(not_null<PeerListRow*> row) {
}

void SelectedChannelsListController::rowRightActionClicked(
		not_null<PeerListRow*> row) {
	const auto peer = row->peer();
	delegate()->peerListRemoveRow(row);
	delegate()->peerListRefreshRows();
	_channelRemoved.fire_copy(peer);
}

Main::Session &SelectedChannelsListController::session() const {
	return _peer->session();
}

void SelectedChannelsListController::prepare() {
	delegate()->peerListAppendRow(createRow(_peer->asChannel()));
}

std::unique_ptr<PeerListRow> SelectedChannelsListController::createRow(
		not_null<ChannelData*> channel) const {
	if (channel->isMegagroup()) {
		return nullptr;
	}
	const auto isYourChannel = (_peer->asChannel() == channel);
	auto row = isYourChannel
		? std::make_unique<PeerListRow>(channel)
		: std::make_unique<ChannelRow>(channel);
	row->setCustomStatus(isYourChannel
		? QString()
		: tr::lng_chat_status_subscribers(
			tr::now,
			lt_count,
			channel->membersCount()));
	return row;
}

} // namespace Giveaway
