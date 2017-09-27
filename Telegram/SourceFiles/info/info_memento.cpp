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
#include "info/info_memento.h"

#include <rpl/never.h>
#include <rpl/combine.h>
#include "window/window_controller.h"
#include "ui/widgets/scroll_area.h"
#include "lang/lang_keys.h"
#include "info/profile/info_profile_widget.h"
#include "info/info_media_widget.h"
#include "info/info_common_groups_widget.h"
#include "info/info_layer_wrap.h"
#include "info/info_narrow_wrap.h"
#include "info/info_side_wrap.h"
#include "styles/style_info.h"
#include "styles/style_profile.h"

namespace Info {

ContentWidget::ContentWidget(
	QWidget *parent,
	Wrap wrap,
	not_null<Window::Controller*> controller,
	not_null<PeerData*> peer)
: RpWidget(parent)
, _controller(controller)
, _peer(peer)
, _wrap(wrap)
, _scroll(this, st::infoScroll) {
	setAttribute(Qt::WA_OpaquePaintEvent);
}

void ContentWidget::setWrap(Wrap wrap) {
	if (_wrap != wrap) {
		_wrap = wrap;
		_wrapChanges.fire_copy(_wrap);
		update();
	}
}

void ContentWidget::resizeEvent(QResizeEvent *e) {
	auto newScrollTop = _scroll->scrollTop() + _topDelta;
	auto scrollGeometry = rect().marginsRemoved(
		QMargins(0, _scrollTopSkip, 0, 0));
	if (_scroll->geometry() != scrollGeometry) {
		_scroll->setGeometry(scrollGeometry);
		_inner->resizeToWidth(_scroll->width());
	}

	if (!_scroll->isHidden()) {
		if (_topDelta) {
			_scroll->scrollToY(newScrollTop);
		}
		auto scrollTop = _scroll->scrollTop();
		_inner->setVisibleTopBottom(scrollTop, scrollTop + _scroll->height());
	}
}

void ContentWidget::paintEvent(QPaintEvent *e) {
	Painter p(this);
	p.fillRect(e->rect(), (_wrap == Wrap::Layer)
		? st::boxBg
		: st::profileBg);
}

void ContentWidget::setGeometryWithTopMoved(
		const QRect &newGeometry,
		int topDelta) {
	_topDelta = topDelta;
	auto willBeResized = (size() != newGeometry.size());
	if (geometry() != newGeometry) {
		setGeometry(newGeometry);
	}
	if (!willBeResized) {
		QResizeEvent fake(size(), size());
		QApplication::sendEvent(this, &fake);
	}
	_topDelta = 0;
}

Ui::RpWidget *ContentWidget::doSetInnerWidget(
		object_ptr<RpWidget> inner,
		int scrollTopSkip) {
	using namespace rpl::mappers;

	_inner = _scroll->setOwnedWidget(std::move(inner));
	_inner->move(0, 0);

	rpl::combine(
		_scroll->scrollTopValue(),
		_scroll->heightValue(),
		_inner->desiredHeightValue(),
		tuple($1, $1 + $2, $3))
		| rpl::start_with_next([inner = _inner](
				int top,
				int bottom,
				int desired) {
			inner->setVisibleTopBottom(top, bottom);
		}, _inner->lifetime());
	return _inner;
}

rpl::producer<Section> ContentWidget::sectionRequest() const {
	return rpl::never<Section>();
}

rpl::producer<int> ContentWidget::desiredHeightValue() const {
	return _inner->desiredHeightValue()
		| rpl::map([this](int value) {
			return value + _scrollTopSkip;
		});
}

rpl::producer<int> ContentWidget::scrollTopValue() const {
	return _scroll->scrollTopValue();
}

int ContentWidget::scrollTopSave() const {
	return _scroll->scrollTop();
}

void ContentWidget::scrollTopRestore(int scrollTop) {
	_scroll->scrollToY(scrollTop);
}

void ContentWidget::scrollTo(const Ui::ScrollToRequest &request) {
	_scroll->scrollTo(request);
}

bool ContentWidget::wheelEventFromFloatPlayer(QEvent *e) {
	return _scroll->viewportEvent(e);
}

QRect ContentWidget::rectForFloatPlayer() const {
	return mapToGlobal(_scroll->geometry());
}

std::unique_ptr<ContentMemento> Memento::Default(
		PeerId peerId,
		Section section) {
	switch (section.type()) {
	case Section::Type::Profile:
		return std::make_unique<Profile::Memento>(peerId);
	case Section::Type::Media:
		return std::make_unique<Media::Memento>(
			peerId,
			section.mediaType());
	case Section::Type::CommonGroups:
		Assert(peerIsUser(peerId));
		return std::make_unique<CommonGroups::Memento>(
			peerToUser(peerId));
	}
	Unexpected("Wrong section type in Info::Memento::Default()");
}

object_ptr<Window::SectionWidget> Memento::createWidget(
		QWidget *parent,
		not_null<Window::Controller*> controller,
		Window::Column column,
		const QRect &geometry) {
	if (column == Window::Column::Third) {
		return object_ptr<SideWrap>(
			parent,
			controller,
			this);
	}
	return object_ptr<NarrowWrap>(
		parent,
		controller,
		this);
}

object_ptr<LayerWidget> Memento::createLayer(
		not_null<Window::Controller*> controller) {
	auto layout = controller->computeColumnLayout();
	auto minimalWidthForLayer = st::infoMinimalWidth
		+ 2 * st::infoMinimalLayerMargin;
	if (layout.bodyWidth < minimalWidthForLayer) {
		return nullptr;
	}
	return object_ptr<LayerWrap>(controller, this);
}

MoveMemento::MoveMemento(
	object_ptr<ContentWidget> content,
	Wrap wrap)
: _content(std::move(content))
, _wrap(wrap) {
}

object_ptr<Window::SectionWidget> MoveMemento::createWidget(
		QWidget *parent,
		not_null<Window::Controller*> controller,
		Window::Column column,
		const QRect &geometry) {
	if (_wrap == Wrap::Narrow && column != Window::Column::Third) {
		auto result = object_ptr<NarrowWrap>(
			parent,
			controller,
			this);
		result->setGeometry(geometry);
		return std::move(result);
	} else if (_wrap == Wrap::Side && column == Window::Column::Third) {
		auto result = object_ptr<SideWrap>(
			parent,
			controller,
			this);
		result->setGeometry(geometry);
		return std::move(result);
	}
	return nullptr;
}

object_ptr<LayerWidget> MoveMemento::createLayer(
		not_null<Window::Controller*> controller) {
	if (_wrap == Wrap::Layer) {
		auto result = object_ptr<LayerWrap>(
			controller,
			this);
		return std::move(result);
	}
	return nullptr;
}

object_ptr<ContentWidget> MoveMemento::content(
		QWidget *parent,
		Wrap wrap) {
	Ui::AttachParentChild(parent, _content);
	_content->setWrap(wrap);
	return std::move(_content);
}

rpl::producer<QString> TitleValue(
		const Section &section,
		not_null<PeerData*> peer) {
	return Lang::Viewer([&] {
		switch (section.type()) {
		case Section::Type::Profile:
			if (auto user = peer->asUser()) {
				return user->botInfo
					? lng_info_bot_title
					: lng_info_user_title;
			} else if (auto channel = peer->asChannel()) {
				return channel->isMegagroup()
					? lng_info_group_title
					: lng_info_channel_title;
			} else if (peer->isChat()) {
				return lng_info_group_title;
			}
			Unexpected("Bad peer type in Info::TitleValue()");

		case Section::Type::Media:
			switch (section.mediaType()) {
			case Section::MediaType::Photo:
				return lng_media_type_photos;
			case Section::MediaType::Video:
				return lng_media_type_videos;
			case Section::MediaType::MusicFile:
				return lng_media_type_songs;
			case Section::MediaType::File:
				return lng_media_type_files;
			case Section::MediaType::VoiceFile:
				return lng_media_type_audios;
			case Section::MediaType::Link:
				return lng_media_type_links;
			case Section::MediaType::RoundFile:
				return lng_media_type_rounds;
			}
			Unexpected("Bad media type in Info::TitleValue()");

		case Section::Type::CommonGroups:
			return lng_profile_common_groups_section;

		}
		Unexpected("Bad section type in Info::TitleValue()");
	}());
}

} // namespace Info
