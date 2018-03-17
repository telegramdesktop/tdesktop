/*
This file is part of Telegram Desktop,
the official desktop application for the Telegram messaging service.

For license and copyright information please follow this link:
https://github.com/telegramdesktop/tdesktop/blob/master/LEGAL
*/
#pragma once

#include <rpl/variable.h>
#include "ui/rp_widget.h"
#include "info/info_wrap_widget.h"

namespace Storage {
enum class SharedMediaType : signed char;
} // namespace Storage

namespace Ui {
class ScrollArea;
class InputField;
struct ScrollToRequest;
template <typename Widget>
class PaddingWrap;
} // namespace Ui

namespace Data {
class Feed;
} // namespace Data

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
	std::unique_ptr<ContentMemento> createMemento();

	virtual rpl::producer<Section> sectionRequest() const;
	virtual void setIsStackBottom(bool isStackBottom) {
	}

	rpl::producer<int> scrollHeightValue() const;
	rpl::producer<int> desiredHeightValue() const override;
	rpl::producer<bool> desiredShadowVisibility() const;
	bool hasTopBarShadow() const;

	virtual void setInnerFocus();

	// When resizing the widget with top edge moved up or down and we
	// want to add this top movement to the scroll position, so inner
	// content will not move.
	void setGeometryWithTopMoved(
		const QRect &newGeometry,
		int topDelta);
	void applyAdditionalScroll(int additionalScroll);
	int scrollTillBottom(int forHeight) const;
	rpl::producer<int> scrollTillBottomChanges() const;

	// Float player interface.
	bool wheelEventFromFloatPlayer(QEvent *e);
	QRect rectForFloatPlayer() const;

	virtual rpl::producer<SelectedItems> selectedListValue() const;
	virtual void cancelSelection() {
	}

protected:
	template <typename Widget>
	Widget *setInnerWidget(object_ptr<Widget> inner) {
		return static_cast<Widget*>(
			doSetInnerWidget(std::move(inner)));
	}

	not_null<Controller*> controller() const {
		return _controller;
	}

	void resizeEvent(QResizeEvent *e) override;
	void paintEvent(QPaintEvent *e) override;

	void setScrollTopSkip(int scrollTopSkip);
	int scrollTopSave() const;
	void scrollTopRestore(int scrollTop);
	void scrollTo(const Ui::ScrollToRequest &request);

private:
	RpWidget *doSetInnerWidget(object_ptr<RpWidget> inner);
	void updateControlsGeometry();
	void refreshSearchField(bool shown);

	virtual std::unique_ptr<ContentMemento> doCreateMemento() = 0;

	const not_null<Controller*> _controller;

	style::color _bg;
	rpl::variable<int> _scrollTopSkip = -1;
	rpl::event_stream<int> _scrollTillBottomChanges;
	object_ptr<Ui::ScrollArea> _scroll;
	Ui::PaddingWrap<Ui::RpWidget> *_innerWrap = nullptr;
	base::unique_qptr<Ui::RpWidget> _searchWrap = nullptr;
	QPointer<Ui::InputField> _searchField;
	int _innerDesiredHeight = 0;

	// Saving here topDelta in setGeometryWithTopMoved() to get it passed to resizeEvent().
	int _topDelta = 0;

};

class ContentMemento {
public:
	ContentMemento(PeerId peerId, PeerId migratedPeerId)
	: _peerId(peerId)
	, _migratedPeerId(migratedPeerId) {
	}
	explicit ContentMemento(not_null<Data::Feed*> feed)
	: _feed(feed) {
	}

	virtual object_ptr<ContentWidget> createWidget(
		QWidget *parent,
		not_null<Controller*> controller,
		const QRect &geometry) = 0;

	PeerId peerId() const {
		return _peerId;
	}
	PeerId migratedPeerId() const {
		return _migratedPeerId;
	}
	Data::Feed *feed() const {
		return _feed;
	}

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
	const PeerId _peerId = 0;
	const PeerId _migratedPeerId = 0;
	Data::Feed * const _feed = nullptr;
	int _scrollTop = 0;
	QString _searchFieldQuery;
	bool _searchEnabledByContent = false;
	bool _searchStartsFocused = false;

};

} // namespace Info
