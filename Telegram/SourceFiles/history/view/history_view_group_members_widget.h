/*
This file is part of Telegram Desktop,
the official desktop application for the Telegram messaging service.

For license and copyright information please follow this link:
https://github.com/telegramdesktop/tdesktop/blob/master/LEGAL
*/
#pragma once

#include "boxes/peer_list_box.h"

namespace Window {
class SessionNavigation;
} // namespace Window

class ParticipantsBoxController;

namespace HistoryView {

class GroupMembersWidget
	: public Ui::RpWidget
	, public PeerListContentDelegate {
public:
	GroupMembersWidget(
		not_null<Ui::RpWidget*> parent,
		not_null<Window::SessionNavigation*> navigation,
		not_null<PeerData*> peer);

private:
	void setupList();

	// PeerListContentDelegate interface.
	void peerListSetTitle(rpl::producer<QString> title) override;
	void peerListSetAdditionalTitle(rpl::producer<QString> title) override;
	bool peerListIsRowChecked(not_null<PeerListRow*> row) override;
	int peerListSelectedRowsCount() override;
	void peerListScrollToTop() override;
	void peerListAddSelectedPeerInBunch(
		not_null<PeerData*> peer) override;
	void peerListAddSelectedRowInBunch(
		not_null<PeerListRow*> row) override;
	void peerListFinishSelectedRowsBunch() override;
	void peerListSetDescription(
		object_ptr<Ui::FlatLabel> description) override;
	std::shared_ptr<Main::SessionShow> peerListUiShow() override;
	void peerListShowRowMenu(
		not_null<PeerListRow*> row,
		bool highlightRow,
		Fn<void(not_null<Ui::PopupMenu*>)> destroyed = nullptr) override;
	// PeerListContentDelegate interface.

	std::shared_ptr<Main::SessionShow> _show;
	object_ptr<PeerListContent> _list = { nullptr };
	std::unique_ptr<ParticipantsBoxController> _listController;

};

} // namespace HistoryView
