/*
This file is part of Telegram Desktop,
the official desktop application for the Telegram messaging service.

For license and copyright information please follow this link:
https://github.com/telegramdesktop/tdesktop/blob/master/LEGAL
*/
#pragma once

#include "info/info_wrap_widget.h"
#include "info/statistics/info_statistics_tag.h"

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

namespace Info::Stories {
struct Tag;
enum class Tab;
} // namespace Info::Stories

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
	int _maxVisibleHeight = 0;
	bool _isStackBottom = false;

	// Saving here topDelta in setGeometryWithTopMoved() to get it passed to resizeEvent().
	int _topDelta = 0;

	// To paint round edges from content.
	style::margins _paintPadding;

};

class ContentMemento {
public:
	ContentMemento(
		not_null<PeerData*> peer,
		Data::ForumTopic *topic,
		PeerId migratedPeerId);
	explicit ContentMemento(Settings::Tag settings);
	explicit ContentMemento(Downloads::Tag downloads);
	explicit ContentMemento(Stories::Tag stories);
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

	virtual object_ptr<ContentWidget> createWidget(
		QWidget *parent,
		not_null<Controller*> controller,
		const QRect &geometry) = 0;

	PeerData *peer() const {
		return _peer;
	}
	PeerId migratedPeerId() const {
		return _migratedPeerId;
	}
	Data::ForumTopic *topic() const {
		return _topic;
	}
	UserData *settingsSelf() const {
		return _settingsSelf;
	}
	PeerData *storiesPeer() const {
		return _storiesPeer;
	}
	Stories::Tab storiesTab() const {
		return _storiesTab;
	}
	Statistics::Tag statisticsTag() const {
		return _statisticsTag;
	}
	PeerData *starrefPeer() const {
		return _starrefPeer;
	}
	BotStarRef::Type starrefType() const {
		return _starrefType;
	}
	PollData *poll() const {
		return _poll;
	}
	FullMsgId pollContextId() const {
		return _poll ? _pollReactionsContextId : FullMsgId();
	}
	std::shared_ptr<Api::WhoReadList> reactionsWhoReadIds() const {
		return _reactionsWhoReadIds;
	}
	Data::ReactionId reactionsSelected() const {
		return _reactionsSelected;
	}
	FullMsgId reactionsContextId() const {
		return _reactionsWhoReadIds ? _pollReactionsContextId : FullMsgId();
	}
	UserData *globalMediaSelf() const {
		return _globalMediaSelf;
	}
	Key key() const;

	virtual Section section() const = 0;

	virtual ~ContentMemento() = default;

	void setScrollTop(int scrollTop) {
		_scrollTop = scrollTop;
	}
	int scrollTop() const {
		return _scrollTop;
	}
	void setSearchFieldQuery(const QString &query) {
		_searchFieldQuery = query;
	}
	QString searchFieldQuery() const {
		return _searchFieldQuery;
	}
	void setSearchEnabledByContent(bool enabled) {
		_searchEnabledByContent = enabled;
	}
	bool searchEnabledByContent() const {
		return _searchEnabledByContent;
	}
	void setSearchStartsFocused(bool focused) {
		_searchStartsFocused = focused;
	}
	bool searchStartsFocused() const {
		return _searchStartsFocused;
	}

private:
	PeerData * const _peer = nullptr;
	const PeerId _migratedPeerId = 0;
	Data::ForumTopic *_topic = nullptr;
	UserData * const _settingsSelf = nullptr;
	PeerData * const _storiesPeer = nullptr;
	Stories::Tab _storiesTab = {};
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
