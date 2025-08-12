/*
This file is part of Telegram Desktop,
the official desktop application for the Telegram messaging service.

For license and copyright information please follow this link:
https://github.com/telegramdesktop/tdesktop/blob/master/LEGAL
*/
#pragma once

#include "info/info_wrap_widget.h"
#include "info/statistics/info_statistics_tag.h"
#include "ui/controls/swipe_handler_data.h"

namespace Api {
struct WhoReadList;
} // namespace Api

namespace Dialogs::Stories {
struct Content;
} // namespace Dialogs::Stories

namespace Storage {
enum class SharedMediaType : signed char;
} // namespace Storage

namespace Ui {
namespace Controls {
struct SwipeHandlerArgs;
} // namespace Controls
class RoundRect;
class ScrollArea;
class InputField;
struct ScrollToRequest;
template <typename Widget>
class PaddingWrap;
} // namespace Ui

namespace Ui::Menu {
struct MenuCallback;
} // namespace Ui::Menu

namespace Info::Settings {
struct Tag;
} // namespace Info::Settings

namespace Info::Downloads {
struct Tag;
} // namespace Info::Downloads

namespace Info::Statistics {
struct Tag;
} // namespace Info::Statistics

namespace Info::BotStarRef {
enum class Type : uchar;
struct Tag;
} // namespace Info::BotStarRef

namespace Info::GlobalMedia {
struct Tag;
} // namespace Info::GlobalMedia

namespace Info::PeerGifts {
struct Tag;
} // namespace Info::PeerGifts

namespace Info::Stories {
struct Tag;
} // namespace Info::Stories

namespace Info::Saved {
struct MusicTag;
} // namespace Info::Saved

namespace Info {

class ContentMemento;
class Controller;

class ContentWidget : public Ui::RpWidget {
public:
	ContentWidget(
		QWidget *parent,
		not_null<Controller*> controller);

	virtual bool showInternal(
		not_null<ContentMemento*> memento) = 0;
	std::shared_ptr<ContentMemento> createMemento();

	virtual void setIsStackBottom(bool isStackBottom);
	[[nodiscard]] bool isStackBottom() const;

	rpl::producer<int> scrollHeightValue() const;
	rpl::producer<int> desiredHeightValue() const override;
	virtual rpl::producer<bool> desiredShadowVisibility() const;
	bool hasTopBarShadow() const;

	virtual void setInnerFocus();
	virtual void showFinished() {
	}
	virtual void enableBackButton() {
	}

	// When resizing the widget with top edge moved up or down and we
	// want to add this top movement to the scroll position, so inner
	// content will not move.
	void setGeometryWithTopMoved(
		const QRect &newGeometry,
		int topDelta);
	void applyAdditionalScroll(int additionalScroll);
	void applyMaxVisibleHeight(int maxVisibleHeight);
	int scrollTillBottom(int forHeight) const;
	[[nodiscard]] rpl::producer<int> scrollTillBottomChanges() const;
	[[nodiscard]] virtual const Ui::RoundRect *bottomSkipRounding() const {
		return nullptr;
	}

	// Float player interface.
	bool floatPlayerHandleWheelEvent(QEvent *e);
	QRect floatPlayerAvailableRect() const;

	virtual rpl::producer<SelectedItems> selectedListValue() const;
	virtual void selectionAction(SelectionAction action) {
	}
	virtual void fillTopBarMenu(const Ui::Menu::MenuCallback &addAction);

	[[nodiscard]] virtual bool closeByOutsideClick() const {
		return true;
	}
	virtual void checkBeforeClose(Fn<void()> close) {
		close();
	}
	virtual void checkBeforeCloseByEscape(Fn<void()> close);
	[[nodiscard]] virtual rpl::producer<QString> title() = 0;
	[[nodiscard]] virtual rpl::producer<QString> subtitle() {
		return nullptr;
	}
	[[nodiscard]] virtual auto titleStories()
		-> rpl::producer<Dialogs::Stories::Content>;

	virtual void saveChanges(FnMut<void()> done);

