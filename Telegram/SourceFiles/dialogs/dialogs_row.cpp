/*
This file is part of Telegram Desktop,
the official desktop application for the Telegram messaging service.

For license and copyright information please follow this link:
https://github.com/telegramdesktop/tdesktop/blob/master/LEGAL
*/
#include "dialogs/dialogs_row.h"

#include "ui/chat/chat_theme.h" // CountAverageColor.
#include "ui/color_contrast.h"
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
#include "data/data_peer_values.h"
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

[[nodiscard]] QImage CornerBadgeTTL(
		not_null<PeerData*> peer,
		Ui::PeerUserpicView &view,
		int photoSize) {
	const auto ttl = peer->messagesTTL();
	if (!ttl) {
		return QImage();
	}
	constexpr auto kBlurRadius = 24;

	const auto ratio = style::DevicePixelRatio();
	const auto fullSize = photoSize;
	const auto blurredFull = Images::BlurLargeImage(
		peer->generateUserpicImage(view, fullSize * ratio, 0),
		kBlurRadius);
	const auto partRect = CornerBadgeTTLRect(fullSize);
	const auto &partSize = partRect.width();
	auto result = [&] {
		auto blurredPart = blurredFull.copy(
			blurredFull.width() - partSize * ratio,
			blurredFull.height() - partSize * ratio,
			partSize * ratio,
			partSize * ratio);
		blurredPart.setDevicePixelRatio(ratio);

		constexpr auto kMinAcceptableContrast = 4.5;
		const auto averageColor = Ui::CountAverageColor(blurredPart);
		const auto contrast = Ui::CountContrast(
			averageColor,
			st::premiumButtonFg->c);
		if (contrast < kMinAcceptableContrast) {
			constexpr auto kDarkerBy = 0.2;
			auto painterPart = QPainter(&blurredPart);
			painterPart.setOpacity(kDarkerBy);
			painterPart.fillRect(
				QRect(QPoint(), partRect.size()),
				Qt::black);
		}
		return Images::Circle(std::move(blurredPart));
	}();

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
	constexpr auto kAngleStart = 90 * 16;
	constexpr auto kAngleSpan = 180 * 16;

	auto pen = QPen(st::premiumButtonFg);
	pen.setJoinStyle(Qt::RoundJoin);
	pen.setCapStyle(Qt::RoundCap);
	pen.setWidthF(style::ConvertScaleExact(kPenWidth));

	q.setPen(pen);
	q.setBrush(Qt::NoBrush);
	q.drawArc(innerRect, kAngleStart, kAngleSpan);

	q.setClipRect(innerRect - QMargins(innerRect.width() / 2, 0, 0, 0));
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

Row::Row(Key key, int index, int top) : _id(key), _top(top), _index(index) {
	if (const auto history = key.history()) {
		updateCornerBadgeShown(history->peer);
	}
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
	} else {
		_height = st::forumTopicRow.height;
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
		not_null<PeerData*> peer,
		Ui::VideoUserpic *videoUserpic,
		Ui::PeerUserpicView &view,
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

	const auto &manager = data->layersManager;
	if (const auto p = manager.progressForLayer(kBottomLayer); p) {
		const auto size = context.st->photoSize;
		if (data->cacheTTL.isNull() && peer->messagesTTL()) {
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

	PainterHighQualityEnabler hq(q);
	q.setCompositionMode(QPainter::CompositionMode_Source);

	const auto size = peer->isUser()
		? st::dialogsOnlineBadgeSize
		: st::dialogsCallBadgeSize;
	const auto stroke = st::dialogsOnlineBadgeStroke;
	const auto skip = peer->isUser()
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

	const auto cornerBadgeShown = !_cornerBadgeUserpic
		? _cornerBadgeShown
		: !_cornerBadgeUserpic->layersManager.isDisplayedNone();
	if (!historyForCornerBadge || !cornerBadgeShown) {
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
	auto key = peer->userpicUniqueKey(userpicView());
	key.first += peer->messagesTTL();
	const auto frameIndex = videoUserpic ? videoUserpic->frameIndex() : -1;
	if (_cornerBadgeUserpic->key != key) {
		_cornerBadgeUserpic->cacheTTL = QImage();
	}
	if (!_cornerBadgeUserpic->layersManager.isFinished()
		|| _cornerBadgeUserpic->key != key
		|| _cornerBadgeUserpic->active != context.active
		|| _cornerBadgeUserpic->frameIndex != frameIndex
		|| videoUserpic) {
		_cornerBadgeUserpic->key = key;
		_cornerBadgeUserpic->active = context.active;
		_cornerBadgeUserpic->frameIndex = frameIndex;
		_cornerBadgeUserpic->layersManager.markFrameShown();
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
	if (_topicJumpRipple) {
		clearRipple();
		_topicJumpRipple = 0;
	}
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
