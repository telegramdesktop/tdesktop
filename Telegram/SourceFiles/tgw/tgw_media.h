/*
This file is part of Telegram Desktop,
the official desktop application for the Telegram messaging service.

For license and copyright information please follow this link:
https://github.com/telegramdesktop/tdesktop/blob/master/LEGAL
*/
#pragma once

#include "data/data_media_types.h"

namespace Tgw {

struct Anchors {
	int width = 0;
	int height = 0;
};

[[nodiscard]] QByteArray UnpackPayload(const QByteArray &payload);

class WidgetMedia final : public Data::Media {
public:
	WidgetMedia(
		not_null<HistoryItem*> parent,
		Anchors anchors,
		QByteArray payload);

	std::unique_ptr<Data::Media> clone(
		not_null<HistoryItem*> parent) override;

	[[nodiscard]] Anchors anchors() const;
	[[nodiscard]] const QByteArray &payload() const;
	[[nodiscard]] const QByteArray &html() const;

	ItemPreview toPreview(ToPreviewOptions options) const override;
	TextWithEntities notificationText() const override;
	QString pinnedTextSubstring() const override;
	TextForMimeData clipboardText() const override;

	bool updateInlineResultMedia(const MTPMessageMedia &media) override;
	bool updateSentMedia(const MTPMessageMedia &media) override;
	std::unique_ptr<HistoryView::Media> createView(
		not_null<HistoryView::Element*> message,
		not_null<HistoryItem*> realParent,
		HistoryView::Element *replacing = nullptr) override;

private:
	Anchors _anchors;
	QByteArray _payload;
	mutable std::optional<QByteArray> _html;

};

} // namespace Tgw
