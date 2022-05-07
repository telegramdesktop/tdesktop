/*
This file is part of Telegram Desktop,
the official desktop application for the Telegram messaging service.

For license and copyright information please follow this link:
https://github.com/telegramdesktop/tdesktop/blob/master/LEGAL
*/
#pragma once

#include "base/object_ptr.h"

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

class Panel final {
public:
	Panel(
		const QString &userDataPath,
		rpl::producer<QString> title,
		Fn<bool(QString)> handleLocalUri,
		Fn<void(QByteArray)> sendData,
		Fn<void()> close,
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
	void createMainButton();

	void postEvent(const QString &event, const QString &data = {});

	[[nodiscard]] bool progressWithBackground() const;
	[[nodiscard]] QRect progressRect() const;
	void setupProgressGeometry();

	QString _userDataPath;
	Fn<bool(QString)> _handleLocalUri;
	Fn<void(QByteArray)> _sendData;
	Fn<void()> _close;
	std::unique_ptr<SeparatePanel> _widget;
	std::unique_ptr<WebviewWithLifetime> _webview;
	std::unique_ptr<RpWidget> _webviewBottom;
	QPointer<QWidget> _webviewParent;
	std::unique_ptr<Button> _mainButton;
	std::unique_ptr<Progress> _progress;
	rpl::event_stream<> _themeUpdateForced;
	rpl::lifetime _fgLifetime;
	rpl::lifetime _bgLifetime;
	bool _webviewProgress = false;
	bool _themeUpdateScheduled = false;

};

struct Args {
	QString url;
	QString userDataPath;
	rpl::producer<QString> title;
	rpl::producer<QString> bottom;
	Fn<bool(QString)> handleLocalUri;
	Fn<void(QByteArray)> sendData;
	Fn<void()> close;
	Fn<Webview::ThemeParams()> themeParams;
};
[[nodiscard]] std::unique_ptr<Panel> Show(Args &&args);

} // namespace Ui::BotWebView
