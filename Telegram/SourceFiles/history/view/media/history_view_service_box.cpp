/*
This file is part of Telegram Desktop,
the official desktop application for the Telegram messaging service.

For license and copyright information please follow this link:
https://github.com/telegramdesktop/tdesktop/blob/master/LEGAL
*/
#include "history/view/media/history_view_service_box.h"

#include "core/ui_integration.h"
#include "history/view/media/history_view_sticker_player_abstract.h"
#include "history/view/history_view_cursor_state.h"
#include "history/view/history_view_element.h"
#include "history/view/history_view_text_helper.h"
#include "history/history.h"
#include "lang/lang_keys.h"
#include "ui/chat/chat_style.h"
#include "ui/effects/animation_value.h"
#include "ui/effects/premium_stars_colored.h"
#include "ui/effects/ripple_animation.h"
#include "ui/text/text_utilities.h"
#include "ui/painter.h"
#include "ui/rect.h"
#include "ui/power_saving.h"
#include "styles/style_chat.h"
#include "styles/style_premium.h"
#include "styles/style_layers.h"

namespace HistoryView {

int ServiceBoxContent::width() {
	return st::msgServiceGiftBoxSize.width();
}

ServiceBox::ServiceBox(
	not_null<Element*> parent,
	std::unique_ptr<ServiceBoxContent> content)
: Media(parent)
, _parent(parent)
, _content(std::move(content))
, _button({ .link = _content->createViewLink() })
, _maxWidth(_content->width()
	- st::msgPadding.left()
	- st::msgPadding.right())
, _title(
	st::defaultSubsectionTitle.style,
	_content->title(),
	kMarkupTextOptions,
	_maxWidth,
	Core::MarkedTextContext{
		.session = &parent->history()->session(),
		.customEmojiRepaint = [parent] { parent->customEmojiRepaint(); },
	})
, _subtitle(
	st::premiumPreviewAbout.style,
	Ui::Text::Filtered(
		_content->subtitle(),
		{
			EntityType::Bold,
			EntityType::StrikeOut,
			EntityType::Underline,
			EntityType::Italic,
			EntityType::Spoiler,
			EntityType::CustomEmoji,
		}),
	kMarkupTextOptions,
	_maxWidth,
	Core::MarkedTextContext{
		.session = &parent->history()->session(),
		.customEmojiRepaint = [parent] { parent->customEmojiRepaint(); },
	})
, _size(
	_content->width(),
	(st::msgServiceGiftBoxTopSkip
		+ _content->top()
		+ _content->size().height()
		+ st::msgServiceGiftBoxTitlePadding.top()
		+ (_title.isEmpty()
			? 0
			: (_title.countHeight(_maxWidth)
				+ st::msgServiceGiftBoxTitlePadding.bottom()))
		+ _subtitle.countHeight(_maxWidth)
		+ (!_content->button()
			? 0
			: (_content->buttonSkip() + st::msgServiceGiftBoxButtonHeight))
		+ st::msgServiceGiftBoxButtonMargins.bottom()))
, _innerSize(_size - QSize(0, st::msgServiceGiftBoxTopSkip)) {
	InitElementTextPart(_parent, _subtitle);
	if (auto text = _content->button()) {
		_button.repaint = [=] { repaint(); };
		std::move(text) | rpl::start_with_next([=](QString value) {
			_button.text.setText(st::semiboldTextStyle, value);
			const auto height = st::msgServiceGiftBoxButtonHeight;
			const auto &padding = st::msgServiceGiftBoxButtonPadding;
			const auto empty = _button.size.isEmpty();
			_button.size = QSize(
				(_button.text.maxWidth()
					+ height
					+ padding.left()
					+ padding.right()),
				height);
			if (!empty) {
				repaint();
			}
		}, _lifetime);
	}
	if (_content->buttonMinistars()) {
		_button.stars = std::make_unique<Ui::Premium::ColoredMiniStars>(
			[=](const QRect &) { repaint(); },
			Ui::Premium::MiniStars::Type::SlowStars);
		_button.lastFg = std::make_unique<QColor>();
	}
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
	p.drawRoundedRect(Rect(_innerSize), radius, radius);

	if (_button.stars) {
		const auto &c = context.st->msgServiceFg()->c;
		if ((*_button.lastFg) != c) {
			_button.lastFg->setRgb(c.red(), c.green(), c.blue());
			const auto padding = _button.size.height() / 2;
			_button.stars->setColorOverride(QGradientStops{
				{ 0., anim::with_alpha(c, .3) },
				{ 1., c },
			});
			_button.stars->setCenter(
				Rect(_button.size) - QMargins(padding, 0, padding, 0));
		}
	}

	const auto content = contentRect();
	auto top = content.top() + content.height();
	{
		p.setPen(context.st->msgServiceFg());
		const auto &padding = st::msgServiceGiftBoxTitlePadding;
		top += padding.top();
		if (!_title.isEmpty()) {
			_title.draw(p, {
				.position = QPoint(st::msgPadding.left(), top),
				.availableWidth = _maxWidth,
				.align = style::al_top,
				.palette = &context.st->serviceTextPalette(),
				.spoiler = Ui::Text::DefaultSpoilerCache(),
				.now = context.now,
				.pausedEmoji = context.paused || On(PowerSaving::kEmojiChat),
				.pausedSpoiler = context.paused || On(PowerSaving::kChatSpoiler),
			});
			top += _title.countHeight(_maxWidth) + padding.bottom();
		}
		_parent->prepareCustomEmojiPaint(p, context, _subtitle);
		_subtitle.draw(p, {
			.position = QPoint(st::msgPadding.left(), top),
			.availableWidth = _maxWidth,
			.align = style::al_top,
			.palette = &context.st->serviceTextPalette(),
			.spoiler = Ui::Text::DefaultSpoilerCache(),
			.now = context.now,
			.pausedEmoji = context.paused || On(PowerSaving::kEmojiChat),
			.pausedSpoiler = context.paused || On(PowerSaving::kChatSpoiler),
		});
		top += _subtitle.countHeight(_maxWidth) + padding.bottom();
	}

	if (!_button.empty()) {
		const auto position = buttonRect().topLeft();
		p.translate(position);

		p.setPen(Qt::NoPen);
		p.setBrush(context.st->msgServiceBg()); // ?
		if (const auto stars = _button.stars.get()) {
			stars->setPaused(context.paused);
		}
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

	if (const auto tag = _content->cornerTag(context); !tag.isNull()) {
		const auto width = tag.width() / tag.devicePixelRatio();
		p.drawImage(_innerSize.width() - width, 0, tag);
	}

	p.translate(0, -st::msgServiceGiftBoxTopSkip);
}

TextState ServiceBox::textState(QPoint point, StateRequest request) const {
	auto result = TextState(_parent);
	point.setY(point.y() - st::msgServiceGiftBoxTopSkip);
	const auto content = contentRect();
	const auto lookupSubtitleLink = [&] {
		auto top = content.top() + content.height();
		const auto &padding = st::msgServiceGiftBoxTitlePadding;
		top += padding.top();
		if (!_title.isEmpty()) {
			top += _title.countHeight(_maxWidth) + padding.bottom();
		}
		auto subtitleRequest = request.forText();
		subtitleRequest.align = style::al_top;
		const auto state = _subtitle.getState(
			point - QPoint(st::msgPadding.left(), top),
			_maxWidth,
			subtitleRequest);
		if (state.link) {
			result.link = state.link;
		}
	};
	if (_button.empty()) {
		if (!_button.link) {
			lookupSubtitleLink();
		} else if (QRect(QPoint(), _innerSize).contains(point)) {
			result.link = _button.link;
		}
	} else {
		const auto rect = buttonRect();
		if (rect.contains(point)) {
			result.link = _button.link;
			_button.lastPoint = point - rect.topLeft();
		} else if (content.contains(point)) {
			if (!_contentLink) {
				_contentLink = _content->createViewLink();
			}
			result.link = _contentLink;
		} else {
			lookupSubtitleLink();
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

void ServiceBox::hideSpoilers() {
	_subtitle.setSpoilerRevealed(false, anim::type::instant);
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
	const auto r = Rect(size);
	p.drawRoundedRect(r, radius, radius);
	if (stars) {
		auto clipPath = QPainterPath();
		clipPath.addRoundedRect(r, radius, radius);
		p.setClipPath(clipPath);
		stars->paint(p);
		p.setClipping(false);
	}
}

} // namespace HistoryView
