/*
This file is part of Telegram Desktop,
the official desktop application for the Telegram messaging service.

For license and copyright information please follow this link:
https://github.com/telegramdesktop/tdesktop/blob/master/LEGAL
*/
#pragma once

#include "base/invoke_queued.h"
#include "base/unique_qptr.h"
#include "ui/effects/animations.h"
#include "ui/text/text.h"

class Painter;

namespace Webview {
struct DataRequest;
class Window;
} // namespace Webview

namespace Ui {
class RpWidget;
class RpWindow;
class PopupMenu;
} // namespace Ui

namespace Iv {

struct Prepared;

class Controller final {
public:
	Controller();
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
		};
		Type type = Type::Close;
		QString url;
		QString context;
	};

	[[nodiscard]] bool showFast(const QString &url, const QString &hash);
	void show(
		const QString &dataPath,
		Prepared page,
		base::flat_map<QByteArray, rpl::producer<bool>> inChannelValues);
	void update(Prepared page);

	[[nodiscard]] bool active() const;
	void showJoinedTooltip();
	void minimize();

	[[nodiscard]] rpl::producer<Webview::DataRequest> dataRequests() const {
		return _dataRequests.events();
	}

	[[nodiscard]] rpl::producer<Event> events() const {
		return _events.events();
	}

	[[nodiscard]] rpl::lifetime &lifetime();

private:
	void createWindow();
	void createWebview(const QString &dataPath);
	[[nodiscard]] QByteArray navigateScript(int index, const QString &hash);
	[[nodiscard]] QByteArray reloadScript(int index);

	void updateTitleGeometry();
	void paintTitle(Painter &p, QRect clip);
	void showInWindow(const QString &dataPath, Prepared page);
	[[nodiscard]] QByteArray fillInChannelValuesScript(
		base::flat_map<QByteArray, rpl::producer<bool>> inChannelValues);
	[[nodiscard]] QByteArray toggleInChannelScript(
		const QByteArray &id,
		bool in) const;

	void processKey(const QString &key, const QString &modifier);
	void processLink(const QString &url, const QString &context);

	void menu(int index, const QString &hash);
	void escape();
	void close();
	void quit();

	std::unique_ptr<Ui::RpWindow> _window;
	std::unique_ptr<Ui::RpWidget> _title;
	base::unique_qptr<Ui::PopupMenu> _menu;
	Ui::Text::String _titleText;
	int _titleLeftSkip = 0;
	int _titleRightSkip = 0;
	Ui::RpWidget *_container = nullptr;
	std::unique_ptr<Webview::Window> _webview;
	rpl::event_stream<Webview::DataRequest> _dataRequests;
	rpl::event_stream<Event> _events;
	base::flat_map<QByteArray, bool> _inChannelChanged;
	base::flat_set<QByteArray> _inChannelSubscribed;
	SingleQueuedInvokation _updateStyles;
	bool _reloadInitialWhenReady = false;
	bool _subscribedToColors = false;
	bool _ready = false;

	std::vector<Prepared> _pages;
	base::flat_map<QString, int> _indices;
	QString _navigateToHashWhenReady;
	int _navigateToIndexWhenReady = -1;

	rpl::lifetime _lifetime;

};

} // namespace Iv
