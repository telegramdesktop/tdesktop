/*
This file is part of Telegram Desktop,
the official desktop application for the Telegram messaging service.

For license and copyright information please follow this link:
https://github.com/telegramdesktop/tdesktop/blob/master/LEGAL
*/
#include "ui/chat/attach/attach_bot_webview_linux_shell.h"

#include "ui/style/style_core_palette.h"
#include "styles/style_layers.h"
#include "styles/style_menu_icons.h"
#include "styles/style_payments.h"

#include <QtCore/QFile>
#include <QtCore/QJsonArray>
#include <QtCore/QJsonDocument>

#if !defined Q_OS_WIN && !defined Q_OS_MAC

namespace Ui::BotWebView::LinuxShell {
namespace {

[[nodiscard]] QByteArray JsonValue(QJsonValue value) {
	auto array = QJsonArray();
	array.push_back(std::move(value));
	auto result = QJsonDocument(array).toJson(QJsonDocument::Compact);
	return result.mid(1, result.size() - 2);
}

[[nodiscard]] QByteArray JsonObject(QJsonObject object) {
	return QJsonDocument(std::move(object)).toJson(QJsonDocument::Compact);
}

[[nodiscard]] QJsonValue ColorValue(QColor color) {
	return color.isValid()
		? QJsonValue(color.name(QColor::HexRgb))
		: QJsonValue();
}

[[nodiscard]] QByteArray ReadResource(const QString &name) {
	auto file = QFile(name);
	return file.open(QIODevice::ReadOnly) ? file.readAll() : QByteArray();
}

[[nodiscard]] const QByteArray &PageCss() {
	static const auto result = ReadResource(u":/bot_webview_shell/page.css"_q);
	return result;
}

[[nodiscard]] const QByteArray &PageJs() {
	static const auto result = ReadResource(u":/bot_webview_shell/page.js"_q);
	return result;
}

[[nodiscard]] const QByteArray &BodyHtml() {
	static const auto result = ReadResource(u":/bot_webview_shell/body.html"_q);
	return result;
}

} // namespace

QByteArray InstallScript(const QString &shellToken) {
	const auto css = QString::fromUtf8(PageCss());
	const auto body = QString::fromUtf8(BodyHtml());
	auto pageJs = PageJs();
	pageJs.replace(
		QByteArray("TDESKTOP_SHELL_TOKEN_PLACEHOLDER"),
		JsonValue(shellToken));

	auto script = QByteArray();
	script += "if (window === window.top"
		" && !window.TelegramDesktopShell"
		" && !window.TelegramDesktopShellInstalling) {"
		"window.TelegramDesktopShellInstalling = true;"
		"try {"
		"if (!document.head) {"
		"document.documentElement.insertBefore("
		"document.createElement('head'),"
		"document.documentElement.firstChild);"
		"}"
		"if (!document.body) {"
		"document.documentElement.appendChild("
		"document.createElement('body'));"
		"}"
		"document.title = 'Telegram';"
		"const metaRobots = document.createElement('meta');"
		"metaRobots.name = 'robots';"
		"metaRobots.content = 'noindex, nofollow';"
		"document.head.appendChild(metaRobots);"
		"const metaViewport = document.createElement('meta');"
		"metaViewport.name = 'viewport';"
		"metaViewport.content = 'width=device-width, initial-scale=1.0';"
		"document.head.appendChild(metaViewport);"
		"const style = document.createElement('style');"
		"style.textContent = ";
	script += JsonValue(css);
	script += ";"
		"document.head.appendChild(style);"
		"document.body.insertAdjacentHTML('beforeend', ";
	script += JsonValue(body);
	script += ");";
	script += pageJs;
	script += "} finally {"
		"window.TelegramDesktopShellInstalling = false;"
		"}"
		"}";
	return script;
}

QByteArray MethodCallScript(
		const QByteArray &method,
		const QJsonObject &data,
		const QString &shellToken) {
	const auto payload = JsonObject(data);
	const auto token = JsonValue(shellToken);
	auto script = QByteArray();
	script.reserve(method.size() * 2 + payload.size() + token.size() + 98);
	script += "if (window.TelegramDesktopShell"
		" && window.TelegramDesktopShell.";
	script += method;
	script += ") { window.TelegramDesktopShell.";
	script += method;
	script += "(";
	script += payload;
	script += ", ";
	script += token;
	script += "); }";
	return script;
}

QByteArray EventScript(
		const QString &event,
		const QJsonObject &data,
		const QString &shellToken) {
	const auto eventValue = JsonValue(event);
	const auto payload = JsonObject(data);
	const auto token = JsonValue(shellToken);
	auto script = QByteArray();
	script += "if (window.TelegramDesktopShell) {"
		"window.TelegramDesktopShell.nativeEvent(";
	script += eventValue;
	script += ", ";
	script += payload;
	script += ", ";
	script += token;
	script += "); }";
	return script;
}

QJsonObject Metrics() {
	const auto &shellPadding = st::botWebViewShellPadding;
	const auto &shadowPadding = st::botWebViewShellShadowPadding;
	const auto &titlePadding = st::botWebViewShellTitlePadding;
	const auto &menuButtonSize = st::botWebViewShellMenuButtonSize;
	const auto fullscreenButtonSize = QSize(
		st::fullScreenPanelClose.width,
		st::fullScreenPanelClose.height);
	const auto fullscreenControlShift
		= st::separatePanelClose.rippleAreaPosition;
	return {
		{ u"shellRadius"_q, st::botWebViewShellRadius },
		{ u"shellPaddingTop"_q, shellPadding.top() },
		{ u"shellPaddingRight"_q, shellPadding.right() },
		{ u"shellPaddingBottom"_q, shellPadding.bottom() },
		{ u"shellPaddingLeft"_q, shellPadding.left() },
		{ u"shadowPaddingTop"_q, shadowPadding.top() },
		{ u"shadowPaddingRight"_q, shadowPadding.right() },
		{ u"shadowPaddingBottom"_q, shadowPadding.bottom() },
		{ u"shadowPaddingLeft"_q, shadowPadding.left() },
		{ u"headerHeight"_q, st::botWebViewShellHeaderHeight },
		{ u"titlePaddingTop"_q, titlePadding.top() },
		{ u"titlePaddingRight"_q, titlePadding.right() },
		{ u"titlePaddingBottom"_q, titlePadding.bottom() },
		{ u"titlePaddingLeft"_q, titlePadding.left() },
		{ u"badgeSkip"_q, st::botWebViewShellBadgeSkip },
		{ u"frameRadius"_q, st::botWebViewShellFrameRadius },
		{ u"controlWidth"_q, menuButtonSize.width() },
		{ u"controlHeight"_q, menuButtonSize.height() },
		{ u"buttonHeight"_q, st::botWebViewBottomButton.height },
		{ u"buttonGapX"_q, st::botWebViewBottomSkip.x() },
		{ u"buttonGapY"_q, st::botWebViewBottomSkip.y() },
		{ u"disclosureSkip"_q, st::botWebViewShellDisclosureSkip },
		{ u"footerButtonSkip"_q, st::botWebViewShellFooterButtonSkip },
		{ u"fullscreenControlWidth"_q, fullscreenButtonSize.width() },
		{ u"fullscreenControlHeight"_q, fullscreenButtonSize.height() },
		{ u"fullscreenControlTop"_q, fullscreenControlShift.y() },
		{ u"fullscreenControlRight"_q, fullscreenControlShift.x() },
		{ u"fullscreenControlGap"_q, fullscreenControlShift.x() },
	};
}

QSize WindowSize(QSize contentSize) {
	const auto &shadowPadding = st::botWebViewShellShadowPadding;
	return contentSize + QSize(
		shadowPadding.left() + shadowPadding.right(),
		shadowPadding.top()
			+ st::botWebViewShellHeaderHeight
			+ shadowPadding.bottom());
}

QJsonObject MenuPalette() {
	return {
		{ u"bg"_q, st::windowBg->c.name(QColor::HexRgb) },
		{ u"fg"_q, st::windowFg->c.name(QColor::HexRgb) },
		{ u"hoverBg"_q, st::windowBgOver->c.name(QColor::HexRgb) },
		{ u"ripple"_q, st::windowBgRipple->c.name(QColor::HexRgb) },
		{ u"separator"_q, st::menuSeparatorFg->c.name(QColor::HexRgb) },
		{ u"attention"_q,
			st::menuIconAttentionColor->c.name(QColor::HexRgb) },
	};
}

QJsonObject ColorPayload(const ResolvedColors &colors) {
	return {
		{ u"bodyBg"_q, ColorValue(colors.bodyBg) },
		{ u"titleBg"_q, ColorValue(colors.titleBg) },
		{ u"bottomBg"_q, ColorValue(colors.bottomBg) },
	};
}

} // namespace Ui::BotWebView::LinuxShell

#endif // !Q_OS_WIN && !Q_OS_MAC
