/*
This file is part of Telegram Desktop,
the official desktop application for the Telegram messaging service.

For license and copyright information please follow this link:
https://github.com/telegramdesktop/tdesktop/blob/master/LEGAL
*/
#pragma once

class DocumentData;
class HistoryItem;

namespace Tgw {

extern const char kOptionWidgetMessages[];

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

} // namespace Tgw
