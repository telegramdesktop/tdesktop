/*
This file is part of Telegram Desktop,
the official desktop application for the Telegram messaging service.

For license and copyright information please follow this link:
https://github.com/telegramdesktop/tdesktop/blob/master/LEGAL
*/
#include "payments/ui/payments_webview.h"

#include "payments/ui/payments_panel_delegate.h"
#include "webview/webview_embed.h"
#include "webview/webview_interface.h"
#include "ui/widgets/window.h"
#include "ui/toast/toast.h"
#include "lang/lang_keys.h"

namespace Payments::Ui {

using namespace ::Ui;

class PanelDelegate;

WebviewWindow::WebviewWindow(
		const QString &userDataPath,
		const QString &url,
		not_null<PanelDelegate*> delegate) {
	if (!url.startsWith("https://", Qt::CaseInsensitive)) {
		return;
	}

	const auto window = &_window;

	window->setGeometry({
		style::ConvertScale(100),
		style::ConvertScale(100),
		style::ConvertScale(640),
		style::ConvertScale(480)
	});
	window->show();

	window->events() | rpl::start_with_next([=](not_null<QEvent*> e) {
		if (e->type() == QEvent::Close) {
			delegate->panelCloseSure();
		}
	}, window->lifetime());

	const auto body = window->body();
	body->paintRequest(
	) | rpl::start_with_next([=](QRect clip) {
		QPainter(body).fillRect(clip, st::windowBg);
	}, body->lifetime());

	const auto path =
	_webview = Ui::CreateChild<Webview::Window>(
		window,
		window,
		Webview::WindowConfig{ .userDataPath = userDataPath });
	if (!_webview->widget()) {
		return;
	}

	body->geometryValue(
	) | rpl::start_with_next([=](QRect geometry) {
		_webview->widget()->setGeometry(geometry);
	}, body->lifetime());

	_webview->setMessageHandler([=](const QJsonDocument &message) {
		delegate->panelWebviewMessage(message);
	});

	_webview->setNavigationHandler([=](const QString &uri) {
		return delegate->panelWebviewNavigationAttempt(uri);
	});

	_webview->init(R"(
window.TelegramWebviewProxy = {
postEvent: function(eventType, eventData) {
	if (window.external && window.external.invoke) {
		window.external.invoke(JSON.stringify([eventType, eventData]));
	}
}
};)");

	navigate(url);
}

[[nodiscard]] bool WebviewWindow::shown() const {
	return _webview && _webview->widget();
}

void WebviewWindow::navigate(const QString &url) {
	if (shown()) {
		_webview->navigate(url);
	}
}

} // namespace Payments::Ui
