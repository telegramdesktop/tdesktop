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
#include "ui/item_text_options.h"
#include "core/ui_integration.h"
#include "styles/style_chat.h"

namespace HistoryView {
namespace {

[[nodiscard]] TimeId TimeFromMatch(
		const QStringRef &hours,
		const QStringRef &minutes1,
		const QStringRef &minutes2,
		const QStringRef &seconds) {
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

QString DocumentTimestampLinkBase(
		not_null<DocumentData*> document,
		FullMsgId context) {
	return QString(
		"doc%1_%2_%3"
	).arg(document->id).arg(context.channel).arg(context.msg);
}

TextWithEntities AddTimestampLinks(
		TextWithEntities text,
		TimeId duration,
		const QString &base) {
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
			m.capturedRef(1),
			m.capturedRef(2),
			m.capturedRef(3),
			m.capturedRef(4));
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
				("internal:media_timestamp?base="
					+ base
					+ "&t="
					+ QString::number(time))));
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

Ui::Text::String Media::createCaption(
		not_null<HistoryItem*> item,
		TimeId timestampLinksDuration,
		const QString &timestampLinkBase) const {
	Expects(timestampLinksDuration >= 0);

	if (item->emptyText()) {
		return {};
	}
	const auto minResizeWidth = st::minPhotoSize
		- st::msgPadding.left()
		- st::msgPadding.right();
	auto result = Ui::Text::String(minResizeWidth);
	const auto context = Core::UiIntegration::Context{
		.session = &history()->session()
	};
	result.setMarkedText(
		st::messageTextStyle,
		(timestampLinksDuration
			? AddTimestampLinks(
				item->originalText(),
				timestampLinksDuration,
				timestampLinkBase)
			: item->originalText()),
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
