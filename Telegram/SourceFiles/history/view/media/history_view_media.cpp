/*
This file is part of Telegram Desktop,
the official desktop application for the Telegram messaging service.

For license and copyright information please follow this link:
https://github.com/telegramdesktop/tdesktop/blob/master/LEGAL
*/
#include "history/view/media/history_view_media.h"

#include "history/history.h"
#include "history/history_item.h"
#include "history/view/history_view_element.h"
#include "history/view/history_view_cursor_state.h"
#include "lottie/lottie_single_player.h"
#include "storage/storage_shared_media.h"
#include "data/data_document.h"
#include "data/data_web_page.h"
#include "ui/item_text_options.h"
#include "ui/chat/chat_style.h"
#include "ui/chat/message_bubble.h"
#include "core/ui_integration.h"
#include "styles/style_chat.h"

namespace HistoryView {
namespace {

[[nodiscard]] TimeId TimeFromMatch(
		QStringView hours,
		QStringView minutes1,
		QStringView minutes2,
		QStringView seconds) {
	auto ok1 = true;
	auto ok2 = true;
	auto ok3 = true;
	auto minutes = minutes1.toString();
	minutes += minutes2;
	const auto value1 = (hours.isEmpty() ? 0 : hours.toInt(&ok1));
	const auto value2 = minutes.toInt(&ok2);
	const auto value3 = seconds.toInt(&ok3);
	const auto ok = ok1 && ok2 && ok3;
	return (ok && value3 < 60 && (hours.isEmpty() || value2 < 60))
		? (value1 * 3600 + value2 * 60 + value3)
		: -1;
}

} // namespace

TimeId DurationForTimestampLinks(not_null<DocumentData*> document) {
	if (!document->isVideoFile()
		&& !document->isSong()
		&& !document->isVoiceMessage()) {
		return TimeId(0);
	}
	return std::max(document->getDuration(), TimeId(0));
}

QString TimestampLinkBase(
		not_null<DocumentData*> document,
		FullMsgId context) {
	return QString(
		"media_timestamp?base=doc%1_%2_%3&t="
	).arg(document->id).arg(context.channel.bare).arg(context.msg.bare);
}

TimeId DurationForTimestampLinks(not_null<WebPageData*> webpage) {
	if (!webpage->collage.items.empty()) {
		return 0;
	} else if (const auto document = webpage->document) {
		return DurationForTimestampLinks(document);
	} else if (webpage->type != WebPageType::Video
		|| webpage->siteName != qstr("YouTube")) {
		return TimeId(0);
	} else if (webpage->duration > 0) {
		return webpage->duration;
	}
	constexpr auto kMaxYouTubeTimestampDuration = 10 * 60 * TimeId(60);
	return kMaxYouTubeTimestampDuration;
}

QString TimestampLinkBase(
		not_null<WebPageData*> webpage,
		FullMsgId context) {
	const auto url = webpage->url;
	if (url.isEmpty()) {
		return QString();
	}
	auto parts = url.split(QChar('#'));
	const auto base = parts[0];
	parts.pop_front();
	const auto use = [&] {
		const auto query = base.indexOf(QChar('?'));
		if (query < 0) {
			return base + QChar('?');
		}
		auto params = base.mid(query + 1).split(QChar('&'));
		for (auto i = params.begin(); i != params.end();) {
			if (i->startsWith("t=")) {
				i = params.erase(i);
			} else {
				++i;
			}
		}
		return base.mid(0, query)
			+ (params.empty() ? "?" : ("?" + params.join(QChar('&')) + "&"));
	}();
	return "url:"
		+ use
		+ "t="
		+ (parts.empty() ? QString() : ("#" + parts.join(QChar('#'))));
}

TextWithEntities AddTimestampLinks(
		TextWithEntities text,
		TimeId duration,
		const QString &base) {
	if (base.isEmpty()) {
		return text;
	}
	static const auto expression = QRegularExpression(
		"(?<![^\\s\\(\\)\"\\,\\.\\-])(?:(?:(\\d{1,2}):)?(\\d))?(\\d):(\\d\\d)(?![^\\s\\(\\)\",\\.\\-])");
	const auto &string = text.text;
	auto offset = 0;
	while (true) {
		const auto m = expression.match(string, offset);
		if (!m.hasMatch()) {
			break;
		}

		const auto from = m.capturedStart();
		const auto till = from + m.capturedLength();
		offset = till;

		const auto time = TimeFromMatch(
			m.capturedView(1),
			m.capturedView(2),
			m.capturedView(3),
			m.capturedView(4));
		if (time < 0 || time > duration) {
			continue;
		}

		auto &entities = text.entities;
		const auto i = ranges::lower_bound(
			entities,
			from,
			std::less<>(),
			&EntityInText::offset);
		if (i != entities.end() && i->offset() < till) {
			continue;
		}

		const auto intersects = [&](const EntityInText &entity) {
			return entity.offset() + entity.length() > from;
		};
		auto j = std::make_reverse_iterator(i);
		const auto e = std::make_reverse_iterator(entities.begin());
		if (std::find_if(j, e, intersects) != e) {
			continue;
		}

		entities.insert(
			i,
			EntityInText(
				EntityType::CustomUrl,
				from,
				till - from,
				("internal:" + base + QString::number(time))));
	}
	return text;
}

Storage::SharedMediaTypesMask Media::sharedMediaTypes() const {
	return {};
}

not_null<History*> Media::history() const {
	return _parent->history();
}

bool Media::isDisplayed() const {
	return true;
}

QSize Media::countCurrentSize(int newWidth) {
	return QSize(qMin(newWidth, maxWidth()), minHeight());
}

Ui::Text::String Media::createCaption(not_null<HistoryItem*> item) const {
	if (item->emptyText()) {
		return {};
	}
	const auto minResizeWidth = st::minPhotoSize
		- st::msgPadding.left()
		- st::msgPadding.right();
	auto result = Ui::Text::String(minResizeWidth);
	const auto context = Core::MarkedTextContext{
		.session = &history()->session()
	};
	result.setMarkedText(
		st::messageTextStyle,
		item->originalTextWithLocalEntities(),
		Ui::ItemTextOptions(item),
		context);
	if (const auto width = _parent->skipBlockWidth()) {
		result.updateSkipBlock(width, _parent->skipBlockHeight());
	}
	return result;
}

TextSelection Media::skipSelection(TextSelection selection) const {
	return UnshiftItemSelection(selection, fullSelectionLength());
}

TextSelection Media::unskipSelection(TextSelection selection) const {
	return ShiftItemSelection(selection, fullSelectionLength());
}

auto Media::getBubbleSelectionIntervals(
	TextSelection selection) const
-> std::vector<Ui::BubbleSelectionInterval> {
	return {};
}

bool Media::usesBubblePattern(const PaintContext &context) const {
	return (context.selection != FullSelection)
		&& _parent->hasOutLayout()
		&& context.bubblesPattern
		&& !context.viewport.isEmpty()
		&& !context.bubblesPattern->pixmap.size().isEmpty();
}

PointState Media::pointState(QPoint point) const {
	return QRect(0, 0, width(), height()).contains(point)
		? PointState::Inside
		: PointState::Outside;
}

std::unique_ptr<Lottie::SinglePlayer> Media::stickerTakeLottie(
		not_null<DocumentData*> data,
		const Lottie::ColorReplacements *replacements) {
	return nullptr;
}

TextState Media::getStateGrouped(
		const QRect &geometry,
		RectParts sides,
		QPoint point,
		StateRequest request) const {
	Unexpected("Grouping method call.");
}

bool Media::isRoundedInBubbleBottom() const {
	return isBubbleBottom()
		&& !_parent->data()->repliesAreComments()
		&& !_parent->data()->externalReply();
}

} // namespace HistoryView
