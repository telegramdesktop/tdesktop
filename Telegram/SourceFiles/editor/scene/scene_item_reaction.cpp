/*
This file is part of Telegram Desktop,
the official desktop application for the Telegram messaging service.

For license and copyright information please follow this link:
https://github.com/telegramdesktop/tdesktop/blob/master/LEGAL
*/
#include "editor/scene/scene_item_reaction.h"

#include "data/data_document.h"
#include "data/data_document_media.h"
#include "data/data_message_reactions.h"
#include "data/data_session.h"
#include "editor/scene/scene.h"
#include "lang/lang_keys.h"
#include "main/main_session.h"
#include "ui/animated_icon.h"
#include "ui/image/image_prepare.h"
#include "ui/painter.h"
#include "ui/widgets/popup_menu.h"

#include "styles/style_menu_icons.h"

#include <QtGui/QPainter>
#include <QtWidgets/QGraphicsSceneMouseEvent>

namespace Editor {
namespace {

constexpr auto kTailBigSize = 0.264;
constexpr auto kTailBigOffset = 0.464;
constexpr auto kTailSmallSize = 0.110;
constexpr auto kTailSmallOffset = 0.697;
constexpr auto kTailBigRotation = -42.29;
constexpr auto kTailSmallRotation = -40.87;
constexpr auto kReactionSize = 0.7;
constexpr auto kTailExtent = 0.12;
constexpr auto kBackgroundCacheSide = 600.;
constexpr auto kIconSide = 512;
constexpr auto kClickSlop = 10.;

[[nodiscard]] DocumentData *ReactionDocument(
		not_null<Main::Session*> session,
		const ::Data::ReactionId &id) {
	if (const auto custom = id.custom()) {
		return session->data().document(custom);
	}
	const auto &reactions = session->data().reactions();
	using Type = ::Data::Reactions::Type;
	for (const auto type : { Type::Active, Type::All }) {
		const auto &list = reactions.list(type);
		const auto i = ranges::find(list, id, &::Data::Reaction::id);
		if (i != end(list)) {
			return i->selectAnimation;
		}
	}
	return nullptr;
}

} // namespace

ItemReaction::ItemReaction(
	not_null<Main::Session*> session,
	::Data::ReactionId reaction,
	bool dark,
	ItemBase::Data data)
: ItemBase(data)
, _session(session)
, _reaction(reaction)
, _dark(dark) {
	setAspectRatio(1.);
	updateIcon();
}

ItemReaction::~ItemReaction() = default;

int ItemReaction::type() const {
	return Type;
}

bool ItemReaction::bakes() const {
	return false;
}

::Data::ReactionId ItemReaction::reaction() const {
	return _reaction;
}

void ItemReaction::setReaction(::Data::ReactionId reaction) {
	if (_reaction == reaction) {
		return;
	}
	_reaction = reaction;
	updateIcon();
	update();
}

bool ItemReaction::dark() const {
	return _dark;
}

void ItemReaction::setDark(bool dark) {
	if (_dark == dark) {
		return;
	}
	_dark = dark;
	_backgroundReady = false;
	update();
}

bool ItemReaction::iconReady() const {
	return _icon && _icon->valid();
}

void ItemReaction::updateIcon() {
	_loadingLifetime.destroy();
	_icon = nullptr;
	_media = nullptr;
	const auto document = ReactionDocument(_session, _reaction);
	if (!document) {
		return;
	}
	_media = document->createMediaView();
	_media->checkStickerLarge();
	if (createIcon()) {
		return;
	}
	_session->downloaderTaskFinished() | rpl::on_next([=] {
		if (!_icon && createIcon()) {
			update();
		}
	}, _loadingLifetime);
}

bool ItemReaction::createIcon() {
	if (!_media || !_media->loaded()) {
		return false;
	}
	_icon = Ui::MakeAnimatedIcon({
		.generator = ::Data::DocumentIconFrameGenerator(_media),
		.sizeOverride = QSize(kIconSide, kIconSide),
	});
	return _icon && _icon->valid();
}

QRectF ItemReaction::boundingRect() const {
	const auto extent = contentRect().width() * kTailExtent;
	return ItemBase::boundingRect().adjusted(
		-extent,
		-extent,
		extent,
		extent);
}

void ItemReaction::cacheBackground() {
	const auto ratio = style::DevicePixelRatio();
	const auto bubble = kBackgroundCacheSide / (1. + 2 * kTailExtent);
	const auto extent = bubble * kTailExtent;
	const auto side = int(std::ceil(bubble + 2 * extent));
	_background = QImage(
		QSize(side, side) * ratio,
		QImage::Format_ARGB32_Premultiplied);
	_background.setDevicePixelRatio(ratio);
	_background.fill(Qt::transparent);

	const auto paintShape = [&](QColor color) {
		auto p = QPainter(&_background);
		auto hq = PainterHighQualityEnabler(p);
		p.setPen(Qt::NoPen);
		p.setCompositionMode(QPainter::CompositionMode_Source);
		p.setBrush(color);
		const auto center = QPointF(side / 2., side / 2.);
		p.drawEllipse(center, bubble / 2., bubble / 2.);

		p.translate(center);
		auto previous = 0.;
		const auto rotate = [&](float64 initial) {
			if (flipped()) {
				initial = 180 - initial;
			}
			auto rotation = -initial;
			while (rotation < 0) {
				rotation += 360;
			}
			while (rotation >= 360) {
				rotation -= 360;
			}
			p.rotate(rotation - previous);
			previous = rotation;
		};
		const auto paintTailPart = [&](float64 offset, float64 size) {
			const auto part = QRectF(-size / 2., -size / 2., size, size);
			p.drawEllipse(part.translated(offset * bubble, 0));
		};
		rotate(kTailBigRotation);
		paintTailPart(kTailBigOffset, bubble * kTailBigSize);
		rotate(kTailSmallRotation);
		paintTailPart(kTailSmallOffset, bubble * kTailSmallSize);
	};
	const auto dark = QColor(0, 0, 0, 128);
	if (!_dark) {
		paintShape(dark);
		_background = Images::Blur(std::move(_background), true);
	}
	paintShape(_dark ? dark : QColor(255, 255, 255));
	_backgroundReady = true;
}

void ItemReaction::paint(
		QPainter *p,
		const QStyleOptionGraphicsItem *option,
		QWidget *w) {
	const auto rect = contentRect();
	const auto bubble = rect.width();
	if (bubble > 0.) {
		if (!_backgroundReady) {
			cacheBackground();
		}
		auto hq = PainterHighQualityEnabler(*p);
		const auto extent = bubble * kTailExtent;
		p->drawImage(
			rect.adjusted(-extent, -extent, extent, extent),
			_background);
		if (iconReady()) {
			const auto side = bubble * kReactionSize;
			const auto center = rect.center();
			p->drawImage(
				QRectF(
					center.x() - side / 2.,
					center.y() - side / 2.,
					side,
					side),
				_icon->frame(QColor()));
		}
	}
	ItemBase::paint(p, option, w);
}

void ItemReaction::mousePressEvent(QGraphicsSceneMouseEvent *event) {
	_pressedSelected = isSelected() && (event->button() == Qt::LeftButton);
	_pressPosition = event->scenePos();
	ItemBase::mousePressEvent(event);
}

void ItemReaction::mouseReleaseEvent(QGraphicsSceneMouseEvent *event) {
	ItemBase::mouseReleaseEvent(event);
	const auto moved = (event->scenePos() - _pressPosition).manhattanLength();
	if (_pressedSelected
		&& (event->button() == Qt::LeftButton)
		&& (moved < kClickSlop)) {
		requestPick();
	}
	_pressedSelected = false;
}

void ItemReaction::mouseDoubleClickEvent(QGraphicsSceneMouseEvent *event) {
	requestPick();
	event->accept();
}

void ItemReaction::requestPick() {
	if (const auto s = static_cast<Scene*>(scene())) {
		s->requestReactionPick(this);
	}
}

void ItemReaction::performFlip() {
	_backgroundReady = false;
	update();
}

std::shared_ptr<ItemBase> ItemReaction::duplicate(
		ItemBase::Data data) const {
	return std::make_shared<ItemReaction>(
		_session,
		_reaction,
		_dark,
		std::move(data));
}

void ItemReaction::fillContextMenu(not_null<Ui::PopupMenu*> menu) {
	menu->addAction(
		(_dark
			? tr::lng_photo_editor_text_style_opaque(tr::now)
			: tr::lng_photo_editor_text_style_semi_transparent(tr::now)),
		[=] { setDark(!_dark); },
		_dark
			? &st::mediaMenuIconTextStyleOpaque
			: &st::mediaMenuIconTextStyleSemiTransparent);
}

} // namespace Editor
