/*
This file is part of Telegram Desktop,
the official desktop application for the Telegram messaging service.

For license and copyright information please follow this link:
https://github.com/telegramdesktop/tdesktop/blob/master/LEGAL
*/
#include "tgw/tgw_media.h"

#include "dialogs/ui/dialogs_message_view.h"
#include "history/view/media/history_view_html_widget.h"
#include "history/view/media/history_view_unsupported_widget.h"
#include "history/view/history_view_item_preview.h"
#include "history/history_item.h"
#include "lang/lang_keys.h"
#include "tgw/tgw_option.h"
#include "ui/text/text_utilities.h"

#include <zlib.h>

namespace Tgw {
namespace {

constexpr auto kInitialUnpackedSize = qsizetype(16 * 1024);

[[nodiscard]] TextWithEntities Stub() {
	return Ui::Text::Colorized(tr::lng_in_dlg_widget(tr::now));
}

[[nodiscard]] uInt ZlibSize(qsizetype size) {
	return uInt(std::min(
		uint64(size),
		uint64(std::numeric_limits<uInt>::max())));
}

[[nodiscard]] qsizetype GrownSize(qsizetype size) {
	const auto limit = QByteArray::maxSize();
	return (size > limit / 2) ? limit : (size * 2);
}

} // namespace

QByteArray UnpackPayload(const QByteArray &payload) {
	if (payload.isEmpty()) {
		return QByteArray();
	}
	auto stream = z_stream();
	if (inflateInit2(&stream, 16 + MAX_WBITS) != Z_OK) {
		return QByteArray();
	}
	const auto guard = gsl::finally([&] { inflateEnd(&stream); });

	auto result = QByteArray(kInitialUnpackedSize, Qt::Uninitialized);
	auto produced = qsizetype(0);
	auto fed = qsizetype(0);
	while (true) {
		if (!stream.avail_in && fed < payload.size()) {
			stream.avail_in = ZlibSize(payload.size() - fed);
			stream.next_in = reinterpret_cast<Bytef*>(
				const_cast<char*>(payload.constData() + fed));
			fed += qsizetype(stream.avail_in);
		}
		// No client cap: the server enforces payload limits and may move them.
		if (produced == result.size()) {
			if (result.size() >= QByteArray::maxSize()) {
				return QByteArray();
			}
			result.resize(GrownSize(result.size()));
		}
		const auto room = ZlibSize(result.size() - produced);
		stream.avail_out = room;
		stream.next_out = reinterpret_cast<Bytef*>(result.data() + produced);
		const auto code = inflate(&stream, Z_NO_FLUSH);
		produced += qsizetype(room - stream.avail_out);
		const auto consumed = !stream.avail_in && (fed == payload.size());
		if (code == Z_STREAM_END && consumed && produced > 0) {
			break;
		} else if (code != Z_OK || (consumed && stream.avail_out)) {
			return QByteArray();
		}
	}
	result.resize(produced);
	result.squeeze();
	return result;
}

WidgetMedia::WidgetMedia(
	not_null<HistoryItem*> parent,
	Anchors anchors,
	QByteArray payload)
: Media(parent)
, _anchors(anchors)
, _payload(std::move(payload)) {
}

std::unique_ptr<Data::Media> WidgetMedia::clone(
		not_null<HistoryItem*> parent) {
	auto result = std::make_unique<WidgetMedia>(parent, _anchors, _payload);
	result->_html = _html;
	return result;
}

Anchors WidgetMedia::anchors() const {
	return _anchors;
}

const QByteArray &WidgetMedia::payload() const {
	return _payload;
}

const QByteArray &WidgetMedia::html() const {
	if (!_html) {
		_html = UnpackPayload(_payload);
		if (_html->isEmpty()) {
			LOG(("Tgw Error: Could not unpack a widget payload of %1 bytes."
				).arg(_payload.size()));
		}
	}
	return *_html;
}

HistoryView::ItemPreview WidgetMedia::toPreview(
		ToPreviewOptions options) const {
	const auto caption = (options.hideCaption || options.ignoreMessageText)
		? tr::marked()
		: Dialogs::Ui::DialogsPreviewText(options.translated
			? parent()->translatedText()
			: parent()->originalText());
	return {
		.text = caption.empty() ? Stub() : st::wrap_rtl(caption),
	};
}

TextWithEntities WidgetMedia::notificationText() const {
	const auto &caption = parent()->originalText();
	return caption.empty() ? Stub() : caption;
}

QString WidgetMedia::pinnedTextSubstring() const {
	return parent()->originalText().empty()
		? (QChar(171) + tr::lng_in_dlg_widget(tr::now) + QChar(187))
		: QString();
}

TextForMimeData WidgetMedia::clipboardText() const {
	return TextForMimeData();
}

bool WidgetMedia::updateInlineResultMedia(const MTPMessageMedia &media) {
	return false;
}

bool WidgetMedia::updateSentMedia(const MTPMessageMedia &media) {
	return media.match([&](const MTPDmessageMediaWidget &data) {
		return (data.vwidth().v == _anchors.width)
			&& (data.vheight().v == _anchors.height)
			&& (data.vpayload().v == _payload);
	}, [](const auto &) {
		return false;
	});
}

std::unique_ptr<HistoryView::Media> WidgetMedia::createView(
		not_null<HistoryView::Element*> message,
		not_null<HistoryItem*> realParent,
		HistoryView::Element *replacing) {
	if (Enabled()) {
		const auto &html = this->html();
		if (!html.isEmpty()) {
			return std::make_unique<HistoryView::HtmlWidget>(
				message,
				realParent,
				_anchors,
				html,
				replacing);
		}
	}
	return std::make_unique<HistoryView::UnsupportedWidget>(message);
}

} // namespace Tgw