	[[nodiscard]] int scrollBottomSkip() const;
	[[nodiscard]] rpl::producer<int> scrollBottomSkipValue() const;
	[[nodiscard]] virtual auto desiredBottomShadowVisibility()
		-> rpl::producer<bool>;

	void replaceSwipeHandler(Ui::Controls::SwipeHandlerArgs *incompleteArgs);

protected:
	template <typename Widget>
	Widget *setInnerWidget(object_ptr<Widget> inner) {
		return static_cast<Widget*>(
			doSetInnerWidget(std::move(inner)));
	}

	[[nodiscard]] not_null<Controller*> controller() const {
		return _controller;
	}
	[[nodiscard]] not_null<Ui::ScrollArea*> scroll() const;
	[[nodiscard]] int maxVisibleHeight() const {
		return _maxVisibleHeight;
	}

	void resizeEvent(QResizeEvent *e) override;
	void paintEvent(QPaintEvent *e) override;

	void setScrollTopSkip(int scrollTopSkip);
	void setScrollBottomSkip(int scrollBottomSkip);
	int scrollTopSave() const;
	void scrollTopRestore(int scrollTop);
	void scrollTo(const Ui::ScrollToRequest &request);
	[[nodiscard]] rpl::producer<int> scrollTopValue() const;

	void setPaintPadding(const style::margins &padding);

	void setViewport(rpl::producer<not_null<QEvent*>> &&events) const;

private:
	RpWidget *doSetInnerWidget(object_ptr<RpWidget> inner);
	void updateControlsGeometry();
	void refreshSearchField(bool shown);
	void setupSwipeHandler(not_null<Ui::RpWidget*> widget);
	void updateInnerPadding();

	virtual std::shared_ptr<ContentMemento> doCreateMemento() = 0;

	const not_null<Controller*> _controller;

	style::color _bg;
	rpl::variable<int> _scrollTopSkip = -1;
	rpl::variable<int> _scrollBottomSkip = 0;
	rpl::event_stream<int> _scrollTillBottomChanges;
	object_ptr<Ui::ScrollArea> _scroll;
	Ui::PaddingWrap<Ui::RpWidget> *_innerWrap = nullptr;
	base::unique_qptr<Ui::RpWidget> _searchWrap = nullptr;
	QPointer<Ui::InputField> _searchField;
	int _innerDesiredHeight = 0;
	int _additionalScroll = 0;
	int _addedHeight = 0;
	int _maxVisibleHeight = 0;
	bool _isStackBottom = false;

	// Saving here topDelta in setGeometryWithTopMoved() to get it passed to resizeEvent().
	int _topDelta = 0;

	// To paint round edges from content.
	style::margins _paintPadding;

	Ui::Controls::SwipeBackResult _swipeBackData;
	rpl::lifetime _swipeHandlerLifetime;

};

class ContentMemento {
public:
	ContentMemento(
		not_null<PeerData*> peer,
		Data::ForumTopic *topic,
		Data::SavedSublist *sublist,
		PeerId migratedPeerId);
	explicit ContentMemento(PeerGifts::Tag gifts);
	explicit ContentMemento(Settings::Tag settings);
	explicit ContentMemento(Downloads::Tag downloads);
	explicit ContentMemento(Stories::Tag stories);
	explicit ContentMemento(Saved::MusicTag music);
	explicit ContentMemento(Statistics::Tag statistics);
	explicit ContentMemento(BotStarRef::Tag starref);
	explicit ContentMemento(GlobalMedia::Tag global);
	ContentMemento(not_null<PollData*> poll, FullMsgId contextId)
	: _poll(poll)
	, _pollReactionsContextId(contextId) {
	}
	ContentMemento(
		std::shared_ptr<Api::WhoReadList> whoReadIds,
		FullMsgId contextId,
		Data::ReactionId selected);
	virtual ~ContentMemento() = default;

	[[nodiscard]] virtual object_ptr<ContentWidget> createWidget(
		QWidget *parent,
		not_null<Controller*> controller,
		const QRect &geometry) = 0;

