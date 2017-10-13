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
#include "media/player/media_player_list.h"

#include "media/player/media_player_instance.h"
#include "overview/overview_layout.h"
#include "styles/style_media_player.h"
#include "history/history_media.h"
#include "auth_session.h"

namespace Media {
namespace Player {

ListWidget::ListWidget(QWidget *parent) : RpWidget(parent) {
	setMouseTracking(true);
	playlistUpdated();
	subscribe(
		instance()->playlistChangedNotifier(),
		[this](AudioMsgId::Type type) { playlistUpdated(); });
	Auth().data().itemRemoved()
		| rpl::start_with_next(
			[this](auto item) { itemRemoved(item); },
			lifetime());
	Auth().data().itemRepaintRequest()
		| rpl::start_with_next(
			[this](auto item) { repaintItem(item); },
			lifetime());
}

ListWidget::~ListWidget() {
	auto layouts = base::take(_layouts);
	for_const (auto layout, layouts) {
		delete layout;
	}
}

void ListWidget::paintEvent(QPaintEvent *e) {
	Painter p(this);

	auto clip = e->rect();
	Overview::Layout::PaintContext context(getms(), false);
	int y = marginTop();
	for_const (auto layout, _list) {
		auto layoutHeight = layout->height();
		if (y + layoutHeight > clip.y()) {
			if (y >= clip.y() + clip.height()) break;

			p.translate(0, y);
			layout->paint(p, clip.translated(0, -y), TextSelection(), &context);
			p.translate(0, -y);
		}
		y += layoutHeight;
	}
}

void ListWidget::mousePressEvent(QMouseEvent *e) {
	if (e->button() != Qt::LeftButton) return;

	ClickHandler::pressed();
}

void ListWidget::mouseReleaseEvent(QMouseEvent *e) {
	ClickHandlerPtr activated = ClickHandler::unpressed();
	if (!ClickHandler::getActive() && _cursor != style::cur_default) {
		_cursor = style::cur_default;
		setCursor(_cursor);
	}
	if (activated) {
		App::activateClickHandler(activated, e->button());
		return;
	}
}

void ListWidget::mouseMoveEvent(QMouseEvent *e) {
	auto m = e->pos();

	ClickHandlerPtr lnk;
	ClickHandlerHost *lnkhost = nullptr;
	HistoryItem *item = nullptr;
	HistoryCursorState cursorState = HistoryDefaultCursorState;

	int y = marginTop();
	for_const (auto layout, _list) {
		auto layoutHeight = layout->height();
		if (y + layoutHeight > m.y()) {
			if (y <= m.y()) {
				if (auto media = layout->toMediaItem()) {
					item = media->getItem();
					auto result = media->getState(m - QPoint(0, y), HistoryStateRequest());
					lnk = result.link;
					cursorState = result.cursor;
					lnkhost = media;
				}
			}
			break;
		}
		y += layoutHeight;
	}

	auto cur = lnk ? style::cur_pointer : style::cur_default;
	if (cur != _cursor) {
		setCursor(_cursor = cur);
	}

	auto lnkChanged = ClickHandler::setActive(lnk, lnkhost);
	if (item != App::mousedItem()) {
		repaintItem(App::mousedItem());
		App::mousedItem(item);
		repaintItem(App::mousedItem());
	}
}

void ListWidget::repaintItem(const HistoryItem *item) {
	if (!item) return;

	auto layoutIt = _layouts.constFind(item->fullId());
	if (layoutIt != _layouts.cend()) {
		int y = 0;
		for_const (auto layout, _list) {
			auto layoutHeight = layout->height();
			if (layout->getItem() == item) {
				update(0, y, width(), layoutHeight);
				break;
			}
			y += layoutHeight;
		}
	}
}

void ListWidget::itemRemoved(not_null<const HistoryItem *> item) {
	auto layoutIt = _layouts.find(item->fullId());
	if (layoutIt != _layouts.cend()) {
		auto layout = layoutIt.value();
		_layouts.erase(layoutIt);

		for (int i = 0, count = _list.size(); i != count; ++i) {
			if (_list[i] == layout) {
				_list.removeAt(i);
				break;
			}
		}
		delete layout;
	}
}

QRect ListWidget::getCurrentTrackGeometry() const {
	auto top = marginTop();
	auto current = instance()->current(AudioMsgId::Type::Song);
	auto fullMsgId = current.contextId();
	for_const (auto layout, _list) {
		auto layoutHeight = layout->height();
		if (layout->getItem()->fullId() == fullMsgId) {
			return QRect(0, top, width(), layoutHeight);
		}
		top += layoutHeight;
	}
	return QRect(0, height(), width(), 0);
}

int ListWidget::resizeGetHeight(int newWidth) {
	auto result = 0;
	for_const (auto layout, _list) {
		result += layout->resizeGetHeight(newWidth);
	}
	return (result > 0) ? (marginTop() + result) : 0;
}

int ListWidget::marginTop() const {
	return st::mediaPlayerListMarginTop;
}

void ListWidget::playlistUpdated() {
	auto newHeight = 0;

	auto playlist = instance()->playlist(AudioMsgId::Type::Song);
	auto playlistSize = playlist.size();
	auto existingSize = _list.size();
	if (playlistSize > existingSize) {
		_list.reserve(playlistSize);
	}

	int existingIndex = 0;
	for (int i = 0; i != playlistSize; ++i) {
		auto &msgId = playlist[i];
		if (existingIndex < existingSize && _list[existingIndex]->getItem()->fullId() == msgId) {
			newHeight += _list[existingIndex]->height();
			++existingIndex;
			continue;
		}
		auto layoutIt = _layouts.constFind(msgId);
		if (layoutIt == _layouts.cend()) {
			if (auto item = App::histItemById(msgId)) {
				if (auto media = item->getMedia()) {
					if (media->type() == MediaTypeMusicFile) {
						layoutIt = _layouts.insert(msgId, new Overview::Layout::Document(item, media->getDocument(), st::mediaPlayerFileLayout));
						layoutIt.value()->initDimensions();
					}
				}
			}
		}
		if (layoutIt != _layouts.cend()) {
			auto layout = layoutIt.value();
			if (existingIndex < existingSize) {
				_list[existingIndex] = layout;
			} else {
				_list.push_back(layout);
				++existingSize;
			}
			++existingIndex;
			newHeight += layout->resizeGetHeight(width());
		}
	}
	while (existingIndex < existingSize) {
		_list.pop_back();
		--existingSize;
	}

	if (newHeight > 0) {
		newHeight += marginTop();
	}
	if (newHeight != height()) {
		resize(width(), newHeight);
		emit heightUpdated();
	}
}

} // namespace Player
} // namespace Media
