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
} // namespace Webview

namespace Ui::BotWebView {

class Panel final {
public:
	Panel(
		const QString &userDataPath,
		Fn<void(QByteArray)> sendData,
		Fn<void()> close,
		Fn<QByteArray()> themeParams);
	~Panel();

	void requestActivate();
	void toggleProgress(bool shown);

	bool showWebview(
		const QString &url,
		rpl::producer<QString> bottomText);

	void showBox(object_ptr<BoxContent> box);
	void showToast(const TextWithEntities &text);
	void showCriticalError(const TextWithEntities &text);

	void updateThemeParams(const QByteArray &json);

	[[nodiscard]] rpl::lifetime &lifetime();

private:
	struct Progress;
	struct WebviewWithLifetime;

	bool createWebview();
	void showWebviewProgress();
	void hideWebviewProgress();
	void showWebviewError(
		const QString &text,
		const Webview::Available &information);
	void setTitle(rpl::producer<QString> title);

	[[nodiscard]] bool progressWithBackground() const;
	[[nodiscard]] QRect progressRect() const;
	void setupProgressGeometry();

	QString _userDataPath;
	Fn<void(QByteArray)> _sendData;
	Fn<void()> _close;
	std::unique_ptr<SeparatePanel> _widget;
	std::unique_ptr<WebviewWithLifetime> _webview;
	std::unique_ptr<RpWidget> _webviewBottom;
	std::unique_ptr<Progress> _progress;
	bool _webviewProgress = false;
	bool _themeUpdateScheduled = false;

};

struct Args {
	QString url;
	QString userDataPath;
	Fn<void(QByteArray)> sendData;
	Fn<void()> close;
	Fn<QByteArray()> themeParams;
};
[[nodiscard]] std::unique_ptr<Panel> Show(Args &&args);

} // namespace Ui::BotWebView
