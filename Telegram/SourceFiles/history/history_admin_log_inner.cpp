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
#include "history/history_admin_log_inner.h"

#include "history/history_admin_log_item.h"
#include "history/history_admin_log_section.h"

namespace AdminLog {
namespace {

constexpr int kEventsPerPage = 3;

} // namespace

InnerWidget::InnerWidget(QWidget *parent, gsl::not_null<ChannelData*> channel, base::lambda<void(int top)> scrollTo) : TWidget(parent)
, _channel(channel)
, _history(App::history(channel))
, _scrollTo(std::move(scrollTo)) {
	setMouseTracking(true);
}

void InnerWidget::setVisibleTopBottom(int visibleTop, int visibleBottom) {
	_visibleTop = visibleTop;
	_visibleBottom = visibleBottom;

	updateVisibleTopItem();
	checkPreloadMore();
}

void InnerWidget::updateVisibleTopItem() {
	auto start = std::rbegin(_items), end = std::rend(_items);
	auto from = std::upper_bound(start, end, _visibleTop, [](int top, auto &elem) {
		return top <= elem->top() + elem->height();
	});
	if (from != end) {
		_visibleTopItem = from->get();
		_visibleTopFromItem = _visibleTop - _visibleTopItem->top();
	} else {
		_visibleTopItem = nullptr;
		_visibleTopFromItem = _visibleTop;
	}
}

void InnerWidget::checkPreloadMore() {
	if (_visibleTop + PreloadHeightsCount * (_visibleBottom - _visibleTop) > height()) {
		preloadMore(Direction::Down);
	}
	if (_visibleTop < PreloadHeightsCount * (_visibleBottom - _visibleTop)) {
		preloadMore(Direction::Up);
	}
}

void InnerWidget::applyFilter(MTPDchannelAdminLogEventsFilter::Flags flags, const std::vector<gsl::not_null<UserData*>> &admins) {
	_filterFlags = flags;
	_filterAdmins = admins;
}

void InnerWidget::saveState(gsl::not_null<SectionMemento*> memento) const {
	//if (auto count = _items.size()) {
	//	QList<gsl::not_null<PeerData*>> groups;
	//	groups.reserve(count);
	//	for_const (auto item, _items) {
	//		groups.push_back(item->peer);
	//	}
	//	memento->setCommonGroups(groups);
	//}
}

void InnerWidget::restoreState(gsl::not_null<const SectionMemento*> memento) {
	//auto list = memento->getCommonGroups();
	//_allLoaded = false;
	//if (!list.empty()) {
	//	showInitial(list);
	//}
}

//void InnerWidget::showInitial(const QList<PeerData*> &list) {
//	for_const (auto group, list) {
//		if (auto item = computeItem(group)) {
//			_items.push_back(item);
//		}
//		_preloadGroupId = group->bareId();
//	}
//	updateSize();
//}

void InnerWidget::preloadMore(Direction direction) {
	auto &requestId = (direction == Direction::Up) ? _preloadUpRequestId : _preloadDownRequestId;
	auto &loadedFlag = (direction == Direction::Up) ? _upLoaded : _downLoaded;
	if (requestId != 0 || loadedFlag) {
		return;
	}

	auto flags = MTPchannels_GetAdminLog::Flags(0);
	auto filter = MTP_channelAdminLogEventsFilter(MTP_flags(_filterFlags));
	if (_filterFlags != 0) {
		flags |= MTPchannels_GetAdminLog::Flag::f_events_filter;
	}
	auto admins = QVector<MTPInputUser>(0);
	if (!_filterAdmins.empty()) {
		admins.reserve(_filterAdmins.size());
		for (auto &admin : _filterAdmins) {
			admins.push_back(admin->inputUser);
		}
		flags |= MTPchannels_GetAdminLog::Flag::f_admins;
	}
	auto query = QString();
	auto maxId = (direction == Direction::Up) ? _minId : 0;
	auto minId = (direction == Direction::Up) ? 0 : _maxId;
	requestId = request(MTPchannels_GetAdminLog(MTP_flags(flags), _channel->inputChannel, MTP_string(query), filter, MTP_vector<MTPInputUser>(admins), MTP_long(maxId), MTP_long(minId), MTP_int(kEventsPerPage))).done([this, &requestId, &loadedFlag, direction](const MTPchannels_AdminLogResults &result) {
		Expects(result.type() == mtpc_channels_adminLogResults);
		requestId = 0;

		auto &results = result.c_channels_adminLogResults();
		App::feedUsers(results.vusers);
		App::feedChats(results.vchats);
		auto &events = results.vevents.v;
		if (!events.empty()) {
			_items.reserve(_items.size() + events.size());
			for_const (auto &event, events) {
				t_assert(event.type() == mtpc_channelAdminLogEvent);
				_items.push_back(std::make_unique<Item>(_history, _idManager, event.c_channelAdminLogEvent()));
			}
			if (!_items.empty()) {
				_maxId = _items.front()->id();
				_minId = _items.back()->id();
				if (_minId == 1) {
					_upLoaded = true;
				}
			}
			itemsAdded(direction);
		} else {
			loadedFlag = true;
		}
	}).fail([this, &requestId, &loadedFlag](const RPCError &error) {
		requestId = 0;
		loadedFlag = true;
	}).send();
}

void InnerWidget::itemsAdded(Direction direction) {
	updateSize();
}

void InnerWidget::updateSize() {
	TWidget::resizeToWidth(width());
	auto newVisibleTop = (_visibleTopItem ? _visibleTopItem->top() : 0) + _visibleTopFromItem;
	_scrollTo(newVisibleTop);
	updateVisibleTopItem();
	checkPreloadMore();
}

int InnerWidget::resizeGetHeight(int newWidth) {
	update();

	auto newHeight = 0;
	for (auto &item : base::reversed(_items)) {
		item->setTop(newHeight);
		newHeight += item->resizeGetHeight(newWidth);
	}
	return qMax(newHeight, _minHeight);
}

void InnerWidget::paintEvent(QPaintEvent *e) {
	Painter p(this);

	auto ms = getms();
	auto clip = e->rect();

	if (_items.empty() && _upLoaded && _downLoaded) {
		paintEmpty(p);
	} else {
		auto start = std::rbegin(_items), end = std::rend(_items);
		auto from = std::upper_bound(start, end, clip.top(), [](int top, auto &elem) {
			return top <= elem->top() + elem->height();
		});
		auto to = std::lower_bound(start, end, clip.top() + clip.height(), [](auto &elem, int bottom) {
			return elem->top() < bottom;
		});
		if (from != end) {
			auto top = (*from)->top();
			p.translate(0, top);
			for (auto i = from; i != to; ++i) {
				(*i)->draw(p, clip.translated(0, -top), TextSelection(), ms);
				auto height = (*i)->height();
				top += height;
				p.translate(0, height);
			}
		}
	}
}

void InnerWidget::paintEmpty(Painter &p) {
	//style::font font(st::msgServiceFont);
	//int32 w = font->width(lang(lng_willbe_history)) + st::msgPadding.left() + st::msgPadding.right(), h = font->height + st::msgServicePadding.top() + st::msgServicePadding.bottom() + 2;
	//QRect tr((width() - w) / 2, (height() - _field->height() - 2 * st::historySendPadding - h) / 2, w, h);
	//HistoryLayout::ServiceMessagePainter::paintBubble(p, tr.x(), tr.y(), tr.width(), tr.height());

	//p.setPen(st::msgServiceFg);
	//p.setFont(font->f);
	//p.drawText(tr.left() + st::msgPadding.left(), tr.top() + st::msgServicePadding.top() + 1 + font->ascent, lang(lng_willbe_history));
}

void InnerWidget::keyPressEvent(QKeyEvent *e) {
	if (e->key() == Qt::Key_Escape && _cancelledCallback) {
		_cancelledCallback();
	}
}

void InnerWidget::mousePressEvent(QMouseEvent *e) {
}

void InnerWidget::mouseMoveEvent(QMouseEvent *e) {
}

void InnerWidget::mouseReleaseEvent(QMouseEvent *e) {
}

InnerWidget::~InnerWidget() = default;

} // namespace AdminLog
