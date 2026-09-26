/*
This file is part of Telegram Desktop,
the official desktop application for the Telegram messaging service.

For license and copyright information please follow this link:
https://github.com/telegramdesktop/tdesktop/blob/master/LEGAL
*/
#pragma once

#include "iv/iv_delegate.h"
#include "ui/text/text.h"
#include "webview/webview_common.h"

class QKeyEvent;

namespace Webview {
class Window;
} // namespace Webview

namespace Ui {
class FlatLabel;
class IconButton;
class RpWidget;
class RpWindow;
} // namespace Ui

namespace Iv {

class Controller final {
public:
	explicit Controller(not_null<Delegate*> delegate);
	~Controller();

	struct Event {
		enum class Type {
			Close,
			Quit,
			OpenChannel,
			JoinChannel,
			OpenPage,
			OpenLink,
			OpenLinkExternal,
			OpenMedia,
			Report,
		};
		Type type = Type::Close;
		QString url;
		QString context;
		uint64 webpageId = 0;
	};

	[[nodiscard]] static bool IsGoodTonSiteUrl(const QString &uri);
	void showTonSite(const Webview::StorageId &storageId, QString uri);

	[[nodiscard]] bool active() const;
	void minimize();

	[[nodiscard]] rpl::producer<Event> events() const {
		return _events.events();
	}

	[[nodiscard]] rpl::lifetime &lifetime();

private:
	void createWindow();
	void createWebview(const Webview::StorageId &storageId);

	void processKey(QKeyEvent *event);
	void updateTitleGeometry(int newWidth) const;

	void activate();
	void setInnerFocus();
	void close();
	void quit();

	void showWebviewError();
	void showWebviewError(TextWithEntities text);

	const not_null<Delegate*> _delegate;

	std::unique_ptr<Ui::RpWindow> _window;
	std::unique_ptr<Ui::RpWidget> _subtitleWrap;
	rpl::variable<QString> _url;
	rpl::variable<QString> _subtitleText;
	rpl::variable<QString> _windowTitleText;
	Ui::FlatLabel *_subtitle = nullptr;
	Ui::IconButton *_back = nullptr;
	Ui::IconButton *_forward = nullptr;
	Ui::RpWidget *_container = nullptr;
	std::unique_ptr<Webview::Window> _webview;
	rpl::event_stream<Event> _events;

	rpl::lifetime _lifetime;

};

} // namespace Iv
