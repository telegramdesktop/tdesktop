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

	friend inline bool operator==(Anchors, Anchors) = default;
};

struct WidgetFlags {
	bool isCopy = false;
	bool canQuery = false;
	bool backgroundQuery = false;

	friend inline bool operator==(WidgetFlags, WidgetFlags) = default;
};

struct WidgetState {
	std::optional<QByteArray> chatData;
	std::optional<QByteArray> publicData;

	friend inline bool operator==(
		const WidgetState &,
		const WidgetState &) = default;
};

struct WidgetFields {
	Anchors anchors;
	QByteArray payload;
	WidgetState state;
	std::optional<uint64> publicDataHash;
	std::optional<uint64> widgetId;
	WidgetFlags flags;

	friend inline bool operator==(
		const WidgetFields &,
		const WidgetFields &) = default;
};

struct WidgetStateEdit {
	std::optional<QByteArray> chatData;
	std::optional<QByteArray> publicData;
	std::optional<uint64> publicDataHash;
};

enum class ChatType : uchar {
	None,
	Private,
	Group,
	Supergroup,
	Channel,
};

struct MessageContext {
	WidgetFlags flags;
	ChatType chatType = ChatType::None;
	bool wallet = false;

	friend inline bool operator==(MessageContext, MessageContext) = default;
};

[[nodiscard]] QByteArray UnpackPayload(const QByteArray &payload);
[[nodiscard]] WidgetFields ParseWidgetFields(
	const MTPDmessageMediaWidget &data);

class WidgetMedia final : public Data::Media {
public:
	WidgetMedia(not_null<HistoryItem*> parent, WidgetFields fields);
	~WidgetMedia();

	std::unique_ptr<Data::Media> clone(
		not_null<HistoryItem*> parent) override;

	[[nodiscard]] const WidgetFields &fields() const;
	[[nodiscard]] const QByteArray &html() const;
	[[nodiscard]] MessageContext context() const;
	bool applyState(const WidgetStateEdit &edit);

	ItemPreview toPreview(ToPreviewOptions options) const override;
	TextWithEntities notificationText() const override;
	QString pinnedTextSubstring() const override;
	TextForMimeData clipboardText() const override;
	bool forceForwardedInfo() const override;

	bool updateInlineResultMedia(const MTPMessageMedia &media) override;
	bool updateSentMedia(const MTPMessageMedia &media) override;
	std::unique_ptr<HistoryView::Media> createView(
		not_null<HistoryView::Element*> message,
		not_null<HistoryItem*> realParent,
		HistoryView::Element *replacing = nullptr) override;

private:
	WidgetFields _fields;
	mutable std::optional<QByteArray> _html;

};

} // namespace Tgw
