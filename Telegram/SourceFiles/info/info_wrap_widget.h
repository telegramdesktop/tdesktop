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

#include <rpl/event_stream.h>
#include "window/section_widget.h"

namespace Ui {
class SettingsSlider;
class FadeShadow;
} // namespace Ui

namespace Info {
namespace Profile {
class Widget;
} // namespace Profile

namespace Media {
class Widget;
} // namespace Media

class Section;
class Memento;
class MoveMemento;
class ContentWidget;
class TopBar;

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

class WrapWidget final : public Ui::RpWidget {
public:
	WrapWidget(
		QWidget *parent,
		not_null<Window::Controller*> controller,
		Wrap wrap,
		not_null<Memento*> memento);

	not_null<PeerData*> peer() const;
	Wrap wrap() const;
	void setWrap(Wrap wrap);

	bool hasTopBarShadow() const;
	QPixmap grabForShowAnimation(
		const Window::SectionSlideParams &params);

	bool showInternal(
		not_null<Window::SectionMemento*> memento);
	std::unique_ptr<Window::SectionMemento> createMemento();

	rpl::producer<int> desiredHeightValue() const;

	void setInnerFocus();
	void showFinished();

	void updateInternalState(not_null<Memento*> memento);
	void saveState(not_null<Memento*> memento);

	// Float player interface.
	bool wheelEventFromFloatPlayer(QEvent *e);
	QRect rectForFloatPlayer() const;

protected:
	void resizeEvent(QResizeEvent *e);
	void paintEvent(QPaintEvent *e);

	not_null<Window::Controller*> controller() const {
		return _controller;
	}

private:
	enum class Tab {
		Profile,
		Media,
		None,
	};
	void applyState(Wrap wrap, not_null<Memento*> memento);
	void setupTop(Wrap wrap, const Section &section, PeerId peerId);
	void setupTabbedTop(const Section &section);
	void setupTabs(Tab tab);
	void createTabs();
	void createTopBar(
		Wrap wrap,
		const Section &section,
		PeerId peerId);
	not_null<RpWidget*> topWidget() const;

	QRect contentGeometry() const;
	rpl::producer<int> desiredHeightForContent() const;
	void finishShowContent();
	void updateContentGeometry();

	void showTab(Tab tab);
	void showContent(object_ptr<ContentWidget> content);
	object_ptr<ContentWidget> createContent(Tab tab);
	object_ptr<Profile::Widget> createProfileWidget();
	object_ptr<Media::Widget> createMediaWidget();

	not_null<Window::Controller*> _controller;
	object_ptr<ContentWidget> _content = { nullptr };
	object_ptr<Ui::SettingsSlider> _topTabs = { nullptr };
	object_ptr<TopBar> _topBar = { nullptr };
	object_ptr<Ui::FadeShadow> _topShadow = { nullptr };
	Tab _tab = Tab::Profile;

	rpl::event_stream<rpl::producer<int>> _desiredHeights;

};

} // namespace Info
