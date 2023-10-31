/*
This file is part of Telegram Desktop,
the official desktop application for the Telegram messaging service.

For license and copyright information please follow this link:
https://github.com/telegramdesktop/tdesktop/blob/master/LEGAL
*/
#pragma once

#include "boxes/peers/edit_participants_box.h"

class PeerData;
class PeerListRow;

namespace Ui {
class PopupMenu;
class Show;
} // namespace Ui

namespace Window {
class SessionNavigation;
} // namespace Window

namespace Giveaway {

class AwardMembersListController : public ParticipantsBoxController {
public:
	AwardMembersListController(
		not_null<Window::SessionNavigation*> navigation,
		not_null<PeerData*> peer);

	void rowClicked(not_null<PeerListRow*> row) override;
	std::unique_ptr<PeerListRow> createRow(
		not_null<PeerData*> participant) const override;
	base::unique_qptr<Ui::PopupMenu> rowContextMenu(
		QWidget *parent,
		not_null<PeerListRow*> row) override;

};

class MyChannelsListController : public PeerListController {
public:
	MyChannelsListController(
		not_null<PeerData*> peer,
		std::shared_ptr<Ui::Show> show,
		std::vector<not_null<PeerData*>> selected);

	Main::Session &session() const override;
	void prepare() override;
	void rowClicked(not_null<PeerListRow*> row) override;

	std::unique_ptr<PeerListRow> createSearchRow(
		not_null<PeerData*> peer) override;
	std::unique_ptr<PeerListRow> createRestoredRow(
		not_null<PeerData*> peer) override;

private:
	std::unique_ptr<PeerListRow> createRow(
		not_null<ChannelData*> channel) const;

	const not_null<PeerData*> _peer;
	const std::shared_ptr<Ui::Show> _show;

	std::vector<not_null<PeerData*>> _selected;

	rpl::lifetime _apiLifetime;

};

} // namespace Giveaway
