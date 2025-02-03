/*
This file is part of Telegram Desktop,
the official desktop application for the Telegram messaging service.

For license and copyright information please follow this link:
https://github.com/telegramdesktop/tdesktop/blob/master/LEGAL
*/
#include "history/view/media/history_view_unique_gift.h"

#include "boxes/star_gift_box.h"
#include "chat_helpers/stickers_lottie.h"
#include "core/click_handler_types.h"
#include "data/stickers/data_custom_emoji.h"
#include "data/data_media_types.h"
#include "data/data_session.h"
#include "data/data_star_gift.h"
#include "history/view/media/history_view_media_generic.h"
#include "history/view/media/history_view_premium_gift.h"
#include "history/view/history_view_cursor_state.h"
#include "history/view/history_view_element.h"
#include "history/history.h"
#include "history/history_item.h"
#include "info/peer_gifts/info_peer_gifts_common.h"
#include "lang/lang_keys.h"
#include "main/main_session.h"
#include "settings/settings_credits_graphics.h"
#include "ui/chat/chat_style.h"
#include "ui/effects/premium_stars_colored.h"
#include "ui/effects/ripple_animation.h"
#include "ui/layers/generic_box.h"
#include "ui/text/text_utilities.h"
#include "ui/painter.h"
#include "ui/power_saving.h"
#include "ui/rect.h"
#include "window/window_session_controller.h"
#include "styles/style_chat.h"
#include "styles/style_credits.h"

namespace HistoryView {
namespace {

class TextPartColored final : public MediaGenericTextPart {
public:
	TextPartColored(
		TextWithEntities text,
		QMargins margins,
		QColor color,
		const style::TextStyle &st = st::defaultTextStyle,
		const base::flat_map<uint16, ClickHandlerPtr> &links = {},
		const std::any &context = {});

private:
	void setupPen(
		Painter &p,
		not_null<const MediaGeneric*> owner,
		const PaintContext &context) const override;

	QColor _color;

};

class AttributeTable final : public MediaGenericPart {
public:
	struct Entry {
		QString label;
		QString value;
	};

	AttributeTable(
		std::vector<Entry> entries,
		QMargins margins,
		QColor labelColor);

	void draw(
		Painter &p,
		not_null<const MediaGeneric*> owner,
		const PaintContext &context,
		int outerWidth) const override;

	QSize countOptimalSize() override;
	QSize countCurrentSize(int newWidth) override;

private:
	struct Part {
		Ui::Text::String label;
		Ui::Text::String value;
	};

	std::vector<Part> _parts;
	QMargins _margins;
	QColor _labelColor;
	int _valueLeft = 0;

};

class ButtonPart final : public MediaGenericPart {
public:
	ButtonPart(
		const QString &text,
		QMargins margins,
		Fn<void()> repaint,
		ClickHandlerPtr link,
		QColor bg = QColor(0, 0, 0, 0));

	void draw(
		Painter &p,
		not_null<const MediaGeneric*> owner,
		const PaintContext &context,
		int outerWidth) const override;
	TextState textState(
		QPoint point,
		StateRequest request,
		int outerWidth) const override;

	void clickHandlerPressedChanged(
		const ClickHandlerPtr &p,
		bool pressed) override;

	QSize countOptimalSize() override;
	QSize countCurrentSize(int newWidth) override;

private:
	Ui::Text::String _text;
	QMargins _margins;
	QColor _bg;
	QSize _size;

	ClickHandlerPtr _link;
	std::unique_ptr<Ui::RippleAnimation> _ripple;
	mutable Ui::Premium::ColoredMiniStars _stars;
	mutable std::optional<QColor> _starsLastColor;
	Fn<void()> _repaint;

