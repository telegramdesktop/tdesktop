/*
This file is part of Telegram Desktop,
the official desktop application for the Telegram messaging service.

For license and copyright information please follow this link:
https://github.com/telegramdesktop/tdesktop/blob/master/LEGAL
*/
#pragma once

class QMimeData;

namespace Main {
class Session;
} // namespace Main

namespace ChatHelpers {

enum class RichPasteOffer : uchar {
	Editor,
	Plain,
};

struct RichPasteToastArgs {
	not_null<Main::Session*> session;
	not_null<QWidget*> parent;
	rpl::producer<int> bottomOffset;
	rpl::producer<> cancel;
	RichPasteOffer offer = RichPasteOffer::Editor;
	Fn<void()> action;
};

[[nodiscard]] bool MimeDataLosesRichFormatting(
	not_null<Main::Session*> session,
	not_null<const QMimeData*> data);

[[nodiscard]] std::shared_ptr<QMimeData> CloneMimeData(
	not_null<const QMimeData*> data);

void ShowRichPasteToast(RichPasteToastArgs &&args);

} // namespace ChatHelpers
