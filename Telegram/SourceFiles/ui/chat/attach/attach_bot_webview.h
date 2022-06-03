/*
This file is part of Telegram Desktop,
the official desktop application for the Telegram messaging service.

For license and copyright information please follow this link:
https://github.com/telegramdesktop/tdesktop/blob/master/LEGAL
*/
#pragma once

#include "base/object_ptr.h"
#include "base/weak_ptr.h"
#include "base/flags.h"

namespace Ui {
class BoxContent;
class RpWidget;
class SeparatePanel;
} // namespace Ui

namespace Webview {
struct Available;
struct ThemeParams;
} // namespace Webview

namespace Ui::BotWebView {

struct MainButtonArgs {
	bool isActive = false;
	bool isVisible = false;
	bool isProgressVisible = false;
	QString text;
};

enum class MenuButton {
	None           = 0x00,
	Settings       = 0x01,
	OpenBot        = 0x02,
	RemoveFromMenu = 0x04,
};
inline constexpr bool is_flag_type(MenuButton) { return true; }
using MenuButtons = base::flags<MenuButton>;

class Panel final : public base::has_weak_ptr {
public:
	Panel(
		const QString &userDataPath,
		rpl::producer<QString> title,
		Fn<bool(QString)> handleLocalUri,
		Fn<void(QString)> handleInvoice,
		Fn<void(QByteArray)> sendData,
		Fn<void()> close,
		MenuButtons menuButtons,
		Fn<void(MenuButton)> handleMenuButton,
		Fn<Webview::ThemeParams()> themeParams);
	~Panel();

	void requestActivate();
	void toggleProgress(bool shown);

	bool showWebview(
		const QString &url,
		const Webview::ThemeParams &params,
		rpl::producer<QString> bottomText);

	void showBox(object_ptr<BoxContent> box);
	void showToast(const TextWithEntities &text);
	void showCriticalError(const TextWithEntities &text);
	void showWebviewError(
		const QString &text,
		const Webview::Available &information);

	void updateThemeParams(const Webview::ThemeParams &params);

	void hideForPayment();
	void invoiceClosed(const QString &slug, const QString &status);

	[[nodiscard]] rpl::lifetime &lifetime();

private:
	class Button;
	struct Progress;
	struct WebviewWithLifetime;

	bool createWebview();
	void showWebviewProgress();
	void hideWebviewProgress();
	void setTitle(rpl::producer<QString> title);
	void sendDataMessage(const QJsonValue &value);
	void processMainButtonMessage(const QJsonValue &value);
	void processBackButtonMessage(const QJsonValue &value);
	void openTgLink(const QJsonValue &value);
	void openExternalLink(const QJsonValue &value);
	void openInvoice(const QJsonValue &value);
	void createMainButton();

	void postEvent(const QString &event, const QString &data = {});

	[[nodiscard]] bool progressWithBackground() const;
	[[nodiscard]] QRect progressRect() const;
	void setupProgressGeometry();

	QString _userDataPath;
	Fn<bool(QString)> _handleLocalUri;
	Fn<void(QString)> _handleInvoice;
	Fn<void(QByteArray)> _sendData;
	Fn<void()> _close;
	MenuButtons _menuButtons = {};
	Fn<void(MenuButton)> _handleMenuButton;
	std::unique_ptr<SeparatePanel> _widget;
	std::unique_ptr<WebviewWithLifetime> _webview;
	std::unique_ptr<RpWidget> _webviewBottom;
	QPointer<QWidget> _webviewParent;
	std::unique_ptr<Button> _mainButton;
	crl::time _mainButtonLastClick = 0;
	std::unique_ptr<Progress> _progress;
	rpl::event_stream<> _themeUpdateForced;
	rpl::lifetime _fgLifetime;
	rpl::lifetime _bgLifetime;
	bool _webviewProgress = false;
	bool _themeUpdateScheduled = false;
	bool _hiddenForPayment = false;

};

struct Args {
	QString url;
	QString userDataPath;
	rpl::producer<QString> title;
	rpl::producer<QString> bottom;
	Fn<bool(QString)> handleLocalUri;
	Fn<void(QString)> handleInvoice;
	Fn<void(QByteArray)> sendData;
	Fn<void()> close;
	MenuButtons menuButtons;
	Fn<void(MenuButton)> handleMenuButton;
	Fn<Webview::ThemeParams()> themeParams;
};
[[nodiscard]] std::unique_ptr<Panel> Show(Args &&args);

} // namespace Ui::BotWebView