	mutable QPoint _lastPoint;

};

ButtonPart::ButtonPart(
	const QString &text,
	QMargins margins,
	Fn<void()> repaint,
	ClickHandlerPtr link,
	QColor bg)
: _text(st::semiboldTextStyle, text)
, _margins(margins)
, _bg(bg)
, _size(
	(_text.maxWidth()
		+ st::msgServiceGiftBoxButtonHeight
		+ st::msgServiceGiftBoxButtonPadding.left()
		+ st::msgServiceGiftBoxButtonPadding.right()),
	st::msgServiceGiftBoxButtonHeight)
, _link(std::move(link))
, _stars([=](const QRect &) {
	repaint();
}, Ui::Premium::MiniStars::Type::SlowStars)
, _repaint(std::move(repaint)) {
}

void ButtonPart::draw(
		Painter &p,
		not_null<const MediaGeneric*> owner,
		const PaintContext &context,
		int outerWidth) const {
	PainterHighQualityEnabler hq(p);

	const auto customColors = (_bg.alpha() > 0);

	const auto position = QPoint(
		(outerWidth - width()) / 2 + _margins.left(),
		_margins.top());
	p.translate(position);

	p.setPen(Qt::NoPen);
	p.setBrush(customColors ? QBrush(_bg) : context.st->msgServiceBg());
	const auto radius = _size.height() / 2.;
	const auto r = Rect(_size);
	p.drawRoundedRect(r, radius, radius);

	auto white = QColor(255, 255, 255);
	const auto fg = customColors ? white : context.st->msgServiceFg()->c;
	if (!_starsLastColor || *_starsLastColor != fg) {
		_starsLastColor = fg;
		_stars.setColorOverride(QGradientStops{
			{ 0., anim::with_alpha(fg, .3) },
			{ 1., fg },
		});
		const auto padding = _size.height() / 2;
		_stars.setCenter(
			Rect(_size) - QMargins(padding, 0, padding, 0));
	}

	auto clipPath = QPainterPath();
	clipPath.addRoundedRect(r, radius, radius);
	p.setClipPath(clipPath);
	_stars.setPaused(context.paused);
	_stars.paint(p);
	p.setClipping(false);

	if (_ripple) {
		const auto opacity = p.opacity();
		const auto ripple = customColors
			? anim::with_alpha(fg, .3)
			: context.messageStyle()->msgWaveformInactive->c;
		p.setOpacity(st::historyPollRippleOpacity);
		_ripple->paint(
			p,
			0,
			0,
			width(),
			&ripple);
		p.setOpacity(opacity);
	}

	p.setPen(fg);
	_text.draw(
		p,
		0,
		(_size.height() - _text.minHeight()) / 2,
		_size.width(),
		style::al_top);

	p.translate(-position);
}

TextState ButtonPart::textState(
		QPoint point,
		StateRequest request,
		int outerWidth) const {
	point -= QPoint{
		(outerWidth - width()) / 2 + _margins.left(),
		_margins.top()
	};
	if (QRect(QPoint(), _size).contains(point)) {
		auto result = TextState();
		result.link = _link;
		_lastPoint = point;
		return result;
	}
	return {};
}

void ButtonPart::clickHandlerPressedChanged(
		const ClickHandlerPtr &p,
		bool pressed) {
	if (p != _link) {
		return;
	} else if (pressed) {
		if (!_ripple) {
			const auto radius = _size.height() / 2;
			_ripple = std::make_unique<Ui::RippleAnimation>(
				st::defaultRippleAnimation,
				Ui::RippleAnimation::RoundRectMask(_size, radius),
				_repaint);
		}
		_ripple->add(_lastPoint);
	} else if (_ripple) {
		_ripple->lastStop();
	}
}

QSize ButtonPart::countOptimalSize() {
	return {
		_margins.left() + _size.width() + _margins.right(),
		_margins.top() + _size.height() + _margins.bottom(),
	};
}

QSize ButtonPart::countCurrentSize(int newWidth) {
	return optimalSize();
}

TextPartColored::TextPartColored(
	TextWithEntities text,
	QMargins margins,
	QColor color,
	const style::TextStyle &st,
	const base::flat_map<uint16, ClickHandlerPtr> &links,
	const std::any &context)
: MediaGenericTextPart(text, margins, st, links, context)
, _color(color) {
}

void TextPartColored::setupPen(
		Painter &p,
		not_null<const MediaGeneric*> owner,
		const PaintContext &context) const {
	p.setPen(_color);
}

AttributeTable::AttributeTable(
	std::vector<Entry> entries,
	QMargins margins,
	QColor labelColor)
: _margins(margins)
, _labelColor(labelColor) {
	for (const auto &entry : entries) {
		_parts.emplace_back();
		auto &part = _parts.back();
		part.label.setText(st::defaultTextStyle, entry.label);
		part.value.setMarkedText(
			st::defaultTextStyle,
			Ui::Text::Bold(entry.value));
	}
}

void AttributeTable::draw(
		Painter &p,
		not_null<const MediaGeneric*> owner,
		const PaintContext &context,
		int outerWidth) const {
	const auto labelRight = _valueLeft - st::chatUniqueTableSkip;
	const auto palette = &context.st->serviceTextPalette();
	auto top = _margins.top();
	const auto paint = [&](
			const Ui::Text::String &text,
			int left,
			int availableWidth,
			style::align align) {
		text.draw(p, {
			.position = { left, top },
			.outerWidth = outerWidth,
			.availableWidth = availableWidth,
			.align = align,
			.palette = palette,
			.spoiler = Ui::Text::DefaultSpoilerCache(),
			.now = context.now,
			.pausedEmoji = context.paused || On(PowerSaving::kEmojiChat),
			.pausedSpoiler = context.paused || On(PowerSaving::kChatSpoiler),
			.elisionLines = 1,
		});
	};
	const auto forLabel = labelRight - _margins.left();
	const auto forValue = width() - _valueLeft - _margins.right();
	const auto white = QColor(255, 255, 255);
	for (const auto &part : _parts) {
		p.setPen(_labelColor);
		paint(part.label, _margins.left(), forLabel, style::al_topright);
		p.setPen(white);
		paint(part.value, _valueLeft, forValue, style::al_topleft);
		top += st::normalFont->height + st::chatUniqueRowSkip;
	}
}

QSize AttributeTable::countOptimalSize() {
	auto maxLabel = 0;
	auto maxValue = 0;
	for (const auto &part : _parts) {
		maxLabel = std::max(maxLabel, part.label.maxWidth());
		maxValue = std::max(maxValue, part.value.maxWidth());
	}
	const auto skip = st::chatUniqueTableSkip;
	const auto row = st::normalFont->height + st::chatUniqueRowSkip;
	const auto height = int(_parts.size()) * row - st::chatUniqueRowSkip;
	return {
		_margins.left() + maxLabel + skip + maxValue + _margins.right(),
		_margins.top() + height + _margins.bottom(),
	};
}

QSize AttributeTable::countCurrentSize(int newWidth) {
	const auto skip = st::chatUniqueTableSkip;
	const auto width = newWidth - _margins.left() - _margins.right() - skip;
	auto maxLabel = 0;
	auto maxValue = 0;
	for (const auto &part : _parts) {
		maxLabel = std::max(maxLabel, part.label.maxWidth());
		maxValue = std::max(maxValue, part.value.maxWidth());
	}
	if (width <= 0 || !maxLabel) {
		_valueLeft = _margins.left();
	} else if (!maxValue) {
		_valueLeft = newWidth - _margins.right();
	} else {
		_valueLeft = _margins.left()
			+ int((int64(maxLabel) * width) / (maxLabel + maxValue))
			+ skip;
	}
	return { newWidth, minHeight() };
}

}; // namespace

auto GenerateUniqueGiftMedia(
	not_null<Element*> parent,
	Element *replacing,
	std::shared_ptr<Data::UniqueGift> gift)
-> Fn<void(
		not_null<MediaGeneric*>,
		Fn<void(std::unique_ptr<MediaGenericPart>)>)> {
	return [=](
			not_null<MediaGeneric*> media,
			Fn<void(std::unique_ptr<MediaGenericPart>)> push) {
		auto pushText = [&](
				TextWithEntities text,
				const style::TextStyle &st,
				QColor color,
				QMargins margins) {
			if (text.empty()) {
				return;
			}
			push(std::make_unique<TextPartColored>(
				std::move(text),
				margins,
				color,
				st));
		};

		const auto item = parent->data();
		const auto itemMedia = item->media();
		const auto fields = itemMedia ? itemMedia->gift() : nullptr;
		const auto upgrade = fields && fields->upgrade;
		const auto outgoing = upgrade ? !item->out() : item->out();

		const auto white = QColor(255, 255, 255);
		const auto sticker = [=] {
			using Tag = ChatHelpers::StickerLottieSize;
			return StickerInBubblePart::Data{
				.sticker = gift->model.document,
				.size = st::chatIntroStickerSize,
				.cacheTag = Tag::ChatIntroHelloSticker,
			};
		};
		push(std::make_unique<StickerInBubblePart>(
			parent,
			replacing,
			sticker,
			st::chatUniqueStickerPadding));
		const auto peer = parent->history()->peer;
		pushText(
			Ui::Text::Bold(peer->isSelf()
				? tr::lng_action_gift_self_subtitle(tr::now)
				: peer->isServiceUser()
				? tr::lng_gift_link_label_gift(tr::now)
				: (outgoing
					? tr::lng_action_gift_sent_subtitle
					: tr::lng_action_gift_got_subtitle)(
						tr::now,
						lt_user,
						peer->shortName())),
			st::chatUniqueTitle,
			white,
			st::chatUniqueTitlePadding);
		pushText(
			Ui::Text::Bold(Data::UniqueGiftName(*gift)),
			st::defaultTextStyle,
			gift->backdrop.textColor,
			st::chatUniqueTextPadding);

		auto attributes = std::vector<AttributeTable::Entry>{
			{ tr::lng_gift_unique_model(tr::now), gift->model.name },
			{ tr::lng_gift_unique_backdrop(tr::now), gift->backdrop.name },
			{ tr::lng_gift_unique_symbol(tr::now), gift->pattern.name },
		};
		push(std::make_unique<AttributeTable>(
			std::move(attributes),
			st::chatUniqueTextPadding,
			gift->backdrop.textColor));

		auto link = OpenStarGiftLink(parent->data());
		push(std::make_unique<ButtonPart>(
			tr::lng_sticker_premium_view(tr::now),
			st::chatUniqueButtonPadding,
			[=] { parent->repaint(); },
			std::move(link),
			anim::with_alpha(gift->backdrop.patternColor, 0.75)));
	};
}

auto UniqueGiftBg(
	not_null<Element*> view,
	std::shared_ptr<Data::UniqueGift> gift)
-> Fn<void(
		Painter&,
		const Ui::ChatPaintContext&,
		not_null<const MediaGeneric*>)> {
	struct State {
		QImage bg;
		base::flat_map<float64, QImage> cache;
		std::unique_ptr<Ui::Text::CustomEmoji> pattern;
		QImage badgeCache;
		Info::PeerGifts::GiftBadge badgeKey;
	};
	const auto state = std::make_shared<State>();
	state->pattern = view->history()->owner().customEmojiManager().create(
		gift->pattern.document,
		[=] { view->repaint(); },
		Data::CustomEmojiSizeTag::Large);
	[[maybe_unused]] const auto preload = state->pattern->ready();

	return [=](
			Painter &p,
			const Ui::ChatPaintContext &context,
			not_null<const MediaGeneric*> media) {
		auto hq = PainterHighQualityEnabler(p);
		p.setPen(Qt::NoPen);
		const auto webpreview = (media.get() != view->media());
		const auto thickness = webpreview ? 0 : st::chatUniqueGiftBorder * 2;
		const auto radius = webpreview
			? st::roundRadiusLarge
			: (st::msgServiceGiftBoxRadius - thickness);
		const auto full = QRect(0, 0, media->width(), media->height());
		const auto inner = full.marginsRemoved(
			{ thickness, thickness, thickness, thickness });
		if (!webpreview) {
			auto pen = context.st->msgServiceBg()->p;
			pen.setWidthF(thickness);
			p.setPen(pen);
			p.setBrush(Qt::transparent);
			p.drawRoundedRect(inner, radius, radius);
		}
		auto gradient = QRadialGradient(inner.center(), inner.height() / 2);
		gradient.setStops({
			{ 0., gift->backdrop.centerColor },
			{ 1., gift->backdrop.edgeColor },
		});
		p.setBrush(gradient);
		p.setPen(Qt::NoPen);
		p.drawRoundedRect(inner, radius, radius);

		const auto width = media->width();
		const auto shift = width / 12;
		const auto doubled = width + 2 * shift;
		const auto top = (webpreview ? 2 : 1) * (-shift);
		const auto outer = QRect(-shift, top, doubled, doubled);
		p.setClipRect(inner);
		Ui::PaintPoints(
			p,
			Ui::PatternPoints(),
			state->cache,
			state->pattern.get(),
			*gift,
			outer);
		p.setClipping(false);

		const auto add = webpreview ? 0 : style::ConvertScale(2);
		p.setClipRect(
			inner.x() - add,
			inner.y() - add,
			inner.width() + 2 * add,
			inner.height() + 2 * add);
		auto badge = Info::PeerGifts::GiftBadge{
			.text = tr::lng_gift_collectible_tag(tr::now),
			.bg1 = gift->backdrop.edgeColor,
			.bg2 = gift->backdrop.patternColor,
			.fg = gift->backdrop.textColor,
		};
		if (state->badgeCache.isNull() || state->badgeKey != badge) {
			state->badgeKey = badge;
			state->badgeCache = ValidateRotatedBadge(badge, add);
		}
		const auto badgeRatio = state->badgeCache.devicePixelRatio();
		const auto badgeWidth = state->badgeCache.width() / badgeRatio;
		p.drawImage(
			inner.x() + inner.width() + add - badgeWidth,
			inner.y() - add,
			state->badgeCache);
		p.setClipping(false);
	};
}

auto GenerateUniqueGiftPreview(
	not_null<Element*> parent,
	Element *replacing,
	std::shared_ptr<Data::UniqueGift> gift)
-> Fn<void(
		not_null<MediaGeneric*>,
		Fn<void(std::unique_ptr<MediaGenericPart>)>)> {
	return [=](
			not_null<MediaGeneric*> media,
			Fn<void(std::unique_ptr<MediaGenericPart>)> push) {
		const auto sticker = [=] {
			using Tag = ChatHelpers::StickerLottieSize;
			return StickerInBubblePart::Data{
				.sticker = gift->model.document,
				.size = st::chatIntroStickerSize,
				.cacheTag = Tag::ChatIntroHelloSticker,
			};
		};
		push(std::make_unique<StickerInBubblePart>(
			parent,
			replacing,
			sticker,
			st::chatUniquePreviewPadding));
	};
}

std::unique_ptr<MediaGenericPart> MakeGenericButtonPart(
		const QString &text,
		QMargins margins,
		Fn<void()> repaint,
		ClickHandlerPtr link,
		QColor bg) {
	return std::make_unique<ButtonPart>(text, margins, repaint, link, bg);
}

} // namespace HistoryView
