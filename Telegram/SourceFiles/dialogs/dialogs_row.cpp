/*
This file is part of Telegram Desktop,
the official desktop application for the Telegram messaging service.

For license and copyright information please follow this link:
https://github.com/telegramdesktop/tdesktop/blob/master/LEGAL
*/
#include "dialogs/dialogs_row.h"

#include "ui/effects/ripple_animation.h"
#include "ui/text/text_options.h"
#include "ui/text/text_utilities.h"
#include "ui/painter.h"
#include "dialogs/dialogs_entry.h"
#include "dialogs/ui/dialogs_video_userpic.h"
#include "dialogs/ui/dialogs_layout.h"
#include "data/data_folder.h"
#include "data/data_forum.h"
#include "data/data_session.h"
#include "data/data_peer_values.h"
#include "history/history.h"
#include "history/history_item.h"
#include "lang/lang_keys.h"
#include "base/unixtime.h"
#include "mainwidget.h"
#include "styles/style_dialogs.h"

namespace Dialogs {
namespace {

[[nodiscard]] TextWithEntities ComposeFolderListEntryText(
		not_null<Data::Folder*> folder) {
	const auto &list = folder->lastHistories();
	if (list.empty()) {
		return {};
	}

	const auto count = std::max(
		int(list.size()),
		folder->chatsList()->fullSize().current());

	const auto throwAwayLastName = (list.size() > 1)
		&& (count == list.size() + 1);
	auto &&peers = ranges::views::all(
		list
	) | ranges::views::take(
		list.size() - (throwAwayLastName ? 1 : 0)
	);
	const auto wrapName = [](not_null<History*> history) {
		const auto name = history->peer->name();
		return TextWithEntities{
			.text = name,
			.entities = (history->chatListBadgesState().unread
				? EntitiesInText{
					{ EntityType::Semibold, 0, int(name.size()), QString() },
					{ EntityType::PlainLink, 0, int(name.size()), QString() },
				}
				: EntitiesInText{}),
		};
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
				wrapName(*i),
				Ui::Text::WithEntities);
		}
		return result;
	}();
	return (shown < count)
		? tr::lng_archived_last(
			tr::now,
			lt_count,
			(count - shown),
			lt_chats,
			accumulated,
			Ui::Text::WithEntities)
		: accumulated;
}

} // namespace

BasicRow::BasicRow() = default;
BasicRow::~BasicRow() = default;

