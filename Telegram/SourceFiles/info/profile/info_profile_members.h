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

#include "ui/rp_widget.h"
#include "boxes/peer_list_box.h"

namespace Ui {
class FlatInput;
class CrossButton;
class IconButton;
class FlatLabel;
struct ScrollToRequest;
} // namespace Ui

namespace Profile {
class GroupMembersWidget;
class ParticipantsBoxController;
} // namespace Profile

namespace Info {

enum class Wrap;

namespace Profile {

class Members
	: public Ui::RpWidget
	, private PeerListContentDelegate {
public:
	Members(
		QWidget *parent,
		not_null<Window::Controller*> controller,
		rpl::producer<Wrap> &&wrapValue,
		not_null<PeerData*> peer);

	rpl::producer<Ui::ScrollToRequest> scrollToRequests() const {
		return _scrollToRequests.events();
	}

	int desiredHeight() const;

protected:
	void visibleTopBottomUpdated(
		int visibleTop,
		int visibleBottom) override;
	int resizeGetHeight(int newWidth) override;

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

	object_ptr<Ui::FlatLabel> setupHeader();
	object_ptr<ListWidget> setupList(
		RpWidget *parent,
		not_null<PeerListController*> controller) const;

	void setupButtons();
	void updateSearchOverrides();

	void addMember();
	void showSearch();
	void toggleSearch();
	void cancelSearch();
	void applySearch();
	void forceSearchSubmit();
	void searchAnimationCallback();

	Wrap _wrap;
	not_null<PeerData*> _peer;
	std::unique_ptr<PeerListController> _listController;
	object_ptr<Ui::RpWidget> _labelWrap;
	object_ptr<Ui::FlatLabel> _label;
	object_ptr<Ui::IconButton> _addMember;
	object_ptr<Ui::FlatInput> _searchField;
	object_ptr<Ui::IconButton> _search;
	object_ptr<Ui::CrossButton> _cancelSearch;
	object_ptr<ListWidget> _list;

	Animation _searchShownAnimation;
	bool _searchShown = false;
	base::Timer _searchTimer;

	rpl::event_stream<Ui::ScrollToRequest> _scrollToRequests;

};

} // namespace Profile
} // namespace Info
