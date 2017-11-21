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
class InputField;
class CrossButton;
class IconButton;
class FlatLabel;
struct ScrollToRequest;
class AbstractButton;
} // namespace Ui

namespace Profile {
class GroupMembersWidget;
class ParticipantsBoxController;
} // namespace Profile

namespace Info {

class Controller;
enum class Wrap;

namespace Profile {

class Button;
class Memento;
struct MembersState {
	std::unique_ptr<PeerListState> list;
	base::optional<QString> search;
};

class Members
	: public Ui::RpWidget
	, private PeerListContentDelegate {
public:
	Members(
		QWidget *parent,
		not_null<Controller*> controller,
		not_null<PeerData*> peer);

	rpl::producer<Ui::ScrollToRequest> scrollToRequests() const;

	std::unique_ptr<MembersState> saveState();
	void restoreState(std::unique_ptr<MembersState> state);

	int desiredHeight() const;
	rpl::producer<int> onlineCountValue() const;

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

	void peerListAppendRow(
			std::unique_ptr<PeerListRow> row) override {
		PeerListContentDelegate::peerListAppendRow(std::move(row));
		updateSearchEnabledByContent();
	}
	void peerListPrependRow(
			std::unique_ptr<PeerListRow> row) override {
		PeerListContentDelegate::peerListPrependRow(std::move(row));
		updateSearchEnabledByContent();
	}
	void peerListRemoveRow(not_null<PeerListRow*> row) override {
		PeerListContentDelegate::peerListRemoveRow(row);
		updateSearchEnabledByContent();
	}

	void setupHeader();
	object_ptr<Ui::FlatLabel> setupTitle();
	void setupList();

	void setupButtons();
	//void updateSearchOverrides();

	void addMember();
	//void showSearch();
	//void toggleSearch(anim::type animated = anim::type::normal);
	//void cancelSearch();
	//void searchAnimationCallback();
	void updateHeaderControlsGeometry(int newWidth);
	void updateSearchEnabledByContent();

	//Wrap _wrap;
	not_null<Controller*> _controller;
	not_null<PeerData*> _peer;
	std::unique_ptr<PeerListController> _listController;
	object_ptr<Ui::RpWidget> _header = { nullptr };
	object_ptr<ListWidget> _list = { nullptr };

	Button *_openMembers = nullptr;
	Ui::RpWidget *_titleWrap = nullptr;
	Ui::FlatLabel *_title = nullptr;
	Ui::IconButton *_addMember = nullptr;
	//base::unique_qptr<Ui::InputField> _searchField;
	//Ui::IconButton *_search = nullptr;
	//Ui::CrossButton *_cancelSearch = nullptr;

	//Animation _searchShownAnimation;
	//bool _searchShown = false;
	//base::Timer _searchTimer;

	rpl::event_stream<Ui::ScrollToRequest> _scrollToRequests;

};

} // namespace Profile
} // namespace Info
