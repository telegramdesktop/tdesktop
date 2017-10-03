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

#include <rpl/variable.h>
#include "ui/rp_widget.h"
#include "info/info_wrap_widget.h"

namespace Storage {
enum class SharedMediaType : char;
} // namespace Storage

namespace Ui {
class ScrollArea;
struct ScrollToRequest;
} // namespace Ui

namespace Window {
class Controller;
} // namespace Window

namespace Info {

class ContentMemento;

class ContentWidget : public Ui::RpWidget {
public:
	ContentWidget(
		QWidget *parent,
		rpl::producer<Wrap> wrap,
		not_null<Window::Controller*> controller,
		not_null<PeerData*> peer);

	virtual bool showInternal(
		not_null<ContentMemento*> memento) = 0;
	virtual std::unique_ptr<ContentMemento> createMemento() = 0;

	virtual rpl::producer<Section> sectionRequest() const;

	virtual Section section() const = 0;
	not_null<PeerData*> peer() const {
		return _peer;
	}

	rpl::producer<int> desiredHeightValue() const override;
	rpl::producer<bool> desiredShadowVisibility() const;

	virtual void setInnerFocus() {
		_inner->setFocus();
	}

	// When resizing the widget with top edge moved up or down and we
	// want to add this top movement to the scroll position, so inner
	// content will not move.
	void setGeometryWithTopMoved(
		const QRect &newGeometry,
		int topDelta);

	// Float player interface.
	bool wheelEventFromFloatPlayer(QEvent *e);
	QRect rectForFloatPlayer() const;

protected:
	template <typename Widget>
	Widget *setInnerWidget(
			object_ptr<Widget> inner,
			int scrollTopSkip = 0) {
		return static_cast<Widget*>(
			doSetInnerWidget(std::move(inner), scrollTopSkip));
	}

	not_null<Window::Controller*> controller() const {
		return _controller;
	}

	void resizeEvent(QResizeEvent *e) override;
	void paintEvent(QPaintEvent *e) override;

	int scrollTopSave() const;
	void scrollTopRestore(int scrollTop);

	void scrollTo(const Ui::ScrollToRequest &request);

private:
	RpWidget *doSetInnerWidget(
		object_ptr<RpWidget> inner,
		int scrollTopSkip);

	const not_null<Window::Controller*> _controller;
	const not_null<PeerData*> _peer;

	style::color _bg;
	int _scrollTopSkip = 0;
	object_ptr<Ui::ScrollArea> _scroll;
	Ui::RpWidget *_inner = nullptr;

	// Saving here topDelta in setGeometryWithTopMoved() to get it passed to resizeEvent().
	int _topDelta = 0;

};

class ContentMemento {
public:
	ContentMemento(PeerId peerId) : _peerId(peerId) {
	}

	virtual object_ptr<ContentWidget> createWidget(
		QWidget *parent,
		rpl::producer<Wrap> wrap,
		not_null<Window::Controller*> controller,
		const QRect &geometry) = 0;

	virtual PeerId peerId() const {
		return _peerId;
	}
	virtual Section section() const = 0;

	virtual ~ContentMemento() = default;

	void setScrollTop(int scrollTop) {
		_scrollTop = scrollTop;
	}
	int scrollTop() const {
		return _scrollTop;
	}

private:
	PeerId _peerId = 0;
	int _scrollTop = 0;

};

} // namespace Info
