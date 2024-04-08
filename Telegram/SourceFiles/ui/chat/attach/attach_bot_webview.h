/*
This file is part of Telegram Desktop,
the official desktop application for the Telegram messaging service.

For license and copyright information please follow this link:
https://github.com/telegramdesktop/tdesktop/blob/master/LEGAL
*/
#pragma once

#include "base/expected.h"
#include "base/object_ptr.h"
#include "base/weak_ptr.h"
#include "base/flags.h"

class QJsonObject;
class QJsonValue;

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
	None               = 0x00,
	OpenBot            = 0x01,
	RemoveFromMenu     = 0x02,
	RemoveFromMainMenu = 0x04,
};
inline constexpr bool is_flag_type(MenuButton) { return true; }
using MenuButtons = base::flags<MenuButton>;

using CustomMethodResult = base::expected<QByteArray, QString>;
struct CustomMethodRequest {
	QString method;
	QByteArray params;
	Fn<void(CustomMethodResult)> callback;
};

class Delegate {
public:
	virtual Webview::ThemeParams botThemeParams() = 0;
	virtual bool botHandleLocalUri(QString uri, bool keepOpen) = 0;
	virtual void botHandleInvoice(QString slug) = 0;
	virtual void botHandleMenuButton(MenuButton button) = 0;
	virtual void botOpenIvLink(QString uri) = 0;
	virtual void botSendData(QByteArray data) = 0;
	virtual void botSwitchInlineQuery(
		std::vector<QString> chatTypes,
		QString query) = 0;
	virtual void botCheckWriteAccess(Fn<void(bool allowed)> callback) = 0;
	virtual void botAllowWriteAccess(Fn<void(bool allowed)> callback) = 0;
	virtual void botSharePhone(Fn<void(bool shared)> callback) = 0;
	virtual void botInvokeCustomMethod(CustomMethodRequest request) = 0;
	virtual void botClose() = 0;
};

class Panel final : public base::has_weak_ptr {
public:
	Panel(
		const QString &userDataPath,
		rpl::producer<QString> title,
		not_null<Delegate*> delegate,
		MenuButtons menuButtons,
		bool allowClipboardRead);
	~Panel();

	void requestActivate();
	void toggleProgress(bool shown);

	bool showWebview(
		const QString &url,
		const Webview::ThemeParams &params,
		rpl::producer<QString> bottomText);

	void showBox(object_ptr<BoxContent> box);
	void showToast(TextWithEntities &&text);
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

	bool createWebview(const Webview::ThemeParams &params);
	void createWebviewBottom();
	void showWebviewProgress();
	void hideWebviewProgress();
	void setTitle(rpl::producer<QString> title);
	void sendDataMessage(const QJsonObject &args);
	void switchInlineQueryMessage(const QJsonObject &args);
	void processMainButtonMessage(const QJsonObject &args);
	void processBackButtonMessage(const QJsonObject &args);
	void processSettingsButtonMessage(const QJsonObject &args);
	void processHeaderColor(const QJsonObject &args);
	void openTgLink(const QJsonObject &args);
	void openExternalLink(const QJsonObject &args);
	void openInvoice(const QJsonObject &args);
	void openPopup(const QJsonObject &args);
	void requestWriteAccess();
	void replyRequestWriteAccess(bool allowed);
	void requestPhone();
	void replyRequestPhone(bool shared);
	void invokeCustomMethod(const QJsonObject &args);
	void replyCustomMethod(QJsonValue requestId, QJsonObject response);
	void requestClipboardText(const QJsonObject &args);
	void setupClosingBehaviour(const QJsonObject &args);
	void createMainButton();
	void scheduleCloseWithConfirmation();
	void closeWithConfirmation();
	void sendViewport();

	using EventData = std::variant<QString, QJsonObject>;
	void postEvent(const QString &event);
	void postEvent(const QString &event, EventData data);

	[[nodiscard]] bool allowOpenLink() const;
	[[nodiscard]] bool allowClipboardQuery() const;
	[[nodiscard]] bool progressWithBackground() const;
	[[nodiscard]] QRect progressRect() const;
	void setupProgressGeometry();

	QString _userDataPath;
	const not_null<Delegate*> _delegate;
	bool _closeNeedConfirmation = false;
	bool _hasSettingsButton = false;
	MenuButtons _menuButtons = {};
	std::unique_ptr<SeparatePanel> _widget;
	std::unique_ptr<WebviewWithLifetime> _webview;
	std::unique_ptr<RpWidget> _webviewBottom;
	rpl::variable<QString> _bottomText;
	QPointer<QWidget> _webviewParent;
	std::unique_ptr<Button> _mainButton;
	mutable crl::time _mainButtonLastClick = 0;
	std::unique_ptr<Progress> _progress;
	rpl::event_stream<> _themeUpdateForced;
	rpl::lifetime _headerColorLifetime;
	rpl::lifetime _fgLifetime;
	rpl::lifetime _bgLifetime;
	bool _webviewProgress = false;
	bool _themeUpdateScheduled = false;
	bool _hiddenForPayment = false;
	bool _closeWithConfirmationScheduled = false;
	bool _allowClipboardRead = false;
	bool _inBlockingRequest = false;

};

struct Args {
	QString url;
	QString userDataPath;
	rpl::producer<QString> title;
	rpl::producer<QString> bottom;
	not_null<Delegate*> delegate;
	MenuButtons menuButtons;
	bool allowClipboardRead = false;
};
[[nodiscard]] std::unique_ptr<Panel> Show(Args &&args);

} // namespace Ui::BotWebView