void BasicRow::addRipple(
		QPoint origin,
		QSize size,
		Fn<void()> updateCallback) {
	if (!_ripple) {
		auto mask = Ui::RippleAnimation::RectMask(size);
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
		QPainter &p,
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

void BasicRow::paintUserpic(
		Painter &p,
		not_null<PeerData*> peer,
		Ui::VideoUserpic *videoUserpic,
		History *historyForCornerBadge,
		const Ui::PaintContext &context) const {
	PaintUserpic(
		p,
		peer,
		videoUserpic,
		_userpic,
		context.st->padding.left(),
		context.st->padding.top(),
		context.width,
		context.st->photoSize,
		context.paused);
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
	_listEntryCache.setMarkedText(
		st::dialogsTextStyle,
		ComposeFolderListEntryText(folder),
		// Use rich options as long as the entry text does not have user text.
		Ui::ItemTextDefaultOptions());
}

void Row::setCornerBadgeShown(
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

void Row::updateCornerBadgeShown(
		not_null<PeerData*> peer,
		Fn<void()> updateCallback) const {
	const auto user = peer->asUser();
	const auto now = user ? base::unixtime::now() : TimeId();
	const auto shown = [&] {
		if (user) {
			return Data::IsUserOnline(user, now);
		} else if (const auto channel = peer->asChannel()) {
			return Data::ChannelHasActiveCall(channel);
		}
		return false;
	}();
	setCornerBadgeShown(shown, std::move(updateCallback));
	if (shown && user) {
		peer->owner().watchForOffline(user, now);
	}
}

void Row::ensureCornerBadgeUserpic() const {
	if (_cornerBadgeUserpic) {
		return;
	}
	_cornerBadgeUserpic = std::make_unique<CornerBadgeUserpic>();
}

void Row::PaintCornerBadgeFrame(
		not_null<CornerBadgeUserpic*> data,
		not_null<PeerData*> peer,
		Ui::VideoUserpic *videoUserpic,
		std::shared_ptr<Data::CloudImageView> &view,
		const Ui::PaintContext &context) {
	data->frame.fill(Qt::transparent);

	Painter q(&data->frame);
	PaintUserpic(
		q,
		peer,
		videoUserpic,
		view,
		0,
		0,
		data->frame.width() / data->frame.devicePixelRatio(),
		context.st->photoSize,
		context.paused);

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
		context.st->photoSize - skip.x() - size,
		context.st->photoSize - skip.y() - size,
		size,
		size
	).marginsRemoved({ shrink, shrink, shrink, shrink }));
}

void Row::paintUserpic(
		Painter &p,
		not_null<PeerData*> peer,
		Ui::VideoUserpic *videoUserpic,
		History *historyForCornerBadge,
		const Ui::PaintContext &context) const {
	updateCornerBadgeShown(peer);

	const auto shown = _cornerBadgeUserpic
		? _cornerBadgeUserpic->animation.value(_cornerBadgeShown ? 1. : 0.)
		: (_cornerBadgeShown ? 1. : 0.);
	if (!historyForCornerBadge || shown == 0.) {
		BasicRow::paintUserpic(
			p,
			peer,
			videoUserpic,
			historyForCornerBadge,
			context);
		if (!historyForCornerBadge || !_cornerBadgeShown) {
			_cornerBadgeUserpic = nullptr;
		}
		return;
	}
	ensureCornerBadgeUserpic();
	const auto ratio = style::DevicePixelRatio();
	const auto added = std::max({
		-st::dialogsCallBadgeSkip.x(),
		-st::dialogsCallBadgeSkip.y(),
		0 });
	const auto frameSide = (context.st->photoSize + added)
		* style::DevicePixelRatio();
	const auto frameSize = QSize(frameSide, frameSide);
	if (_cornerBadgeUserpic->frame.size() != frameSize) {
		_cornerBadgeUserpic->frame = QImage(
			frameSize,
			QImage::Format_ARGB32_Premultiplied);
		_cornerBadgeUserpic->frame.setDevicePixelRatio(ratio);
	}
	const auto key = peer->userpicUniqueKey(userpicView());
	const auto frameIndex = videoUserpic ? videoUserpic->frameIndex() : -1;
	if (_cornerBadgeUserpic->shown != shown
		|| _cornerBadgeUserpic->key != key
		|| _cornerBadgeUserpic->active != context.active
		|| _cornerBadgeUserpic->frameIndex != frameIndex
		|| videoUserpic) {
		_cornerBadgeUserpic->shown = shown;
		_cornerBadgeUserpic->key = key;
		_cornerBadgeUserpic->active = context.active;
		_cornerBadgeUserpic->frameIndex = frameIndex;
		PaintCornerBadgeFrame(
			_cornerBadgeUserpic.get(),
			peer,
			videoUserpic,
			userpicView(),
			context);
	}
	p.drawImage(
		context.st->padding.left(),
		context.st->padding.top(),
		_cornerBadgeUserpic->frame);
	if (historyForCornerBadge->peer->isUser()) {
		return;
	}
	const auto actionPainter = historyForCornerBadge->sendActionPainter();
	const auto bg = context.active
		? st::dialogsBgActive
		: st::dialogsBg;
	const auto size = st::dialogsCallBadgeSize;
	const auto skip = st::dialogsCallBadgeSkip;
	p.setOpacity(shown);
	p.translate(context.st->padding.left(), context.st->padding.top());
	actionPainter->paintSpeaking(
		p,
		context.st->photoSize - skip.x() - size,
		context.st->photoSize - skip.y() - size,
		context.width,
		bg,
		context.now);
	p.translate(-context.st->padding.left(), -context.st->padding.top());
	p.setOpacity(1.);
}

FakeRow::FakeRow(
	Key searchInChat,
	not_null<HistoryItem*> item,
	Fn<void()> repaint)
: _searchInChat(searchInChat)
, _item(item)
, _repaint(std::move(repaint)) {
	invalidateTopic();
}

void FakeRow::invalidateTopic() {
	_topic = _item->topic();
	if (_topic) {
		return;
	} else if (const auto rootId = _item->topicRootId()) {
		if (const auto forum = _item->history()->asForum()) {
			if (!forum->topicDeleted(rootId)) {
				forum->requestTopic(rootId, crl::guard(this, [=] {
					_topic = _item->topic();
					if (_topic) {
						_repaint();
					}
				}));
			}
		}
	}
}

const Ui::Text::String &FakeRow::name() const {
	if (_name.isEmpty()) {
		const auto from = _searchInChat
			? _item->displayFrom()
			: nullptr;
		const auto peer = from ? from : _item->history()->peer.get();
		_name.setText(
			st::semiboldTextStyle,
			peer->name(),
			Ui::NameTextOptions());
	}
	return _name;
}

} // namespace Dialogs
