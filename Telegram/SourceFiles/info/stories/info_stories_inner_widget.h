/*
This file is part of Telegram Desktop,
the official desktop application for the Telegram messaging service.

For license and copyright information please follow this link:
https://github.com/telegramdesktop/tdesktop/blob/master/LEGAL
*/
#pragma once

#include "base/unique_qptr.h"
#include "data/data_stories.h"
#include "info/stories/info_stories_common.h"
#include "ui/rp_widget.h"
#include "ui/widgets/scroll_area.h"

namespace Data {
struct StoryAlbum;
} // namespace Data

namespace Ui {
class SubTabs;
class PopupMenu;
class VerticalLayout;
class MultiSlideTracker;
struct SubTabsReorderUpdate;
} // namespace Ui

namespace Info {
class Controller;
struct SelectedItems;
enum class SelectionAction;
} // namespace Info

namespace Info::Media {
class ListWidget;
} // namespace Info::Media

namespace Window {
class SessionNavigation;
} // namespace Window

namespace MTP {
class Sender;
} // namespace MTP

namespace Info::Stories {

class Memento;
class EmptyWidget;

class InnerWidget final : public Ui::RpWidget {
public:
	InnerWidget(
		QWidget *parent,
		not_null<Controller*> controller,
		rpl::producer<int> albumId,
		int addingToAlbumId = 0);
	~InnerWidget();

	bool showInternal(not_null<Memento*> memento);
	void setIsStackBottom(bool isStackBottom) {
		_isStackBottom = isStackBottom;
		setupTop();
	}

	void saveState(not_null<Memento*> memento);
	void restoreState(not_null<Memento*> memento);

	void setScrollHeightValue(rpl::producer<int> value);

	rpl::producer<Ui::ScrollToRequest> scrollToRequests() const;
	rpl::producer<SelectedItems> selectedListValue() const;
	void selectionAction(SelectionAction action);

	void reload();
	void editAlbumStories(int id);
	void shareAlbumLink(const QString &username, int id);
	void editAlbumName(int id);
	void confirmDeleteAlbum(int id);
	void albumAdded(Data::StoryAlbum result);

	[[nodiscard]] rpl::producer<bool> albumEmptyValue() const {
		return _albumEmpty.value();
	}

	[[nodiscard]] rpl::producer<int> albumIdChanges() const;
	[[nodiscard]] rpl::producer<Data::StoryAlbumUpdate> changes() const;

protected:
	int resizeGetHeight(int newWidth) override;
	void visibleTopBottomUpdated(
		int visibleTop,
		int visibleBottom) override;

private:
	int recountHeight();
	void refreshHeight();
	void refreshEmpty();
	void preloadArchiveCount();

	void setupTop();
	void setupList();
	void setupEmpty();
	void setupAlbums();
	void createButtons();
	void createProfileTop();
	void createAboutArchive();

	void startTop();
	void addArchiveButton(Ui::MultiSlideTracker &tracker);
	void addRecentButton(Ui::MultiSlideTracker &tracker);
	void addGiftsButton(Ui::MultiSlideTracker &tracker);
	void finalizeTop();

	void refreshAlbumsTabs();
	void showMenuForAlbum(int id);

	void albumRenamed(int id, QString name);
	void albumRemoved(int id);

	void reorderAlbumsLocally(const Ui::SubTabsReorderUpdate &update);
	void flushAlbumReorder();

	const not_null<Controller*> _controller;
	const not_null<PeerData*> _peer;
	const int _addingToAlbumId = 0;

	std::vector<Data::StoryAlbum> _albums;
	rpl::variable<int> _albumId;
	rpl::event_stream<int> _albumIdChanges;
	Ui::RpWidget *_albumsWrap = nullptr;
	std::unique_ptr<Ui::SubTabs> _albumsTabs;
	rpl::variable<Data::StoryAlbumUpdate> _albumChanges;
	bool _pendingAlbumReorder = false;

	base::unique_qptr<Ui::PopupMenu> _menu;
	std::unique_ptr<MTP::Sender> _api;
	mtpRequestId _reorderRequestId = 0;

	object_ptr<Ui::VerticalLayout> _top = { nullptr };
	object_ptr<Media::ListWidget> _list = { nullptr };
	object_ptr<Ui::RpWidget> _empty = { nullptr };
	int _lastNonLoadingHeight = 0;
	bool _emptyLoading = false;

	bool _inResize = false;
	bool _isStackBottom = false;

	rpl::event_stream<Ui::ScrollToRequest> _scrollToRequests;
	rpl::event_stream<rpl::producer<SelectedItems>> _selectedLists;
	rpl::event_stream<rpl::producer<int>> _listTops;
	rpl::variable<int> _topHeight;
	rpl::variable<bool> _albumEmpty;

};

} // namespace Info::Stories
