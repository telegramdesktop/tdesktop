/*
This file is part of Telegram Desktop,
the official desktop application for the Telegram messaging service.

For license and copyright information please follow this link:
https://github.com/telegramdesktop/tdesktop/blob/master/LEGAL
*/
#pragma once

#include "base/timer.h"
#include "mtproto/sender.h"

class HistoryItem;

namespace twidget {
struct WidgetRequestOutcome;
} // namespace twidget

namespace Window {
class SessionController;
} // namespace Window

namespace Tgw {

class Bridge final {
public:
	using Respond = Fn<void(const twidget::WidgetRequestOutcome&)>;

	explicit Bridge(not_null<HistoryItem*> item);
	~Bridge();

	[[nodiscard]] QString openLink(const QString &url, bool telegram);
	[[nodiscard]] QString copyText(
		const QString &text,
		not_null<QWidget*> window);
	void request(const std::string &data, Respond respond);

private:
	struct Pending {
		Respond respond;
		crl::time deadline = 0;
	};

	[[nodiscard]] Window::SessionController *findController() const;
	void finish(
		mtpRequestId id,
		const twidget::WidgetRequestOutcome &outcome);
	void checkDeadlines();

	const not_null<HistoryItem*> _item;
	MTP::Sender _api;
	base::flat_map<mtpRequestId, Pending> _pending;
	base::Timer _deadlines;

};

} // namespace Tgw
