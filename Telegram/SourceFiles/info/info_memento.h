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

#include "window/section_memento.h"
#include "ui/rp_widget.h"

namespace Storage {
enum class SharedMediaType : char;
} // namespace Storage

namespace Ui {
class ScrollArea;
} // namespace Ui

namespace Info {

enum class Wrap {
	Layer,
	Narrow,
	Side,
};

class Section final {
public:
	enum class Type {
		Profile,
		Media,
		CommonGroups,
	};
	using MediaType = Storage::SharedMediaType;

	Section(Type type) : _type(type) {
		Expects(type != Type::Media);
	}
	Section(MediaType mediaType)
		: _type(Type::Media)
		, _mediaType(mediaType) {
	}

	Type type() const {
		return _type;
	}
	MediaType mediaType() const {
		Expects(_type == Type::Media);
		return _mediaType;
	}

private:
	Type _type;
	Storage::SharedMediaType _mediaType;

};

class ContentMemento;

class ContentWidget : public Ui::RpWidget {
public:
	ContentWidget(
		QWidget *parent,
		Wrap wrap,
		not_null<Window::Controller*> controller);

	virtual bool showInternal(
		not_null<ContentMemento*> memento) = 0;
	virtual std::unique_ptr<ContentMemento> createMemento() = 0;

	virtual rpl::producer<Section> sectionRequest() const;
	
	virtual void setWrap(Wrap wrap);

	rpl::producer<int> desiredHeightValue() const override;

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
	Wrap wrap() const {
		return _wrap;
	}

	void resizeEvent(QResizeEvent *e) override;
	void paintEvent(QPaintEvent *e) override;

	rpl::producer<int> scrollTopValue() const;
	int scrollTopSave() const;
	void scrollTopRestore(int scrollTop);

private:
	RpWidget *doSetInnerWidget(
		object_ptr<RpWidget> inner,
		int scrollTopSkip);

	not_null<Window::Controller*> _controller;
	Wrap _wrap = Wrap::Layer;

	int _scrollTopSkip = 0;
	object_ptr<Ui::ScrollArea> _scroll;
	Ui::RpWidget *_inner = nullptr;

	// Saving here topDelta in setGeometryWithTopMoved() to get it passed to resizeEvent().
	int _topDelta = 0;

};

class ContentMemento {
public:
	virtual object_ptr<ContentWidget> createWidget(
		QWidget *parent,
		Wrap wrap,
		not_null<Window::Controller*> controller,
		const QRect &geometry) = 0;

	virtual ~ContentMemento() = default;

	void setScrollTop(int scrollTop) {
		_scrollTop = scrollTop;
	}
	int scrollTop() const {
		return _scrollTop;
	}

private:
	int _scrollTop = 0;

};

class Memento final : public Window::SectionMemento {
public:
	Memento(PeerId peerId)
		: Memento(peerId, Section::Type::Profile) {
	}
	Memento(PeerId peerId, Section section)
		: Memento(peerId, section, Default(peerId, section)) {
	}
	Memento(
		PeerId peerId,
		Section section,
		std::unique_ptr<ContentMemento> content)
		: _peerId(peerId)
		, _section(section)
		, _content(std::move(content)) {
	}

	object_ptr<Window::SectionWidget> createWidget(
		QWidget *parent,
		not_null<Window::Controller*> controller,
		const QRect &geometry) override;

	object_ptr<LayerWidget> createLayer(
		not_null<Window::Controller*> controller) override;

	void setInner(std::unique_ptr<ContentMemento> content) {
		_content = std::move(content);
	}
	not_null<ContentMemento*> content() {
		return _content.get();
	}

	PeerId peerId() const {
		return _peerId;
	}
	Section section() const {
		return _section;
	}

private:
	static std::unique_ptr<ContentMemento> Default(
		PeerId peerId,
		Section section);

	PeerId _peerId = 0;
	Section _section = Section::Type::Profile;
	std::unique_ptr<ContentMemento> _content;

};

rpl::producer<QString> TitleValue(
	const Section &section,
	not_null<PeerData*> peer);

} // namespace Info
