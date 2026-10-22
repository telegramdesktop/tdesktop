/*
This file is part of Telegram Desktop,
the official desktop application for the Telegram messaging service.

For license and copyright information please follow this link:
https://github.com/telegramdesktop/tdesktop/blob/master/LEGAL
*/
#include "tgw/tgw_media.h"

#include "dialogs/ui/dialogs_message_view.h"
#include "history/view/media/history_view_unsupported_widget.h"
#include "history/view/history_view_item_preview.h"
#include "history/history_item.h"
#include "lang/lang_keys.h"
#include "ui/text/text_utilities.h"

namespace Tgw {
namespace {

[[nodiscard]] TextWithEntities Stub() {
	return Ui::Text::Colorized(tr::lng_in_dlg_widget(tr::now));
}

} // namespace

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
	return std::make_unique<WidgetMedia>(parent, _anchors, _payload);
}

Anchors WidgetMedia::anchors() const {
	return _anchors;
}

const QByteArray &WidgetMedia::payload() const {
	return _payload;
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
	return std::make_unique<HistoryView::UnsupportedWidget>(message);
}

} // namespace Tgw