	[[nodiscard]] PeerData *peer() const {
		return _peer;
	}
	[[nodiscard]] PeerId migratedPeerId() const {
		return _migratedPeerId;
	}
	[[nodiscard]] Data::ForumTopic *topic() const {
		return _topic;
	}
	[[nodiscard]] Data::SavedSublist *sublist() const {
		return _sublist;
	}
	[[nodiscard]] UserData *settingsSelf() const {
		return _settingsSelf;
	}
	[[nodiscard]] PeerData *storiesPeer() const {
		return _storiesPeer;
	}
	[[nodiscard]] int storiesAlbumId() const {
		return _storiesAlbumId;
	}
	[[nodiscard]] int storiesAddToAlbumId() const {
		return _storiesAddToAlbumId;
	}
	[[nodiscard]] PeerData *musicPeer() const {
		return _musicPeer;
	}
	[[nodiscard]] PeerData *giftsPeer() const {
		return _giftsPeer;
	}
	[[nodiscard]] int giftsCollectionId() const {
		return _giftsCollectionId;
	}
	[[nodiscard]] Statistics::Tag statisticsTag() const {
		return _statisticsTag;
	}
	[[nodiscard]] PeerData *starrefPeer() const {
		return _starrefPeer;
	}
	[[nodiscard]] BotStarRef::Type starrefType() const {
		return _starrefType;
	}
	[[nodiscard]] PollData *poll() const {
		return _poll;
	}
	[[nodiscard]] FullMsgId pollContextId() const {
		return _poll ? _pollReactionsContextId : FullMsgId();
	}
	[[nodiscard]] auto reactionsWhoReadIds() const
	-> std::shared_ptr<Api::WhoReadList> {
		return _reactionsWhoReadIds;
	}
	[[nodiscard]] Data::ReactionId reactionsSelected() const {
		return _reactionsSelected;
	}
	[[nodiscard]] FullMsgId reactionsContextId() const {
		return _reactionsWhoReadIds ? _pollReactionsContextId : FullMsgId();
	}
	[[nodiscard]] UserData *globalMediaSelf() const {
		return _globalMediaSelf;
	}
	[[nodiscard]] Key key() const;

	[[nodiscard]] virtual Section section() const = 0;

	void setScrollTop(int scrollTop) {
		_scrollTop = scrollTop;
	}
	int scrollTop() const {
		return _scrollTop;
	}
	void setSearchFieldQuery(const QString &query) {
		_searchFieldQuery = query;
	}
	[[nodiscard]] QString searchFieldQuery() const {
		return _searchFieldQuery;
	}
	void setSearchEnabledByContent(bool enabled) {
		_searchEnabledByContent = enabled;
	}
	[[nodiscard]] bool searchEnabledByContent() const {
		return _searchEnabledByContent;
	}
	void setSearchStartsFocused(bool focused) {
		_searchStartsFocused = focused;
	}
	[[nodiscard]] bool searchStartsFocused() const {
		return _searchStartsFocused;
	}

private:
	PeerData * const _peer = nullptr;
	const PeerId _migratedPeerId = 0;
	Data::ForumTopic *_topic = nullptr;
	Data::SavedSublist *_sublist = nullptr;
	UserData * const _settingsSelf = nullptr;
	PeerData * const _storiesPeer = nullptr;
	int _storiesAlbumId = 0;
	int _storiesAddToAlbumId = 0;
	PeerData * const _musicPeer = nullptr;
	PeerData * const _giftsPeer = nullptr;
	int _giftsCollectionId = 0;
	Statistics::Tag _statisticsTag;
	PeerData * const _starrefPeer = nullptr;
	BotStarRef::Type _starrefType = {};
	PollData * const _poll = nullptr;
	std::shared_ptr<Api::WhoReadList> _reactionsWhoReadIds;
	Data::ReactionId _reactionsSelected;
	const FullMsgId _pollReactionsContextId;
	UserData * const _globalMediaSelf = nullptr;

	int _scrollTop = 0;
	QString _searchFieldQuery;
	bool _searchEnabledByContent = false;
	bool _searchStartsFocused = false;

	rpl::lifetime _lifetime;

};

} // namespace Info
