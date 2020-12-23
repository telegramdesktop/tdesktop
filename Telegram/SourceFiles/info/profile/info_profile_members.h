/*
This file is part of Telegram Desktop,
the official desktop application for the Telegram messaging service.

For license and copyright information please follow this link:
https://github.com/telegramdesktop/tdesktop/blob/master/LEGAL
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
class SettingsButton;
} // namespace Ui

namespace Info {

class Controller;
enum class Wrap;

namespace Profile {

class Memento;
struct MembersState {
	std::unique_ptr<PeerListState> list;
	std::optional<QString> search;
};

class Members
	: public Ui::RpWidget
	, private PeerListContentDelegate {
public:
	Members(
		QWidget *parent,
		not_null<Controller*> controller);

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

	//void peerListAppendRow(
	//	std::unique_ptr<PeerListRow> row) override {
	//	PeerListContentDelegate::peerListAppendRow(std::move(row));
	//	updateSearchEnabledByContent();
	//}
	//void peerListPrependRow(
	//	std::unique_ptr<PeerListRow> row) override {
	//	PeerListContentDelegate::peerListPrependRow(std::move(row));
	//	updateSearchEnabledByContent();
	//}
	//void peerListRemoveRow(not_null<PeerListRow*> row) override {
	//	PeerListContentDelegate::peerListRemoveRow(row);
	//	updateSearchEnabledByContent();
	//}

	void setupHeader();
	object_ptr<Ui::FlatLabel> setupTitle();
	void setupList();

	void setupButtons();
	//void updateSearchOverrides();

	void addMember();
	void showMembersWithSearch(bool withSearch);
	//void toggleSearch(anim::type animated = anim::type::normal);
	//void cancelSearch();
	//void searchAnimationCallback();
	void updateHeaderControlsGeometry(int newWidth);
	//void updateSearchEnabledByContent();

	//Wrap _wrap;
	not_null<Controller*> _controller;
	not_null<PeerData*> _peer;
	std::unique_ptr<PeerListController> _listController;
	object_ptr<Ui::RpWidget> _header = { nullptr };
	object_ptr<ListWidget> _list = { nullptr };

	Ui::SettingsButton *_openMembers = nullptr;
	Ui::RpWidget *_titleWrap = nullptr;
	Ui::FlatLabel *_title = nullptr;
	Ui::IconButton *_addMember = nullptr;
	//base::unique_qptr<Ui::InputField> _searchField;
	Ui::IconButton *_search = nullptr;
	//Ui::CrossButton *_cancelSearch = nullptr;

	//Ui::Animations::Simple _searchShownAnimation;
	//bool _searchShown = false;
	//base::Timer _searchTimer;

	rpl::event_stream<Ui::ScrollToRequest> _scrollToRequests;

};

} // namespace Profile
} // namespace Info
