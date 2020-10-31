/*
This file is part of Telegram Desktop,
the official desktop application for the Telegram messaging service.

For license and copyright information please follow this link:
https://github.com/telegramdesktop/tdesktop/blob/master/LEGAL
*/
#include "dialogs/dialogs_row.h"

#include "ui/effects/ripple_animation.h"
#include "ui/text/text_options.h"
#include "dialogs/dialogs_entry.h"
#include "data/data_folder.h"
#include "data/data_peer_values.h"
#include "history/history.h"
#include "lang/lang_keys.h"
#include "mainwidget.h"
#include "styles/style_dialogs.h"

namespace Dialogs {
namespace {

QString ComposeFolderListEntryText(not_null<Data::Folder*> folder) {
	const auto &list = folder->lastHistories();
	if (list.empty()) {
		return QString();
	}

	const auto count = std::max(
		int(list.size()),
		folder->chatsList()->fullSize().current());

	const auto throwAwayLastName = (list.size() > 1)
		&& (count == list.size() + 1);
	auto &&peers = ranges::view::all(
		list
	) | ranges::view::take(
		list.size() - (throwAwayLastName ? 1 : 0)
	);
	const auto wrapName = [](not_null<History*> history) {
		const auto name = TextUtilities::Clean(history->peer->name);
		return (history->unreadCount() > 0)
			? (textcmdStartSemibold()
				+ textcmdLink(1, name)
				+ textcmdStopSemibold())
			: name;
	};
	const auto shown = int(peers.size());
	const auto accumulated = [&] {
		Expects(shown > 0);

		auto i = peers.begin();
		auto result = wrapName(*i);
		for (++i; i != peers.end(); ++i) {
			result = tr::lng_archived_last_list(
				tr::now,
				lt_accumulated,
				result,
				lt_chat,
				wrapName(*i));
		}
		return result;
	}();
	return (shown < count)
		? tr::lng_archived_last(tr::now, lt_count, (count - shown), lt_chats, accumulated)
		: accumulated;
}

} // namespace

BasicRow::BasicRow() = default;
BasicRow::~BasicRow() = default;

void BasicRow::setOnline(bool online, Fn<void()> updateCallback) const {
	if (_online == online) {
		return;
	}
	_online = online;
	if (_onlineUserpic && _onlineUserpic->animation.animating()) {
		_onlineUserpic->animation.change(
			_online ? 1. : 0.,
			st::dialogsOnlineBadgeDuration);
	} else if (updateCallback) {
		ensureOnlineUserpic();
		_onlineUserpic->animation.start(
			std::move(updateCallback),
			_online ? 0. : 1.,
			_online ? 1. : 0.,
			st::dialogsOnlineBadgeDuration);
	}
	if (!_online
		&& _onlineUserpic
		&& !_onlineUserpic->animation.animating()) {
		_onlineUserpic = nullptr;
	}
}

void BasicRow::addRipple(
		QPoint origin,
		QSize size,
		Fn<void()> updateCallback) {
	if (!_ripple) {
		auto mask = Ui::RippleAnimation::rectMask(size);
		_ripple = std::make_unique<Ui::RippleAnimation>(
			st::dialogsRipple,
			std::move(mask),
			std::move(updateCallback));
	}
	_ripple->add(origin);
}

void BasicRow::stopLastRipple() {
	if (_ripple) {
		_ripple->lastStop();
	}
}

void BasicRow::paintRipple(
		Painter &p,
		int x,
		int y,
		int outerWidth,
		const QColor *colorOverride) const {
	if (_ripple) {
		_ripple->paint(p, x, y, outerWidth, colorOverride);
		if (_ripple->empty()) {
			_ripple.reset();
		}
	}
}

void BasicRow::ensureOnlineUserpic() const {
	if (_onlineUserpic) {
		return;
	}
	_onlineUserpic = std::make_unique<OnlineUserpic>();
}

void BasicRow::PaintOnlineFrame(
		not_null<OnlineUserpic*> data,
		not_null<PeerData*> peer,
		std::shared_ptr<Data::CloudImageView> &view) {
	data->frame.fill(Qt::transparent);

	Painter q(&data->frame);
	peer->paintUserpic(
		q,
		view,
		0,
		0,
		st::dialogsPhotoSize);

	PainterHighQualityEnabler hq(q);
	q.setCompositionMode(QPainter::CompositionMode_Source);

	const auto size = st::dialogsOnlineBadgeSize;
	const auto stroke = st::dialogsOnlineBadgeStroke;
	const auto skip = st::dialogsOnlineBadgeSkip;
	const auto edge = st::dialogsPadding.x() + st::dialogsPhotoSize;
	const auto shrink = (size / 2) * (1. - data->online);

	auto pen = QPen(Qt::transparent);
	pen.setWidthF(stroke * data->online);
	q.setPen(pen);
	q.setBrush(data->active
		? st::dialogsOnlineBadgeFgActive
		: st::dialogsOnlineBadgeFg);
	q.drawEllipse(QRectF(
		edge - skip.x() - size,
		edge - skip.y() - size,
		size,
		size
	).marginsRemoved({ shrink, shrink, shrink, shrink }));
}

void BasicRow::paintUserpic(
		Painter &p,
		not_null<PeerData*> peer,
		bool allowOnline,
		bool active,
		int fullWidth) const {
	setOnline(Data::IsPeerAnOnlineUser(peer));

	const auto online = _onlineUserpic
		? _onlineUserpic->animation.value(_online ? 1. : 0.)
		: (_online ? 1. : 0.);
	if (!allowOnline || online == 0.) {
		peer->paintUserpicLeft(
			p,
			_userpic,
			st::dialogsPadding.x(),
			st::dialogsPadding.y(),
			fullWidth,
			st::dialogsPhotoSize);
		if (!allowOnline || !_online) {
			_onlineUserpic = nullptr;
		}
		return;
	}
	ensureOnlineUserpic();
	if (_onlineUserpic->frame.isNull()) {
		_onlineUserpic->frame = QImage(
			st::dialogsPhotoSize * cRetinaFactor(),
			st::dialogsPhotoSize * cRetinaFactor(),
			QImage::Format_ARGB32_Premultiplied);
		_onlineUserpic->frame.setDevicePixelRatio(cRetinaFactor());
	}
	const auto key = peer->userpicUniqueKey(_userpic);
	if (_onlineUserpic->online != online
		|| _onlineUserpic->key != key
		|| _onlineUserpic->active != active) {
		_onlineUserpic->online = online;
		_onlineUserpic->key = key;
		_onlineUserpic->active = active;
		PaintOnlineFrame(_onlineUserpic.get(), peer, _userpic);
	}
	p.drawImage(st::dialogsPadding, _onlineUserpic->frame);
}

Row::Row(Key key, int pos) : _id(key), _pos(pos) {
	if (const auto history = key.history()) {
		setOnline(Data::IsPeerAnOnlineUser(history->peer));
	}
}

uint64 Row::sortKey(FilterId filterId) const {
	return _id.entry()->sortKeyInChatList(filterId);
}

void Row::validateListEntryCache() const {
	const auto folder = _id.folder();
	if (!folder) {
		return;
	}
	const auto version = folder->chatListViewVersion();
	if (_listEntryCacheVersion == version) {
		return;
	}
	_listEntryCacheVersion = version;
	_listEntryCache.setText(
		st::dialogsTextStyle,
		ComposeFolderListEntryText(folder),
		Ui::DialogTextOptions());
}

FakeRow::FakeRow(Key searchInChat, not_null<HistoryItem*> item)
: _searchInChat(searchInChat)
, _item(item)
, _cache(st::dialogsTextWidthMin) {
}

} // namespace Dialogs