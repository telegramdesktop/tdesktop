/*
This file is part of Telegram Desktop,
the official desktop application for the Telegram messaging service.

For license and copyright information please follow this link:
https://github.com/telegramdesktop/tdesktop/blob/master/LEGAL
*/
#pragma once

#include <rpl/producer.h>
#include "ui/rp_widget.h"
#include "boxes/peer_list_box.h"

namespace Info {

class Controller;

namespace CommonGroups {

class Memento;

class InnerWidget final
	: public Ui::RpWidget
	, private PeerListContentDelegate {
public:
	InnerWidget(
		QWidget *parent,
		not_null<Controller*> controller,
		not_null<UserData*> user);

	not_null<UserData*> user() const {
		return _user;
	}

	rpl::producer<Ui::ScrollToRequest> scrollToRequests() const;

	int desiredHeight() const;

	void saveState(not_null<Memento*> memento);
	void restoreState(not_null<Memento*> memento);

protected:
	void visibleTopBottomUpdated(
		int visibleTop,
		int visibleBottom) override;

private:
	using ListWidget = PeerListContent;

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

	object_ptr<ListWidget> setupList(
		RpWidget *parent,
		not_null<PeerListController*> controller) const;

	not_null<Controller*> _controller;
	not_null<UserData*> _user;
	std::unique_ptr<PeerListController> _listController;
	object_ptr<ListWidget> _list;

	rpl::event_stream<Ui::ScrollToRequest> _scrollToRequests;

};

} // namespace CommonGroups
} // namespace Info

