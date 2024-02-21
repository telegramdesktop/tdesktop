/*
This file is part of Telegram Desktop,
the official desktop application for the Telegram messaging service.

For license and copyright information please follow this link:
https://github.com/telegramdesktop/tdesktop/blob/master/LEGAL
*/
#include "dialogs/dialogs_row.h"

#include "ui/chat/chat_theme.h" // CountAverageColor.
#include "ui/color_contrast.h"
#include "ui/effects/outline_segments.h"
#include "ui/effects/ripple_animation.h"
#include "ui/image/image_prepare.h"
#include "ui/text/format_values.h"
#include "ui/text/text_options.h"
#include "ui/text/text_utilities.h"
#include "ui/painter.h"
#include "dialogs/dialogs_entry.h"
#include "dialogs/ui/dialogs_video_userpic.h"
#include "dialogs/ui/dialogs_layout.h"
#include "data/data_folder.h"
#include "data/data_forum.h"
#include "data/data_session.h"
#include "data/data_stories.h"
#include "data/data_peer_values.h"
#include "data/data_user.h"
#include "history/history.h"
#include "history/history_item.h"
#include "lang/lang_keys.h"
#include "base/unixtime.h"
#include "styles/style_dialogs.h"

namespace Dialogs {
namespace {

constexpr auto kTopLayer = 2;
constexpr auto kBottomLayer = 1;
constexpr auto kNoneLayer = 0;
constexpr auto kBlurRadius = 24;

[[nodiscard]] QImage CornerBadgeTTL(
		not_null<PeerData*> peer,
		Ui::PeerUserpicView &view,
		int photoSize) {
	const auto ttl = peer->messagesTTL();
	if (!ttl) {
		return QImage();
	}
	const auto ratio = style::DevicePixelRatio();
	const auto fullSize = photoSize;
	const auto partRect = CornerBadgeTTLRect(fullSize);
	const auto &partSize = partRect.width();
	const auto partSkip = fullSize - partSize;
	auto result = Images::Circle(BlurredDarkenedPart(
		peer->generateUserpicImage(view, fullSize * ratio, 0),
		QRect(
			QPoint(partSkip, partSkip) * ratio,
			QSize(partSize, partSize) * ratio)));
	result.setDevicePixelRatio(ratio);

	auto q = QPainter(&result);
	PainterHighQualityEnabler hq(q);

	const auto innerRect = QRect(QPoint(), partRect.size())
		- st::dialogsTTLBadgeInnerMargins;
	const auto ttlText = Ui::FormatTTLTiny(ttl);

	q.setFont(st::dialogsScamFont);
	q.setPen(st::premiumButtonFg);
	q.drawText(
		innerRect,
		(ttlText.size() > 2) ? ttlText.mid(0, 2) : ttlText,
		style::al_center);

	constexpr auto kPenWidth = 1.5;

	const auto penWidth = style::ConvertScaleExact(kPenWidth);
	auto pen = QPen(st::premiumButtonFg);
	pen.setJoinStyle(Qt::RoundJoin);
	pen.setCapStyle(Qt::RoundCap);
	pen.setWidthF(penWidth);

	q.setPen(pen);
	q.setBrush(Qt::NoBrush);
	q.drawArc(innerRect, arc::kQuarterLength, arc::kHalfLength);

	q.setClipRect(innerRect
		- QMargins(innerRect.width() / 2, -penWidth, -penWidth, -penWidth));
	pen.setStyle(Qt::DotLine);
	q.setPen(pen);
	q.drawEllipse(innerRect);

	return result;
}

} // namespace

QRect CornerBadgeTTLRect(int photoSize) {
	const auto &partSize = st::dialogsTTLBadgeSize;
	return QRect(
		photoSize - partSize + st::dialogsTTLBadgeSkip.x(),
		photoSize - partSize + st::dialogsTTLBadgeSkip.y(),
		partSize,
		partSize);
}

QImage BlurredDarkenedPart(QImage image, QRect part) {
	auto blurred = Images::BlurLargeImage(
		std::move(image),
		kBlurRadius).copy(part);

	constexpr auto kMinAcceptableContrast = 4.5;
	const auto averageColor = Ui::CountAverageColor(blurred);
	const auto contrast = Ui::CountContrast(
		averageColor,
		st::premiumButtonFg->c);
	if (contrast < kMinAcceptableContrast) {
		constexpr auto kDarkerBy = 0.2;
		auto painterPart = QPainter(&blurred);
		painterPart.setOpacity(kDarkerBy);
		painterPart.fillRect(QRect(QPoint(), part.size()), Qt::black);
	}

	blurred.setDevicePixelRatio(image.devicePixelRatio());
	return blurred;
}

Row::CornerLayersManager::CornerLayersManager() = default;

bool Row::CornerLayersManager::isSameLayer(Layer layer) const {
	return isFinished() && (_nextLayer == layer);
}

void Row::CornerLayersManager::setLayer(
		Layer layer,
		Fn<void()> updateCallback) {
	if (_nextLayer == layer) {
		return;
	}
	_lastFrameShown = false;
	_prevLayer = _nextLayer;
	_nextLayer = layer;
	if (_animation.animating()) {
		_animation.change(
			1.,
			st::dialogsOnlineBadgeDuration * (1. - _animation.value(1.)));
	} else if (updateCallback) {
		_animation.start(
			std::move(updateCallback),
			0.,
			1.,
			st::dialogsOnlineBadgeDuration);
	}
}

float64 Row::CornerLayersManager::progressForLayer(Layer layer) const {
	return (_nextLayer == layer)
		? progress()
		: (_prevLayer == layer)
		? (1. - progress())
		: 0.;
}

float64 Row::CornerLayersManager::progress() const {
	return _animation.value(1.);
}

bool Row::CornerLayersManager::isFinished() const {
	return (progress() == 1.) && _lastFrameShown;
}

void Row::CornerLayersManager::markFrameShown() {
	if (progress() == 1.) {
		_lastFrameShown = true;
	}
}

bool Row::CornerLayersManager::isDisplayedNone() const {
	return (progress() == 1.) && (_nextLayer == 0);
}

BasicRow::BasicRow() = default;
BasicRow::~BasicRow() = default;

void BasicRow::addRipple(
		QPoint origin,
		QSize size,
		Fn<void()> updateCallback) {
	if (!_ripple) {
		addRippleWithMask(
			origin,
			Ui::RippleAnimation::RectMask(size),
			std::move(updateCallback));
	} else {
		_ripple->add(origin);
	}
}

void BasicRow::addRippleWithMask(
		QPoint origin,
		QImage mask,
		Fn<void()> updateCallback) {
	_ripple = std::make_unique<Ui::RippleAnimation>(
		st::dialogsRipple,
		std::move(mask),
		std::move(updateCallback));
	_ripple->add(origin);
}

void BasicRow::clearRipple() {
	_ripple = nullptr;
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
		not_null<Entry*> entry,
		PeerData *peer,
		Ui::VideoUserpic *videoUserpic,
		const Ui::PaintContext &context) const {
	PaintUserpic(p, entry, peer, videoUserpic, _userpic, context);
}

Row::Row(Key key, int index, int top) : _id(key), _top(top), _index(index) {
	if (const auto history = key.history()) {
		updateCornerBadgeShown(history->peer);
	}
}

Row::~Row() {
	clearTopicJumpRipple();
}

void Row::recountHeight(float64 narrowRatio) {
	if (const auto history = _id.history()) {
		_height = history->isForum()
			? anim::interpolate(
				st::forumDialogRow.height,
				st::defaultDialogRow.height,
				narrowRatio)
			: st::defaultDialogRow.height;
	} else if (_id.folder()) {
		_height = st::defaultDialogRow.height;
	} else if (_id.topic()) {
		_height = st::forumTopicRow.height;
	} else {
		_height = st::defaultDialogRow.height;
	}
}

uint64 Row::sortKey(FilterId filterId) const {
	return _id.entry()->sortKeyInChatList(filterId);
}

void Row::setCornerBadgeShown(
		CornerLayersManager::Layer nextLayer,
		Fn<void()> updateCallback) const {
	const auto cornerBadgeShown = (nextLayer ? 1 : 0);
	if (_cornerBadgeShown == cornerBadgeShown) {
		if (!cornerBadgeShown) {
			return;
		} else if (_cornerBadgeUserpic
			&& _cornerBadgeUserpic->layersManager.isSameLayer(nextLayer)) {
			return;
		}
	}
	const_cast<Row*>(this)->_cornerBadgeShown = cornerBadgeShown;
	ensureCornerBadgeUserpic();
	_cornerBadgeUserpic->layersManager.setLayer(
		nextLayer,
		std::move(updateCallback));
	if (!_cornerBadgeShown
		&& _cornerBadgeUserpic
		&& _cornerBadgeUserpic->layersManager.isDisplayedNone()) {
		_cornerBadgeUserpic = nullptr;
	}
}

void Row::updateCornerBadgeShown(
		not_null<PeerData*> peer,
		Fn<void()> updateCallback) const {
	const auto user = peer->asUser();
	const auto now = user ? base::unixtime::now() : TimeId();
	const auto nextLayer = [&] {
		if (user && Data::IsUserOnline(user, now)) {
			return kTopLayer;
		} else if (peer->isChannel()
			&& Data::ChannelHasActiveCall(peer->asChannel())) {
			return kTopLayer;
		} else if (peer->messagesTTL()) {
			return kBottomLayer;
		}
		return kNoneLayer;
	}();
	setCornerBadgeShown(nextLayer, std::move(updateCallback));
	if ((nextLayer == kTopLayer) && user) {
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
		int framePadding,
		not_null<Entry*> entry,
		PeerData *peer,
		Ui::VideoUserpic *videoUserpic,
		Ui::PeerUserpicView &view,
		const Ui::PaintContext &context) {
	data->frame.fill(Qt::transparent);

	Painter q(&data->frame);
	q.translate(framePadding, framePadding);
	auto hq = std::optional<PainterHighQualityEnabler>();
	const auto photoSize = context.st->photoSize;
	const auto storiesCount = data->storiesCount;
	if (storiesCount) {
		hq.emplace(q);
		const auto line = st::dialogsStoriesFull.lineTwice / 2.;
		const auto skip = line * 3 / 2.;
		const auto scale = 1. - (2 * skip / photoSize);
		const auto center = photoSize / 2.;
		q.save();
		q.translate(center, center);
		q.scale(scale, scale);
		q.translate(-center, -center);
	}
	q.translate(-context.st->padding.left(), -context.st->padding.top());
	PaintUserpic(
		q,
		entry,
		peer,
		videoUserpic,
		view,
		context);
	q.translate(context.st->padding.left(), context.st->padding.top());
	if (storiesCount) {
		q.restore();

		const auto outline = QRectF(0, 0, photoSize, photoSize);
		const auto storiesUnreadCount = data->storiesUnreadCount;
		const auto storiesUnreadBrush = [&] {
			if (context.active || !storiesUnreadCount) {
				return st::dialogsUnreadBgMutedActive->b;
			}
			auto gradient = Ui::UnreadStoryOutlineGradient(outline);
			return QBrush(gradient);
		}();
		const auto storiesBrush = context.active
			? st::dialogsUnreadBgMutedActive->b
			: st::dialogsUnreadBgMuted->b;
		const auto storiesUnread = st::dialogsStoriesFull.lineTwice / 2.;
		const auto storiesLine = st::dialogsStoriesFull.lineReadTwice / 2.;
		auto segments = std::vector<Ui::OutlineSegment>();
		segments.reserve(storiesCount);
		const auto storiesReadCount = storiesCount - storiesUnreadCount;
		for (auto i = 0; i != storiesReadCount; ++i) {
			segments.push_back({ storiesBrush, storiesLine });
		}
		for (auto i = 0; i != storiesUnreadCount; ++i) {
			segments.push_back({ storiesUnreadBrush, storiesUnread });
		}
		Ui::PaintOutlineSegments(q, outline, segments);
	}

	const auto &manager = data->layersManager;
	if (const auto p = manager.progressForLayer(kBottomLayer); p > 0.) {
		const auto size = photoSize;
		if (data->cacheTTL.isNull() && peer && peer->messagesTTL()) {
			data->cacheTTL = CornerBadgeTTL(peer, view, size);
		}
		q.setOpacity(p);
		const auto point = CornerBadgeTTLRect(size).topLeft();
		q.drawImage(point, data->cacheTTL);
		q.setOpacity(1.);
	}
	const auto topLayerProgress = manager.progressForLayer(kTopLayer);
	if (!topLayerProgress) {
		return;
	}

	if (!hq) {
		hq.emplace(q);
	}
	q.setCompositionMode(QPainter::CompositionMode_Source);

	const auto online = peer && peer->isUser();
	const auto size = online
		? st::dialogsOnlineBadgeSize
		: st::dialogsCallBadgeSize;
	const auto stroke = st::dialogsOnlineBadgeStroke;
	const auto skip = online
		? st::dialogsOnlineBadgeSkip
		: st::dialogsCallBadgeSkip;
	const auto shrink = (size / 2) * (1. - topLayerProgress);

	auto pen = QPen(Qt::transparent);
	pen.setWidthF(stroke * topLayerProgress);
	q.setPen(pen);
	q.setBrush(data->active
		? st::dialogsOnlineBadgeFgActive
		: st::dialogsOnlineBadgeFg);
	q.drawEllipse(QRectF(
		photoSize - skip.x() - size,
		photoSize - skip.y() - size,
		size,
		size
	).marginsRemoved({ shrink, shrink, shrink, shrink }));
}

void Row::paintUserpic(
		Painter &p,
		not_null<Entry*> entry,
		PeerData *peer,
		Ui::VideoUserpic *videoUserpic,
		const Ui::PaintContext &context) const {
	if (peer) {
		updateCornerBadgeShown(peer);
	}

	const auto cornerBadgeShown = !_cornerBadgeUserpic
		? _cornerBadgeShown
		: !_cornerBadgeUserpic->layersManager.isDisplayedNone();
	const auto storiesPeer = peer
		? ((peer->isUser() || peer->isChannel()) ? peer : nullptr)
		: nullptr;
	const auto storiesFolder = peer ? nullptr : _id.folder();
	const auto storiesHas = storiesPeer
		? storiesPeer->hasActiveStories()
		: storiesFolder
		? storiesFolder->storiesCount()
		: false;
	if (!cornerBadgeShown && !storiesHas) {
		BasicRow::paintUserpic(p, entry, peer, videoUserpic, context);
		if (!peer || !_cornerBadgeShown) {
			_cornerBadgeUserpic = nullptr;
		}
		return;
	}
	ensureCornerBadgeUserpic();
	const auto ratio = style::DevicePixelRatio();
	const auto framePadding = std::max({
		-st::dialogsCallBadgeSkip.x(),
		-st::dialogsCallBadgeSkip.y(),
		st::lineWidth * 2 });
	const auto frameSide = (2 * framePadding + context.st->photoSize)
		* ratio;
	const auto frameSize = QSize(frameSide, frameSide);
	const auto storiesSource = (storiesHas && storiesPeer)
		? storiesPeer->owner().stories().source(storiesPeer->id)
		: nullptr;
	const auto storiesCountReal = storiesSource
		? int(storiesSource->ids.size())
		: storiesFolder
		? storiesFolder->storiesCount()
		: storiesHas
		? 1
		: 0;
	const auto storiesUnreadCountReal = storiesSource
		? storiesSource->unreadCount()
		: storiesFolder
		? storiesFolder->storiesUnreadCount()
		: (storiesPeer && storiesPeer->hasUnreadStories())
		? 1
		: 0;
	const auto limit = Ui::kOutlineSegmentsMax;
	const auto storiesCount = std::min(storiesCountReal, limit);
	const auto storiesUnreadCount = std::min(storiesUnreadCountReal, limit);
	if (_cornerBadgeUserpic->frame.size() != frameSize) {
		_cornerBadgeUserpic->frame = QImage(
			frameSize,
			QImage::Format_ARGB32_Premultiplied);
		_cornerBadgeUserpic->frame.setDevicePixelRatio(ratio);
	}
	auto key = peer ? peer->userpicUniqueKey(userpicView()) : InMemoryKey();
	key.first += peer ? peer->messagesTTL() : 0;
	const auto frameIndex = videoUserpic ? videoUserpic->frameIndex() : -1;
	const auto paletteVersionReal = style::PaletteVersion();
	const auto paletteVersion = (paletteVersionReal & ((1 << 17) - 1));
	const auto active = context.active ? 1 : 0;
	const auto keyChanged = (_cornerBadgeUserpic->key != key)
		|| (_cornerBadgeUserpic->paletteVersion != paletteVersion);
	if (keyChanged) {
		_cornerBadgeUserpic->cacheTTL = QImage();
	}
	if (keyChanged
		|| !_cornerBadgeUserpic->layersManager.isFinished()
		|| _cornerBadgeUserpic->active != active
		|| _cornerBadgeUserpic->frameIndex != frameIndex
		|| _cornerBadgeUserpic->storiesCount != storiesCount
		|| _cornerBadgeUserpic->storiesUnreadCount != storiesUnreadCount
		|| videoUserpic) {
		_cornerBadgeUserpic->key = key;
		_cornerBadgeUserpic->paletteVersion = paletteVersion;
		_cornerBadgeUserpic->active = active;
		_cornerBadgeUserpic->storiesCount = storiesCount;
		_cornerBadgeUserpic->storiesUnreadCount = storiesUnreadCount;
		_cornerBadgeUserpic->frameIndex = frameIndex;
		_cornerBadgeUserpic->layersManager.markFrameShown();
		PaintCornerBadgeFrame(
			_cornerBadgeUserpic.get(),
			framePadding,
			_id.entry(),
			peer,
			videoUserpic,
			userpicView(),
			context);
	}
	p.drawImage(
		context.st->padding.left() - framePadding,
		context.st->padding.top() - framePadding,
		_cornerBadgeUserpic->frame);
	const auto history = _id.history();
	if (!history || history->peer->isUser()) {
		return;
	}
	const auto actionPainter = history->sendActionPainter();
	const auto bg = context.active
		? st::dialogsBgActive
		: st::dialogsBg;
	const auto size = st::dialogsCallBadgeSize;
	const auto skip = st::dialogsCallBadgeSkip;
	p.setOpacity(
		_cornerBadgeUserpic->layersManager.progressForLayer(kTopLayer));
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

bool Row::lookupIsInTopicJump(int x, int y) const {
	const auto history = this->history();
	return history && history->lastItemDialogsView().isInTopicJump(x, y);
}

void Row::stopLastRipple() {
	BasicRow::stopLastRipple();
	const auto history = this->history();
	const auto view = history ? &history->lastItemDialogsView() : nullptr;
	if (view) {
		view->stopLastRipple();
	}
}

void Row::clearRipple() {
	BasicRow::clearRipple();
	clearTopicJumpRipple();
}

void Row::addTopicJumpRipple(
		QPoint origin,
		not_null<Ui::TopicJumpCache*> topicJumpCache,
		Fn<void()> updateCallback) {
	const auto history = this->history();
	const auto view = history ? &history->lastItemDialogsView() : nullptr;
	if (view) {
		view->addTopicJumpRipple(
			origin,
			topicJumpCache,
			std::move(updateCallback));
		_topicJumpRipple = 1;
	}
}

void Row::clearTopicJumpRipple() {
	if (!_topicJumpRipple) {
		return;
	}
	const auto history = this->history();
	const auto view = history ? &history->lastItemDialogsView() : nullptr;
	if (view) {
		view->clearRipple();
	}
	_topicJumpRipple = 0;
}

bool Row::topicJumpRipple() const {
	return _topicJumpRipple != 0;
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
