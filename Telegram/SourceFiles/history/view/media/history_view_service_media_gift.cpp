/*
This file is part of Telegram Desktop,
the official desktop application for the Telegram messaging service.

For license and copyright information please follow this link:
https://github.com/telegramdesktop/tdesktop/blob/master/LEGAL
*/
#include "history/view/media/history_view_service_media_gift.h"

#include "chat_helpers/stickers_gift_box_pack.h"
#include "core/click_handler_types.h" // ClickHandlerContext
#include "data/data_document.h"
#include "data/data_media_types.h"
#include "history/history.h"
#include "history/history_item.h"
#include "history/history_item_components.h"
#include "history/view/history_view_cursor_state.h"
#include "history/view/history_view_element.h"
#include "lang/lang_keys.h"
#include "lottie/lottie_common.h"
#include "lottie/lottie_single_player.h"
#include "main/main_session.h"
#include "settings/settings_premium.h" // Settings::ShowGiftPremium
#include "ui/chat/chat_style.h"
#include "ui/effects/ripple_animation.h"
#include "window/window_session_controller.h"
#include "styles/style_chat.h"
#include "styles/style_premium.h"
#include "styles/style_settings.h"

namespace HistoryView {
namespace {

[[nodiscard]] QString FormatGiftMonths(int months) {
	return (months < 12)
		? tr::lng_premium_gift_duration_months(tr::now, lt_count, months)
		: tr::lng_premium_gift_duration_years(
			tr::now,
			lt_count,
			std::round(months / 12.));
}

} // namespace

MediaGift::MediaGift(
	not_null<Element*> parent,
	not_null<Data::MediaGiftBox*> gift)
: Media(parent)
, _parent(parent)
, _gift(gift)
, _size(st::msgServiceGiftBoxSize)
, _innerSize(_size - QSize(0, st::msgServiceGiftBoxTopSkip))
, _button([&] {
	auto result = Button();
	result.repaint = [=] { repaint(); };
	result.text.setText(
		st::semiboldTextStyle,
		tr::lng_sticker_premium_view(tr::now));

	const auto height = st::msgServiceGiftBoxButtonHeight;
	const auto &margins = st::msgServiceGiftBoxButtonMargins;
	result.size = QSize(
		result.text.maxWidth()
			+ height
			+ margins.left()
			+ margins.right(),
		height);

	const auto from = _gift->from();
	const auto to = _parent->data()->history()->peer;
	const auto months = _gift->months();
	result.link = std::make_shared<LambdaClickHandler>([=](
			ClickContext context) {
		const auto my = context.other.value<ClickHandlerContext>();
		if (const auto controller = my.sessionWindow.get()) {
			const auto me = (from->id == controller->session().userPeerId());
			Settings::ShowGiftPremium(controller, me ? to : from, months, me);
		}
	});

	return result;
}())
, _title(
	st::settingsSubsectionTitle.style,
	tr::lng_premium_summary_title(tr::now))
, _subtitle(
	st::premiumPreviewAbout.style,
	FormatGiftMonths(gift->months())) {
}

MediaGift::~MediaGift() = default;

QSize MediaGift::countOptimalSize() {
	return _size;
}

QSize MediaGift::countCurrentSize(int newWidth) {
	return _size;
}

void MediaGift::draw(Painter &p, const PaintContext &context) const {
	p.translate(0, st::msgServiceGiftBoxTopSkip);

	PainterHighQualityEnabler hq(p);
	const auto radius = st::msgServiceGiftBoxRadius;
	p.setPen(Qt::NoPen);
	p.setBrush(context.st->msgServiceBg());
	p.drawRoundedRect(QRect(QPoint(), _innerSize), radius, radius);

	{
		p.setPen(context.st->msgServiceFg());
		const auto &padding = st::msgServiceGiftBoxTitlePadding;
		const auto titleTop = padding.top();
		_title.draw(p, 0, titleTop, _innerSize.width(), style::al_top);
		const auto subtitleTop = titleTop
			+ _title.minHeight()
			+ padding.bottom();
		_subtitle.draw(p, 0, subtitleTop, _innerSize.width(), style::al_top);
	}

	{
		const auto position = buttonRect().topLeft();
		p.translate(position);

		p.setPen(Qt::NoPen);
		p.setBrush(context.st->msgServiceBg()); // ?
		_button.drawBg(p);
		p.setPen(context.st->msgServiceFg());
		if (_button.ripple) {
			const auto opacity = p.opacity();
			p.setOpacity(st::historyPollRippleOpacity);
			_button.ripple->paint(
				p,
				0,
				0,
				width(),
				&context.messageStyle()->msgWaveformInactive->c);
			p.setOpacity(opacity);
		}
		_button.text.draw(
			p,
			0,
			(_button.size.height() - _button.text.minHeight()) / 2,
			_button.size.width(),
			style::al_top);

		p.translate(-position);
	}

	if (_sticker) {
		_sticker->draw(p, context, stickerRect());
	} else {
		ensureStickerCreated();
	}

	p.translate(0, -st::msgServiceGiftBoxTopSkip);
}

TextState MediaGift::textState(QPoint point, StateRequest request) const {
	auto result = TextState(_parent);
	{
		const auto rect = buttonRect();
		if (rect.contains(point)) {
			result.link = _button.link;
			_button.lastPoint = point - rect.topLeft();
		}
	}
	return result;
}

bool MediaGift::toggleSelectionByHandlerClick(
		const ClickHandlerPtr &p) const {
	return false;
}

bool MediaGift::dragItemByHandler(const ClickHandlerPtr &p) const {
	return false;
}

void MediaGift::clickHandlerPressedChanged(
		const ClickHandlerPtr &handler,
		bool pressed) {
	if (!handler) {
		return;
	}

	if (handler == _button.link) {
		_button.toggleRipple(pressed);
	}
}

void MediaGift::stickerClearLoopPlayed() {
	if (_sticker) {
		_sticker->stickerClearLoopPlayed();
	}
}

std::unique_ptr<Lottie::SinglePlayer> MediaGift::stickerTakeLottie(
		not_null<DocumentData*> data,
		const Lottie::ColorReplacements *replacements) {
	return _sticker
		? _sticker->stickerTakeLottie(data, replacements)
		: nullptr;
}

bool MediaGift::needsBubble() const {
	return false;
}

bool MediaGift::customInfoLayout() const {
	return false;
}

bool MediaGift::hasHeavyPart() const {
	return (_sticker ? _sticker->hasHeavyPart() : false);
}

void MediaGift::unloadHeavyPart() {
	if (_sticker) {
		_sticker->unloadHeavyPart();
	}
}

void MediaGift::ensureStickerCreated() const {
	if (_sticker) {
		return;
	}
	const auto &session = _parent->data()->history()->session();
	auto &packs = session.giftBoxStickersPacks();
	if (const auto document = packs.lookup(_gift->months())) {
		if (const auto sticker = document->sticker()) {
			const auto skipPremiumEffect = false;
			_sticker.emplace(_parent, document, skipPremiumEffect, _parent);
			_sticker->setDiceIndex(sticker->alt, 1);
			_sticker->initSize();
		}
	}
}

QRect MediaGift::buttonRect() const {
	const auto &padding = st::msgServiceGiftBoxButtonPadding;
	const auto position = QPoint(
		(width() - _button.size.width()) / 2,
		height() - padding.bottom() - _button.size.height());
	return QRect(position, _button.size);
}

QRect MediaGift::stickerRect() const {
	const auto &size = st::msgServiceGiftBoxStickerSize;
	const auto top = st::msgServiceGiftBoxStickerTop;
	return QRect(QPoint((width() - size.width()) / 2, top), size);
}

void MediaGift::Button::toggleRipple(bool pressed) {
	if (pressed) {
		const auto linkWidth = size.width();
		const auto linkHeight = size.height();
		if (!ripple) {
			const auto drawMask = [&](QPainter &p) { drawBg(p); };
			auto mask = Ui::RippleAnimation::maskByDrawer(
				QSize(linkWidth, linkHeight),
				false,
				drawMask);
			ripple = std::make_unique<Ui::RippleAnimation>(
				st::defaultRippleAnimation,
				std::move(mask),
				repaint);
		}
		ripple->add(lastPoint);
	} else if (ripple) {
		ripple->lastStop();
	}
}

void MediaGift::Button::drawBg(QPainter &p) const {
	const auto radius = size.height() / 2.;
	p.drawRoundedRect(0, 0, size.width(), size.height(), radius, radius);
}

} // namespace HistoryView
