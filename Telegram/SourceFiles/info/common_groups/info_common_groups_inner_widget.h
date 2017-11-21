/*
This file is part of Telegram Desktop,
the official desktop version of Telegram messaging app, see https://telegram.org

Telegram Desktop is free software: you can redistribute it and/or modify
it under the terms of the GNU General Public License as published by
the Free Software Foundation, either version 3 of the License, or
(at your option) any later version.

It is distributed in the hope that it will be useful,
but WITHOUT ANY WARRANTY; without even the implied warranty of
MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE. See the
GNU General Public License for more details.

In addition, as a special exception, the copyright holders give permission
to link the code of portions of this program with the OpenSSL library.

Full license: https://github.com/telegramdesktop/tdesktop/blob/master/LICENSE
Copyright (c) 2014-2017 John Preston, https://desktop.telegram.org
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
	void peerListSetTitle(base::lambda<QString()> title) override;
	void peerListSetAdditionalTitle(
		base::lambda<QString()> title) override;
	bool peerListIsRowSelected(not_null<PeerData*> peer) override;
	int peerListSelectedRowsCount() override;
	std::vector<not_null<PeerData*>> peerListCollectSelectedRows() override;
	void peerListScrollToTop() override;
	void peerListAddSelectedRowInBunch(
		not_null<PeerData*> peer) override;
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

