/*
This file is part of Telegram Desktop,
the official desktop application for the Telegram messaging service.

For license and copyright information please follow this link:
https://github.com/telegramdesktop/tdesktop/blob/master/LEGAL
*/
#include "history/view/history_view_service_message.h"

#include "history/view/media/history_view_media.h"
#include "history/view/reactions/history_view_reactions.h"
#include "history/view/history_view_cursor_state.h"
#include "history/history.h"
#include "history/history_item.h"
#include "history/history_item_components.h"
#include "history/history_item_helpers.h"
#include "data/data_abstract_structure.h"
#include "data/data_chat.h"
#include "data/data_channel.h"
#include "info/profile/info_profile_cover.h"
#include "ui/chat/chat_style.h"
#include "ui/effects/reaction_fly_animation.h"
#include "ui/text/text_options.h"
#include "ui/painter.h"
#include "ui/power_saving.h"
#include "ui/ui_utility.h"
#include "mainwidget.h"
#include "menu/menu_ttl_validator.h"
#include "data/data_forum_topic.h"
#include "lang/lang_keys.h"
#include "styles/style_chat.h"
#include "styles/style_info.h"

namespace HistoryView {
namespace {

TextParseOptions EmptyLineOptions = {
	TextParseMultiline, // flags
	4096, // maxw
	1, // maxh
	Qt::LayoutDirectionAuto, // lang-dependent
};

enum CircleMask {
	NormalMask     = 0x00,
	InvertedMask   = 0x01,
};
enum CircleMaskMultiplier {
	MaskMultiplier = 0x04,
};
enum CornerVerticalSide {
	CornerTop      = 0x00,
	CornerBottom   = 0x02,
};
enum CornerHorizontalSide {
	CornerLeft     = 0x00,
	CornerRight    = 0x01,
};

enum class SideStyle {
	Rounded,
	Plain,
	Inverted,
};

// Returns amount of pixels already painted vertically (so you can skip them in the complex rect shape).
int PaintBubbleSide(
		Painter &p,
		not_null<const Ui::ChatStyle*> st,
		int x,
		int y,
		int width,
		SideStyle style,
		CornerVerticalSide side) {
	if (style == SideStyle::Rounded) {
		const auto &corners = st->serviceBgCornersNormal();
		const auto left = corners.p[(side == CornerTop) ? 0 : 2];
		const auto leftWidth = left.width() / style::DevicePixelRatio();
		p.drawPixmap(x, y, left);

		const auto right = corners.p[(side == CornerTop) ? 1 : 3];
		const auto rightWidth = right.width() / style::DevicePixelRatio();
		p.drawPixmap(x + width - rightWidth, y, right);

		const auto cornerHeight = left.height() / style::DevicePixelRatio();
		p.fillRect(
			x + leftWidth,
			y,
			width - leftWidth - rightWidth,
			cornerHeight,
			st->msgServiceBg());
		return cornerHeight;
	} else if (style == SideStyle::Inverted) {
		// CornerLeft and CornerRight are inverted in the top part.
		const auto &corners = st->serviceBgCornersInverted();
		const auto left = corners.p[(side == CornerTop) ? 1 : 2];
		const auto leftWidth = left.width() / style::DevicePixelRatio();
		p.drawPixmap(x - leftWidth, y, left);

		const auto right = corners.p[(side == CornerTop) ? 0 : 3];
		p.drawPixmap(x + width, y, right);
	}
	return 0;
}

void PaintBubblePart(
		Painter &p,
		not_null<const Ui::ChatStyle*> st,
		int x,
		int y,
		int width,
		int height,
		SideStyle topStyle,
		SideStyle bottomStyle,
		bool forceShrink = false) {
	if ((topStyle == SideStyle::Inverted)
		|| (bottomStyle == SideStyle::Inverted)
		|| forceShrink) {
		width -= Ui::HistoryServiceMsgInvertedShrink() * 2;
		x += Ui::HistoryServiceMsgInvertedShrink();
	}

	if (int skip = PaintBubbleSide(p, st, x, y, width, topStyle, CornerTop)) {
		y += skip;
		height -= skip;
	}
	int bottomSize = 0;
	if (bottomStyle == SideStyle::Rounded) {
		bottomSize = Ui::HistoryServiceMsgRadius();
	} else if (bottomStyle == SideStyle::Inverted) {
		bottomSize = Ui::HistoryServiceMsgInvertedRadius();
	}
	const auto skip = PaintBubbleSide(
		p,
		st,
		x,
		y + height - bottomSize,
		width,
		bottomStyle,
		CornerBottom);
	if (skip) {
		height -= skip;
	}

	p.fillRect(x, y, width, height, st->msgServiceBg());
}

void PaintPreparedDate(
		Painter &p,
		const style::color &bg,
		const Ui::CornersPixmaps &corners,
		const style::color &fg,
		const QString &dateText,
		int dateTextWidth,
		int y,
		int w,
		bool chatWide) {
	int left = st::msgServiceMargin.left();
	const auto maxwidth = chatWide
		? std::min(w, WideChatWidth())
		: w;
	w = maxwidth - st::msgServiceMargin.left() - st::msgServiceMargin.left();

	left += (w - dateTextWidth - st::msgServicePadding.left() - st::msgServicePadding.right()) / 2;
	int height = st::msgServicePadding.top() + st::msgServiceFont->height + st::msgServicePadding.bottom();
	ServiceMessagePainter::PaintBubble(
		p,
		bg,
		corners,
		QRect(
			left,
			y + st::msgServiceMargin.top(),
			dateTextWidth
				+ st::msgServicePadding.left()
				+ st::msgServicePadding.left(),
			height));

	p.setFont(st::msgServiceFont);
	p.setPen(fg);
	p.drawText(
		left + st::msgServicePadding.left(),
		(y
			+ st::msgServiceMargin.top()
			+ st::msgServicePadding.top()
			+ st::msgServiceFont->ascent),
		dateText);
}

bool NeedAboutGroup(not_null<History*> history) {
	if (const auto chat = history->peer->asChat()) {
		return chat->amCreator();
	} else if (const auto channel = history->peer->asMegagroup()) {
		return channel->amCreator();
	}
	return false;
}

void SetText(Ui::Text::String &text, const QString &content) {
	text.setText(st::serviceTextStyle, content, EmptyLineOptions);
}

} // namespace

int WideChatWidth() {
	return st::msgMaxWidth + 2 * st::msgPhotoSkip + 2 * st::msgMargin.left();
}

void ServiceMessagePainter::PaintDate(
		Painter &p,
		not_null<const Ui::ChatStyle*> st,
		const QDateTime &date,
		int y,
		int w,
		bool chatWide) {
	PaintDate(
		p,
		st,
		langDayOfMonthFull(date.date()),
		y,
		w,
		chatWide);
}

void ServiceMessagePainter::PaintDate(
		Painter &p,
		not_null<const Ui::ChatStyle*> st,
		const QString &dateText,
		int y,
		int w,
		bool chatWide) {
	PaintDate(
		p,
		st,
		dateText,
		st::msgServiceFont->width(dateText),
		y,
		w,
		chatWide);
}

void ServiceMessagePainter::PaintDate(
		Painter &p,
		not_null<const Ui::ChatStyle*> st,
		const QString &dateText,
		int dateTextWidth,
		int y,
		int w,
		bool chatWide) {
	PaintPreparedDate(
		p,
		st->msgServiceBg(),
		st->serviceBgCornersNormal(),
		st->msgServiceFg(),
		dateText,
		dateTextWidth,
		y,
		w,
		chatWide);
}

void ServiceMessagePainter::PaintDate(
		Painter &p,
		const style::color &bg,
		const Ui::CornersPixmaps &corners,
		const style::color &fg,
		const QString &dateText,
		int dateTextWidth,
		int y,
		int w,
		bool chatWide) {
	PaintPreparedDate(
		p,
		bg,
		corners,
		fg,
		dateText,
		dateTextWidth,
		y,
		w,
		chatWide);
}

void ServiceMessagePainter::PaintBubble(
		Painter &p,
		not_null<const Ui::ChatStyle*> st,
		QRect rect) {
	PaintBubble(p, st->msgServiceBg(), st->serviceBgCornersNormal(), rect);
}

void ServiceMessagePainter::PaintBubble(
		Painter &p,
		const style::color &bg,
		const Ui::CornersPixmaps &corners,
		QRect rect) {
	Ui::FillRoundRect(p, rect, bg, corners);
}

void ServiceMessagePainter::PaintComplexBubble(
		Painter &p,
		not_null<const Ui::ChatStyle*> st,
		int left,
		int width,
		const Ui::Text::String &text,
		const QRect &textRect) {
	const auto lineWidths = CountLineWidths(text, textRect);

	int y = st::msgServiceMargin.top(), previousRichWidth = 0;
	bool previousShrink = false, forceShrink = false;
	SideStyle topStyle = SideStyle::Rounded, bottomStyle;
	for (int i = 0, count = lineWidths.size(); i < count; ++i) {
		const auto lineWidth = lineWidths[i];
		if (i + 1 < count) {
			const auto nextLineWidth = lineWidths[i + 1];
			if (nextLineWidth > lineWidth) {
				bottomStyle = SideStyle::Inverted;
			} else if (nextLineWidth < lineWidth) {
				bottomStyle = SideStyle::Rounded;
			} else {
				bottomStyle = SideStyle::Plain;
			}
		} else {
			bottomStyle = SideStyle::Rounded;
		}

		auto richWidth = lineWidth + st::msgServicePadding.left() + st::msgServicePadding.right();
		auto richHeight = st::msgServiceFont->height;
		if (topStyle == SideStyle::Rounded) {
			richHeight += st::msgServicePadding.top();
		} else if (topStyle == SideStyle::Inverted) {
			richHeight -= st::msgServicePadding.bottom();
		}
		if (bottomStyle == SideStyle::Rounded) {
			richHeight += st::msgServicePadding.bottom();
		} else if (bottomStyle == SideStyle::Inverted) {
			richHeight -= st::msgServicePadding.top();
		}
		forceShrink = previousShrink && (richWidth == previousRichWidth);
		PaintBubblePart(
			p,
			st,
			left + ((width - richWidth) / 2),
			y,
			richWidth,
			richHeight,
			topStyle,
			bottomStyle,
			forceShrink);
		y += richHeight;

		previousShrink = forceShrink || (topStyle == SideStyle::Inverted) || (bottomStyle == SideStyle::Inverted);
		previousRichWidth = richWidth;

		if (bottomStyle == SideStyle::Inverted) {
			topStyle = SideStyle::Rounded;
		} else if (bottomStyle == SideStyle::Rounded) {
			topStyle = SideStyle::Inverted;
		} else {
			topStyle = SideStyle::Plain;
		}
	}
}

std::vector<int> ServiceMessagePainter::CountLineWidths(
		const Ui::Text::String &text,
		const QRect &textRect) {
	const auto linesCount = qMax(
		textRect.height() / st::msgServiceFont->height,
		1);
	auto result = text.countLineWidths(textRect.width(), {
		.reserve = linesCount,
	});

	const auto minDelta = 2 * (Ui::HistoryServiceMsgRadius()
		+ Ui::HistoryServiceMsgInvertedRadius()
		- Ui::HistoryServiceMsgInvertedShrink());
	for (int i = 0, count = result.size(); i != count; ++i) {
		auto width = qMax(result[i], 0);
		if (i > 0) {
			const auto widthBefore = result[i - 1];
			if (width < widthBefore && width + minDelta > widthBefore) {
				width = widthBefore;
			}
		}
		if (i + 1 < count) {
			const auto widthAfter = result[i + 1];
			if (width < widthAfter && width + minDelta > widthAfter) {
				width = widthAfter;
			}
		}
		if (width > result[i]) {
			result[i] = width;
			if (i > 0) {
				int widthBefore = result[i - 1];
				if (widthBefore != width
					&& widthBefore < width + minDelta
					&& widthBefore + minDelta > width) {
					i -= 2;
				}
			}
		}
	}
	return result;
}

Service::Service(
	not_null<ElementDelegate*> delegate,
	not_null<HistoryItem*> data,
	Element *replacing)
: Element(delegate, data, replacing, Flag::ServiceMessage) {
	setupReactions(replacing);
}

QRect Service::innerGeometry() const {
	return countGeometry();
}

bool Service::consumeHorizontalScroll(QPoint position, int delta) {
	if (const auto media = this->media()) {
		return media->consumeHorizontalScroll(position, delta);
	}
	return false;
}

QRect Service::countGeometry() const {
	auto result = QRect(0, 0, width(), height());
	if (delegate()->elementIsChatWide()) {
		result.setWidth(qMin(result.width(), st::msgMaxWidth + 2 * st::msgPhotoSkip + 2 * st::msgMargin.left()));
	}
	auto margins = st::msgServiceMargin;
	margins.setTop(marginTop());
	return result.marginsRemoved(margins);
}

void Service::animateReaction(Ui::ReactionFlyAnimationArgs &&args) {
	auto g = countGeometry();
	if (g.width() < 1 || isHidden()) {
		return;
	}
	const auto repainter = [=] { repaint(); };

	if (_reactions) {
		const auto reactionsHeight = st::mediaInBubbleSkip + _reactions->height();
		const auto reactionsLeft = 0;
		g.setHeight(g.height() - reactionsHeight);
		const auto reactionsPosition = QPoint(reactionsLeft + g.left(), g.top() + g.height() + st::mediaInBubbleSkip);
		_reactions->animate(args.translated(-reactionsPosition), repainter);
	}
}

QSize Service::performCountCurrentSize(int newWidth) {
	auto newHeight = displayedDateHeight();
	if (const auto bar = Get<UnreadBar>()) {
		newHeight += bar->height();
	}

	data()->resolveDependent();

	if (isHidden()) {
		return { newWidth, newHeight };
	}
	const auto media = this->media();
	const auto mediaDisplayed = media && media->isDisplayed();
	auto contentWidth = newWidth;
	if (mediaDisplayed && media->hideServiceText()) {
		newHeight += st::msgServiceMargin.top()
			+ media->resizeGetHeight(newWidth)
			+ st::msgServiceMargin.bottom();
	} else if (!text().isEmpty()) {
		if (delegate()->elementIsChatWide()) {
			accumulate_min(contentWidth, st::msgMaxWidth + 2 * st::msgPhotoSkip + 2 * st::msgMargin.left());
		}
		contentWidth -= st::msgServiceMargin.left() + st::msgServiceMargin.left(); // two small margins
		if (contentWidth < st::msgServicePadding.left() + st::msgServicePadding.right() + 1) {
			contentWidth = st::msgServicePadding.left() + st::msgServicePadding.right() + 1;
		}

		auto nwidth = qMax(contentWidth - st::msgServicePadding.left() - st::msgServicePadding.right(), 0);
		newHeight += (contentWidth >= maxWidth())
			? minHeight()
			: textHeightFor(nwidth);
		newHeight += st::msgServicePadding.top() + st::msgServicePadding.bottom() + st::msgServiceMargin.top() + st::msgServiceMargin.bottom();
		if (mediaDisplayed) {
			const auto mediaWidth = std::min(media->maxWidth(), nwidth);
			newHeight += st::msgServiceMargin.top()
				+ media->resizeGetHeight(mediaWidth);
		}
	}

	if (_reactions) {
		newHeight += st::mediaInBubbleSkip
			+ _reactions->resizeGetHeight(contentWidth);
		if (hasRightLayout()) {
			_reactions->flipToRight();
		}
	}

	return { newWidth, newHeight };
}

QSize Service::performCountOptimalSize() {
	validateText();

	if (_reactions) {
		_reactions->initDimensions();
	}

	if (const auto media = this->media()) {
		media->initDimensions();
		if (media->hideServiceText()) {
			return { media->maxWidth(), media->minHeight() };
		}
	}
	auto maxWidth = text().maxWidth() + st::msgServicePadding.left() + st::msgServicePadding.right();
	auto minHeight = text().minHeight();
	return { maxWidth, minHeight };
}

bool Service::isHidden() const {
	return Element::isHidden();
}

int Service::marginTop() const {
	auto result = st::msgServiceMargin.top();
	result += displayedDateHeight();
	if (const auto bar = Get<UnreadBar>()) {
		result += bar->height();
	}
	return result;
}

int Service::marginBottom() const {
	return st::msgServiceMargin.bottom();
}

void Service::draw(Painter &p, const PaintContext &context) const {
	auto g = countGeometry();
	if (g.width() < 1) {
		return;
	}

	const auto st = context.st;
	if (const auto bar = Get<UnreadBar>()) {
		auto unreadbarh = bar->height();
		auto dateh = 0;
		if (const auto date = Get<DateBadge>()) {
			dateh = date->height();
		}
		if (context.clip.intersects(QRect(0, dateh, width(), unreadbarh))) {
			p.translate(0, dateh);
			bar->paint(
				p,
				context,
				0,
				width(),
				delegate()->elementIsChatWide());
			p.translate(0, -dateh);
		}
	}

	if (isHidden()) {
		return;
	}

	paintHighlight(p, context, g.height());

	p.setTextPalette(st->serviceTextPalette());

	const auto media = this->media();
	const auto mediaDisplayed = media && media->isDisplayed();
	const auto onlyMedia = (mediaDisplayed && media->hideServiceText());

	if (_reactions) {
		const auto reactionsHeight = st::mediaInBubbleSkip + _reactions->height();
		const auto reactionsLeft = 0;
		g.setHeight(g.height() - reactionsHeight);
		const auto reactionsPosition = QPoint(reactionsLeft + g.left(), g.top() + g.height() + st::mediaInBubbleSkip);
		p.translate(reactionsPosition);
		prepareCustomEmojiPaint(p, context, *_reactions);
		_reactions->paint(p, context, g.width(), context.clip.translated(-reactionsPosition));
		if (context.reactionInfo) {
			context.reactionInfo->position = reactionsPosition;
		}
		p.translate(-reactionsPosition);
	}

	if (!onlyMedia) {
		const auto mediaSkip = mediaDisplayed ? (st::msgServiceMargin.top() + media->height()) : 0;
		const auto trect = QRect(g.left(), g.top(), g.width(), g.height() - mediaSkip)
			- st::msgServicePadding;

		p.translate(0, g.top() - st::msgServiceMargin.top());
		ServiceMessagePainter::PaintComplexBubble(
			p,
			context.st,
			g.left(),
			g.width(),
			text(),
			trect);
		p.translate(0, -g.top() + st::msgServiceMargin.top());

		p.setBrush(Qt::NoBrush);
		p.setPen(st->msgServiceFg());
		p.setFont(st::msgServiceFont);
		prepareCustomEmojiPaint(p, context, text());
		text().draw(p, {
			.position = trect.topLeft(),
			.availableWidth = trect.width(),
			.align = style::al_top,
			.palette = &st->serviceTextPalette(),
			.spoiler = Ui::Text::DefaultSpoilerCache(),
			.now = context.now,
			.pausedEmoji = context.paused || On(PowerSaving::kEmojiChat),
			.pausedSpoiler = context.paused || On(PowerSaving::kChatSpoiler),
			.fullWidthSelection = false,
			.selection = context.selection,
		});
	}
	if (mediaDisplayed) {
		const auto left = g.left() + (g.width() - media->width()) / 2;
		const auto top = g.top() + (onlyMedia ? 0 : (g.height() - media->height()));
		const auto position = QPoint(left, top);
		p.translate(position);
		media->draw(p, context.translated(-position).withSelection({}));
		p.translate(-position);
	}
}

PointState Service::pointState(QPoint point) const {
	const auto media = this->media();
	const auto mediaDisplayed = media && media->isDisplayed();

	auto g = countGeometry();
	if (g.width() < 1 || isHidden()) {
		return PointState::Outside;
	}

	if (mediaDisplayed) {
		const auto centerPadding = (g.width() - media->width()) / 2;
		const auto r = g - QMargins(centerPadding, 0, centerPadding, 0);
		if (!r.contains(point)) {
			g.setHeight(g.height()
				- (st::msgServiceMargin.top() + media->height()));
		}
	}
	return g.contains(point) ? PointState::Inside : PointState::Outside;
}

TextState Service::textState(QPoint point, StateRequest request) const {
	const auto item = data();
	const auto media = this->media();
	const auto mediaDisplayed = media && media->isDisplayed();
	const auto onlyMedia = (mediaDisplayed && media->hideServiceText());

	auto result = TextState(item);

	auto g = countGeometry();
	if (g.width() < 1 || isHidden()) {
		return result;
	}

	if (_reactions) {
		const auto reactionsHeight = st::mediaInBubbleSkip + _reactions->height();
		const auto reactionsLeft = 0;
		g.setHeight(g.height() - reactionsHeight);
		const auto reactionsPosition = QPoint(reactionsLeft + g.left(), g.top() + g.height() + st::mediaInBubbleSkip);
		if (_reactions->getState(point - reactionsPosition, &result)) {
			//result.symbol += visibleMediaTextLen + visibleTextLen;
			return result;
		}
	}


	if (onlyMedia) {
		return media->textState(point - QPoint(st::msgServiceMargin.left() + (g.width() - media->width()) / 2, g.top()), request);
	} else if (mediaDisplayed) {
		g.setHeight(g.height() - (st::msgServiceMargin.top() + media->height()));
	}
	const auto mediaLeft = st::msgServiceMargin.left()
		+ (media ? ((g.width() - media->width()) / 2) : 0);
	const auto mediaTop = g.top()
		+ g.height()
		+ st::msgServiceMargin.top();
	const auto mediaPoint = point - QPoint(mediaLeft, mediaTop);
	auto trect = g.marginsAdded(-st::msgServicePadding);
	if (trect.contains(point)) {
		auto textRequest = request.forText();
		textRequest.align = style::al_center;
		result = TextState(item, text().getState(
			point - trect.topLeft(),
			trect.width(),
			textRequest));
		if (!result.link
			&& result.cursor == CursorState::Text
			&& g.contains(point)) {
			if (const auto gamescore = item->Get<HistoryServiceGameScore>()) {
				result.link = gamescore->lnk;
			} else if (const auto payment = item->Get<HistoryServicePayment>()) {
				result.link = payment->invoiceLink;
			} else if (const auto call = item->Get<HistoryServiceOngoingCall>()) {
				const auto peer = history()->peer;
				if (PeerHasThisCall(peer, call->id).value_or(false)) {
					result.link = call->link;
				}
			} else if (const auto theme = item->Get<HistoryServiceChatThemeChange>()) {
				result.link = theme->link;
			} else if (const auto ttl = item->Get<HistoryServiceTTLChange>()) {
				if (TTLMenu::TTLValidator(nullptr, history()->peer).can()) {
					result.link = ttl->link;
				}
			} else if (const auto same = item->Get<HistoryServiceSameBackground>()) {
				result.link = same->lnk;
			} else if (const auto results = item->Get<HistoryServiceGiveawayResults>()) {
				result.link = results->lnk;
			} else if (const auto custom = item->Get<HistoryServiceCustomLink>()) {
				result.link = custom->link;
			} else if (const auto payment = item->Get<HistoryServicePaymentRefund>()) {
				result.link = payment->link;
			} else if (media && data()->showSimilarChannels()) {
				result = media->textState(mediaPoint, request);
			}
		}
	} else if (mediaDisplayed && point.y() >= mediaTop) {
		result = media->textState(mediaPoint, request);
	}
	return result;
}

void Service::updatePressed(QPoint point) {
}

TextForMimeData Service::selectedText(TextSelection selection) const {
	return text().toTextForMimeData(selection);
}

SelectedQuote Service::selectedQuote(TextSelection selection) const {
	return {};
}

TextSelection Service::selectionFromQuote(
		const SelectedQuote &quote) const {
	return {};
}

TextSelection Service::adjustSelection(
		TextSelection selection,
		TextSelectType type) const {
	return text().adjustSelection(selection, type);
}

EmptyPainter::EmptyPainter(not_null<History*> history)
: _history(history)
, _header(st::msgMinWidth)
, _text(st::msgMinWidth) {
	if (NeedAboutGroup(_history)) {
		fillAboutGroup();
	}
}

EmptyPainter::EmptyPainter(
	not_null<Data::ForumTopic*> topic,
	Fn<bool()> paused,
	Fn<void()> update)
: _history(topic->history())
, _topic(topic)
, _icon(
	std::make_unique<Info::Profile::TopicIconView>(
		topic,
		paused,
		update,
		st::msgServiceFg))
, _header(st::msgMinWidth)
, _text(st::msgMinWidth) {
	fillAboutTopic();
}

EmptyPainter::~EmptyPainter() = default;

void EmptyPainter::fillAboutGroup() {
	const auto phrases = {
		tr::lng_group_about1(tr::now),
		tr::lng_group_about2(tr::now),
		tr::lng_group_about3(tr::now),
		tr::lng_group_about4(tr::now),
	};
	SetText(_header, tr::lng_group_about_header(tr::now));
	SetText(_text, tr::lng_group_about_text(tr::now));
	for (const auto &text : phrases) {
		_phrases.emplace_back(st::msgMinWidth);
		SetText(_phrases.back(), text);
	}
}

void EmptyPainter::fillAboutTopic() {
	SetText(_header, _topic->my()
		? tr::lng_forum_topic_created_title_my(tr::now)
		: tr::lng_forum_topic_created_title(tr::now));
	SetText(_text, _topic->my()
		? tr::lng_forum_topic_created_body_my(tr::now)
		: tr::lng_forum_topic_created_body(tr::now));
}

void EmptyPainter::paint(
		Painter &p,
		not_null<const Ui::ChatStyle*> st,
		int width,
		int height) {
	if (_phrases.empty() && _text.isEmpty()) {
		return;
	}
	constexpr auto kMaxTextLines = 3;
	const auto maxPhraseWidth = _phrases.empty()
		? 0
		: ranges::max_element(
			_phrases,
			ranges::less(),
			&Ui::Text::String::maxWidth)->maxWidth();

	const auto &font = st::serviceTextStyle.font;
	const auto maxBubbleWidth = width - 2 * st::historyGroupAboutMargin;
	const auto padding = st::historyGroupAboutPadding;
	const auto bubbleWidth = std::min(
		maxBubbleWidth,
		std::max({
			maxPhraseWidth + st::historyGroupAboutBulletSkip,
			_header.maxWidth(),
			_text.maxWidth() }) + padding.left() + padding.right());
	const auto innerWidth = bubbleWidth - padding.left() - padding.right();
	const auto textHeight = [&](const Ui::Text::String &text) {
		return std::min(
			text.countHeight(innerWidth),
			kMaxTextLines * font->height);
	};
	const auto iconHeight = _icon
		? st::infoTopicCover.photo.size.height()
		: 0;
	const auto bubbleHeight = padding.top()
		+ (_icon ? (iconHeight + st::historyGroupAboutHeaderSkip) : 0)
		+ textHeight(_header)
		+ st::historyGroupAboutHeaderSkip
		+ textHeight(_text)
		+ st::historyGroupAboutTextSkip
		+ ranges::accumulate(_phrases, 0, ranges::plus(), textHeight)
		+ st::historyGroupAboutSkip * std::max(int(_phrases.size()) - 1, 0)
		+ padding.bottom();
	const auto bubbleLeft = (width - bubbleWidth) / 2;
	const auto bubbleTop = (height - bubbleHeight) / 2;

	ServiceMessagePainter::PaintBubble(
		p,
		st->msgServiceBg(),
		st->serviceBgCornersNormal(),
		QRect(bubbleLeft, bubbleTop, bubbleWidth, bubbleHeight));

	p.setPen(st->msgServiceFg());
	p.setBrush(st->msgServiceFg());

	const auto left = bubbleLeft + padding.left();
	auto top = bubbleTop + padding.top();

	if (_icon) {
		_icon->paintInRect(
			p,
			QRect(bubbleLeft, top, bubbleWidth, iconHeight));
		top += iconHeight + st::historyGroupAboutHeaderSkip;
	}

	_header.drawElided(
		p,
		left,
		top,
		innerWidth,
		kMaxTextLines,
		style::al_top);
	top += textHeight(_header) + st::historyGroupAboutHeaderSkip;

	_text.drawElided(
		p,
		left,
		top,
		innerWidth,
		kMaxTextLines,
		_topic ? style::al_top : style::al_topleft);
	top += textHeight(_text) + st::historyGroupAboutTextSkip;

	for (const auto &text : _phrases) {
		p.setPen(st->msgServiceFg());
		text.drawElided(
			p,
			left + st::historyGroupAboutBulletSkip,
			top,
			innerWidth,
			kMaxTextLines);

		PainterHighQualityEnabler hq(p);
		p.setPen(Qt::NoPen);
		p.drawEllipse(
			left,
			top + (font->height - st::mediaUnreadSize) / 2,
			st::mediaUnreadSize,
			st::mediaUnreadSize);
		top += textHeight(text) + st::historyGroupAboutSkip;
	}
}

} // namespace HistoryView
