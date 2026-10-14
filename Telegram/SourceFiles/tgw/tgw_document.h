/*
This file is part of Telegram Desktop,
the official desktop application for the Telegram messaging service.

For license and copyright information please follow this link:
https://github.com/telegramdesktop/tdesktop/blob/master/LEGAL
*/
#pragma once

class DocumentData;
class HistoryItem;

namespace Main {
class Session;
} // namespace Main

namespace Tgw {

extern const char kOptionWidgetMessages[];

struct ContentReader {
	Fn<QByteArray()> read;
	int generation = 0;
};

struct WidgetItemId {
	not_null<Main::Session*> session;
	FullMsgId id;
};

void InitEnabled();
[[nodiscard]] bool Enabled();

[[nodiscard]] bool IsWidgetFileName(const QString &name);
[[nodiscard]] bool IsWidgetDocument(not_null<const DocumentData*> document);

[[nodiscard]] bool ShouldAutoDownload(
	not_null<const DocumentData*> document);
[[nodiscard]] QString UploadMimeType(
	const QString &fileName,
	const QString &mimeType);

[[nodiscard]] bool IsWidgetViewCandidate(
	not_null<const DocumentData*> document,
	not_null<const HistoryItem*> item);

// Never refreshes synchronously, so it is safe while building a view.
[[nodiscard]] bool ShowWidgetView(
	not_null<DocumentData*> document,
	not_null<const HistoryItem*> item);
[[nodiscard]] bool HasValidatedContent(not_null<DocumentData*> document);
[[nodiscard]] bool IsBareWidget(not_null<DocumentData*> document);

[[nodiscard]] std::optional<ContentReader> PrepareContentReader(
	not_null<DocumentData*> document);
void MarkContentUnavailable(
	not_null<DocumentData*> document,
	int generation,
	FullMsgId itemId);
void DisableWidgetViews(const std::vector<WidgetItemId> &items);

} // namespace Tgw
