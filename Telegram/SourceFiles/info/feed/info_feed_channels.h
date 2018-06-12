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
} // namespace Ui

namespace Info {

class Controller;
enum class Wrap;

namespace Profile {
class Button;
} // namespace Profile

namespace FeedProfile {

class Memento;
struct ChannelsState {
	std::unique_ptr<PeerListState> list;
	base::optional<QString> search;
};

class Channels
	: public Ui::RpWidget
	, private PeerListContentDelegate {
public:
	Channels(
		QWidget *parent,
		not_null<Controller*> controller);

	rpl::producer<Ui::ScrollToRequest> scrollToRequests() const;

	std::unique_ptr<ChannelsState> saveState();
	void restoreState(std::unique_ptr<ChannelsState> state);

	int desiredHeight() const;

protected:
	void visibleTopBottomUpdated(
		int visibleTop,
		int visibleBottom) override;
	int resizeGetHeight(int newWidth) override;

private:
	using ListWidget = PeerListContent;

	// PeerListContentDelegate interface.
	void peerListSetTitle(Fn<QString()> title) override;
	void peerListSetAdditionalTitle(
		Fn<QString()> title) override;
	bool peerListIsRowSelected(not_null<PeerData*> peer) override;
	int peerListSelectedRowsCount() override;
	std::vector<not_null<PeerData*>> peerListCollectSelectedRows() override;
	void peerListScrollToTop() override;
	void peerListAddSelectedRowInBunch(
		not_null<PeerData*> peer) override;
	void peerListFinishSelectedRowsBunch() override;
	void peerListSetDescription(
		object_ptr<Ui::FlatLabel> description) override;

	void setupHeader();
	object_ptr<Ui::FlatLabel> setupTitle();
	void setupList();

	void setupButtons();
	void addChannel();
	void showChannelsWithSearch(bool withSearch);
	void updateHeaderControlsGeometry(int newWidth);

	not_null<Controller*> _controller;
	not_null<Data::Feed*> _feed;
	std::unique_ptr<PeerListController> _listController;
	object_ptr<Ui::RpWidget> _header = { nullptr };
	object_ptr<ListWidget> _list = { nullptr };

	Profile::Button *_openChannels = nullptr;
	Ui::RpWidget *_titleWrap = nullptr;
	Ui::FlatLabel *_title = nullptr;
	Ui::IconButton *_addChannel = nullptr;
	Ui::IconButton *_search = nullptr;

	rpl::event_stream<Ui::ScrollToRequest> _scrollToRequests;

};

} // namespace FeedProfile
} // namespace Info
