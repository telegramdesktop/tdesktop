/*
This file is part of Telegram Desktop,
the official desktop application for the Telegram messaging service.

For license and copyright information please follow this link:
https://github.com/telegramdesktop/tdesktop/blob/master/LEGAL
*/
#include "history/view/media/history_view_service_box.h"
//
#include "history/view/history_view_cursor_state.h"
#include "history/view/media/history_view_sticker_player_abstract.h"
#include "lang/lang_keys.h"
#include "ui/chat/chat_style.h"
#include "ui/effects/ripple_animation.h"
#include "ui/text/text_utilities.h"
#include "ui/painter.h"
#include "styles/style_chat.h"
#include "styles/style_premium.h"
#include "styles/style_layers.h"

namespace HistoryView {

ServiceBox::ServiceBox(
	not_null<Element*> parent,
	std::unique_ptr<ServiceBoxContent> content)
: Media(parent)
, _parent(parent)
, _content(std::move(content))
, _button([&] {
	auto result = Button();
	result.link = _content->createViewLink();

	const auto text = _content->button();
	if (text.isEmpty()) {
		return result;
	}
	result.repaint = [=] { repaint(); };
	result.text.setText(st::semiboldTextStyle, _content->button());

	const auto height = st::msgServiceGiftBoxButtonHeight;
	const auto &padding = st::msgServiceGiftBoxButtonPadding;
	result.size = QSize(
		result.text.maxWidth()
			+ height
			+ padding.left()
			+ padding.right(),
		height);

	return result;
}())
, _maxWidth(st::msgServiceGiftBoxSize.width()
	- st::msgPadding.left()
	- st::msgPadding.right())
, _title(
	st::defaultSubsectionTitle.style,
	_content->title(),
	kDefaultTextOptions,
	_maxWidth)
, _subtitle(
	st::premiumPreviewAbout.style,
	Ui::Text::Filtered(
		_content->subtitle(),
		{
			EntityType::Bold,
			EntityType::StrikeOut,
			EntityType::Underline,
			EntityType::Italic,
		}),
	kMarkupTextOptions,
	_maxWidth)
, _size(
	st::msgServiceGiftBoxSize.width(),
	(st::msgServiceGiftBoxTopSkip
		+ _content->top()
		+ _content->size().height()
		+ st::msgServiceGiftBoxTitlePadding.top()
		+ (_title.isEmpty()
			? 0
			: (_title.countHeight(_maxWidth)
				+ st::msgServiceGiftBoxTitlePadding.bottom()))
		+ _subtitle.countHeight(_maxWidth)
		+ (_button.empty()
			? 0
			: (_content->buttonSkip() + _button.size.height()))
		+ st::msgServiceGiftBoxButtonMargins.bottom()))
, _innerSize(_size - QSize(0, st::msgServiceGiftBoxTopSkip)) {
}

ServiceBox::~ServiceBox() = default;

QSize ServiceBox::countOptimalSize() {
	return _size;
}

QSize ServiceBox::countCurrentSize(int newWidth) {
	return _size;
}

void ServiceBox::draw(Painter &p, const PaintContext &context) const {
	p.translate(0, st::msgServiceGiftBoxTopSkip);

	PainterHighQualityEnabler hq(p);
	const auto radius = st::msgServiceGiftBoxRadius;
	p.setPen(Qt::NoPen);
	p.setBrush(context.st->msgServiceBg());
	p.drawRoundedRect(QRect(QPoint(), _innerSize), radius, radius);

	const auto content = contentRect();
	auto top = content.top() + content.height();
	{
		p.setPen(context.st->msgServiceFg());
		const auto &padding = st::msgServiceGiftBoxTitlePadding;
		top += padding.top();
		if (!_title.isEmpty()) {
			_title.draw(p, st::msgPadding.left(), top, _maxWidth, style::al_top);
			top += _title.countHeight(_maxWidth) + padding.bottom();
		}
		_subtitle.draw(p, st::msgPadding.left(), top, _maxWidth, style::al_top);
		top += _subtitle.countHeight(_maxWidth) + padding.bottom();
	}

	if (!_button.empty()) {
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

	_content->draw(p, context, content);

	p.translate(0, -st::msgServiceGiftBoxTopSkip);
}

TextState ServiceBox::textState(QPoint point, StateRequest request) const {
	auto result = TextState(_parent);
	if (_button.empty()) {
		if (QRect(QPoint(), _innerSize).contains(point)) {
			result.link = _button.link;
		}
	} else {
		const auto rect = buttonRect();
		if (rect.contains(point)) {
			result.link = _button.link;
			_button.lastPoint = point - rect.topLeft();
		} else if (contentRect().contains(point)) {
			if (!_contentLink) {
				_contentLink = _content->createViewLink();
			}
			result.link = _contentLink;
		}
	}
	return result;
}

bool ServiceBox::toggleSelectionByHandlerClick(
		const ClickHandlerPtr &p) const {
	return false;
}

bool ServiceBox::dragItemByHandler(const ClickHandlerPtr &p) const {
	return false;
}

void ServiceBox::clickHandlerPressedChanged(
		const ClickHandlerPtr &handler,
		bool pressed) {
	if (!handler) {
		return;
	}

	if (handler == _button.link) {
		_button.toggleRipple(pressed);
	}
}

void ServiceBox::stickerClearLoopPlayed() {
	_content->stickerClearLoopPlayed();
}

std::unique_ptr<StickerPlayer> ServiceBox::stickerTakePlayer(
		not_null<DocumentData*> data,
		const Lottie::ColorReplacements *replacements) {
	return _content->stickerTakePlayer(data, replacements);
}

bool ServiceBox::needsBubble() const {
	return false;
}

bool ServiceBox::customInfoLayout() const {
	return false;
}

bool ServiceBox::hasHeavyPart() const {
	return _content->hasHeavyPart();
}

void ServiceBox::unloadHeavyPart() {
	_content->unloadHeavyPart();
}

QRect ServiceBox::buttonRect() const {
	const auto &padding = st::msgServiceGiftBoxButtonMargins;
	const auto position = QPoint(
		(width() - _button.size.width()) / 2,
		height() - padding.bottom() - _button.size.height());
	return QRect(position, _button.size);
}

QRect ServiceBox::contentRect() const {
	const auto size = _content->size();
	const auto top = _content->top();
	return QRect(QPoint((width() - size.width()) / 2, top), size);
}

void ServiceBox::Button::toggleRipple(bool pressed) {
	if (empty()) {
		return;
	} else if (pressed) {
		const auto linkWidth = size.width();
		const auto linkHeight = size.height();
		if (!ripple) {
			const auto drawMask = [&](QPainter &p) { drawBg(p); };
			auto mask = Ui::RippleAnimation::MaskByDrawer(
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

bool ServiceBox::Button::empty() const {
	return text.isEmpty();
}

void ServiceBox::Button::drawBg(QPainter &p) const {
	const auto radius = size.height() / 2.;
	p.drawRoundedRect(0, 0, size.width(), size.height(), radius, radius);
}

} // namespace HistoryView
