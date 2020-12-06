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

void BasicRow::setCornerBadgeShown(
		bool shown,
		Fn<void()> updateCallback) const {
	if (_cornerBadgeShown == shown) {
		return;
	}
	_cornerBadgeShown = shown;
	if (_cornerBadgeUserpic && _cornerBadgeUserpic->animation.animating()) {
		_cornerBadgeUserpic->animation.change(
			_cornerBadgeShown ? 1. : 0.,
			st::dialogsOnlineBadgeDuration);
	} else if (updateCallback) {
		ensureCornerBadgeUserpic();
		_cornerBadgeUserpic->animation.start(
			std::move(updateCallback),
			_cornerBadgeShown ? 0. : 1.,
			_cornerBadgeShown ? 1. : 0.,
			st::dialogsOnlineBadgeDuration);
	}
	if (!_cornerBadgeShown
		&& _cornerBadgeUserpic
		&& !_cornerBadgeUserpic->animation.animating()) {
		_cornerBadgeUserpic = nullptr;
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

void BasicRow::updateCornerBadgeShown(
		not_null<PeerData*> peer,
		Fn<void()> updateCallback) const {
	const auto shown = [&] {
		if (const auto user = peer->asUser()) {
			return Data::IsUserOnline(user);
		} else if (const auto channel = peer->asChannel()) {
			return Data::ChannelHasActiveCall(channel);
		}
		return false;
	}();
	setCornerBadgeShown(shown, std::move(updateCallback));
}

void BasicRow::ensureCornerBadgeUserpic() const {
	if (_cornerBadgeUserpic) {
		return;
	}
	_cornerBadgeUserpic = std::make_unique<CornerBadgeUserpic>();
}

void BasicRow::PaintCornerBadgeFrame(
		not_null<CornerBadgeUserpic*> data,
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

	const auto size = peer->isUser()
		? st::dialogsOnlineBadgeSize
		: st::dialogsCallBadgeSize;
	const auto stroke = st::dialogsOnlineBadgeStroke;
	const auto skip = peer->isUser()
		? st::dialogsOnlineBadgeSkip
		: st::dialogsCallBadgeSkip;
	const auto shrink = (size / 2) * (1. - data->shown);

	auto pen = QPen(Qt::transparent);
	pen.setWidthF(stroke * data->shown);
	q.setPen(pen);
	q.setBrush(data->active
		? st::dialogsOnlineBadgeFgActive
		: st::dialogsOnlineBadgeFg);
	q.drawEllipse(QRectF(
		st::dialogsPhotoSize - skip.x() - size,
		st::dialogsPhotoSize - skip.y() - size,
		size,
		size
	).marginsRemoved({ shrink, shrink, shrink, shrink }));
}

void BasicRow::paintUserpic(
		Painter &p,
		not_null<PeerData*> peer,
		History *historyForCornerBadge,
		crl::time now,
		bool active,
		int fullWidth) const {
	updateCornerBadgeShown(peer);

	const auto shown = _cornerBadgeUserpic
		? _cornerBadgeUserpic->animation.value(_cornerBadgeShown ? 1. : 0.)
		: (_cornerBadgeShown ? 1. : 0.);
	if (!historyForCornerBadge || shown == 0.) {
		peer->paintUserpicLeft(
			p,
			_userpic,
			st::dialogsPadding.x(),
			st::dialogsPadding.y(),
			fullWidth,
			st::dialogsPhotoSize);
		if (!historyForCornerBadge || !_cornerBadgeShown) {
			_cornerBadgeUserpic = nullptr;
		}
		return;
	}
	ensureCornerBadgeUserpic();
	if (_cornerBadgeUserpic->frame.isNull()) {
		_cornerBadgeUserpic->frame = QImage(
			st::dialogsPhotoSize * cRetinaFactor(),
			st::dialogsPhotoSize * cRetinaFactor(),
			QImage::Format_ARGB32_Premultiplied);
		_cornerBadgeUserpic->frame.setDevicePixelRatio(cRetinaFactor());
	}
	const auto key = peer->userpicUniqueKey(_userpic);
	if (_cornerBadgeUserpic->shown != shown
		|| _cornerBadgeUserpic->key != key
		|| _cornerBadgeUserpic->active != active) {
		_cornerBadgeUserpic->shown = shown;
		_cornerBadgeUserpic->key = key;
		_cornerBadgeUserpic->active = active;
		PaintCornerBadgeFrame(_cornerBadgeUserpic.get(), peer, _userpic);
	}
	p.drawImage(st::dialogsPadding, _cornerBadgeUserpic->frame);
	if (historyForCornerBadge->peer->isUser()) {
		return;
	}
	const auto actionPainter = historyForCornerBadge->sendActionPainter();
	const auto bg = active
		? st::dialogsBgActive
		: st::dialogsBg;
	const auto size = st::dialogsCallBadgeSize;
	const auto skip = st::dialogsCallBadgeSkip;
	p.setOpacity(shown);
	p.translate(st::dialogsPadding);
	actionPainter->paintSpeaking(
		p,
		st::dialogsPhotoSize - skip.x() - size,
		st::dialogsPhotoSize - skip.y() - size,
		fullWidth,
		bg,
		now);
	p.translate(-st::dialogsPadding);
	p.setOpacity(1.);
}

Row::Row(Key key, int pos) : _id(key), _pos(pos) {
	if (const auto history = key.history()) {
		updateCornerBadgeShown(history->peer);
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