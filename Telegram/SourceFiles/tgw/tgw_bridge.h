/*
This file is part of Telegram Desktop,
the official desktop application for the Telegram messaging service.

For license and copyright information please follow this link:
https://github.com/telegramdesktop/tdesktop/blob/master/LEGAL
*/
#pragma once

class HistoryItem;

namespace Ui {
class BoxContent;
} // namespace Ui

namespace Window {
class SessionController;
} // namespace Window

namespace Tgw {

class Bridge final {
public:
	explicit Bridge(not_null<HistoryItem*> item);

	[[nodiscard]] QString call(
		const std::string &method,
		const std::string &argsJson);

private:
	enum class Method : uchar {
		Ready,
		OpenLink,
		SendMessage,
		Clipboard,
		Alert,
	};
	struct Call {
		Method method = Method::Ready;
		QString argument;
	};

	[[nodiscard]] static std::optional<Call> Parse(
		const std::string &method,
		const std::string &argsJson);

	[[nodiscard]] bool passCooldown(Method method);
	[[nodiscard]] Window::SessionController *findController() const;

	[[nodiscard]] QString openLink(const QString &url);
	[[nodiscard]] QString sendMessage(const QString &text);
	[[nodiscard]] QString copyToClipboard(const QString &text);
	[[nodiscard]] QString showAlert(const QString &text);

	const not_null<HistoryItem*> _item;
	base::flat_map<Method, crl::time> _lastCalls;
	base::weak_qptr<Ui::BoxContent> _alert;

};

} // namespace Tgw
