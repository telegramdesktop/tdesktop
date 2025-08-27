/*
This file is part of Telegram Desktop,
the official desktop application for the Telegram messaging service.

For license and copyright information please follow this link:
https://github.com/telegramdesktop/tdesktop/blob/master/LEGAL
*/
#pragma once

#include "base/invoke_queued.h"
#include "base/object_ptr.h"
#include "base/unique_qptr.h"
#include "iv/iv_delegate.h"
#include "ui/effects/animations.h"
#include "ui/text/text.h"
#include "webview/webview_common.h"

class Painter;

namespace Webview {
struct DataRequest;
class Window;
} // namespace Webview

namespace Ui {
class RpWidget;
class RpWindow;
class PopupMenu;
class FlatLabel;
class IconButton;
template <typename Widget>
class FadeWrapScaled;
} // namespace Ui

namespace Iv {

struct Prepared;

struct ShareBoxResult {
	Fn<void()> focus;
	Fn<void()> hide;
	rpl::producer<> destroyRequests;
};
struct ShareBoxDescriptor {
	not_null<Ui::RpWidget*> parent;
	QString url;
};

class Controller final {
public:
	Controller(
		not_null<Delegate*> delegate,
		Fn<ShareBoxResult(ShareBoxDescriptor)> showShareBox);
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
	};

	void show(
		const Webview::StorageId &storageId,
		Prepared page,
		base::flat_map<QByteArray, rpl::producer<bool>> inChannelValues);
	void update(Prepared page);

	[[nodiscard]] static bool IsGoodTonSiteUrl(const QString &uri);
	void showTonSite(const Webview::StorageId &storageId, QString uri);

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
	void createWebview(const Webview::StorageId &storageId);
	[[nodiscard]] QByteArray navigateScript(int index, const QString &hash);
	[[nodiscard]] QByteArray reloadScript(int index);

	void showInWindow(const Webview::StorageId &storageId, Prepared page);
	[[nodiscard]] QByteArray fillInChannelValuesScript(
		base::flat_map<QByteArray, rpl::producer<bool>> inChannelValues);
	[[nodiscard]] QByteArray toggleInChannelScript(
		const QByteArray &id,
		bool in) const;

	void processKey(const QString &key, const QString &modifier);
	void processLink(const QString &url, const QString &context);

	void initControls();
	void updateTitleGeometry(int newWidth) const;

	void activate();
	void setInnerFocus();
	void showMenu();
	void escape();
	void close();
	void quit();

	[[nodiscard]] QString composeCurrentUrl() const;
	[[nodiscard]] uint64 compuseCurrentPageId() const;
	void showShareMenu();
	void destroyShareMenu();

	void showWebviewError();
	void showWebviewError(TextWithEntities text);

	const not_null<Delegate*> _delegate;

	std::unique_ptr<Ui::RpWindow> _window;
	std::unique_ptr<Ui::RpWidget> _subtitleWrap;
	rpl::variable<QString> _url;
	rpl::variable<QString> _subtitleText;
	rpl::variable<QString> _windowTitleText;
	std::unique_ptr<Ui::FlatLabel> _subtitle;
	Ui::Animations::Simple _subtitleBackShift;
	Ui::Animations::Simple _subtitleForwardShift;
	object_ptr<Ui::IconButton> _menuToggle = { nullptr };
	object_ptr<Ui::FadeWrapScaled<Ui::IconButton>> _back = { nullptr };
	object_ptr<Ui::FadeWrapScaled<Ui::IconButton>> _forward = { nullptr };
	base::unique_qptr<Ui::PopupMenu> _menu;
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

	rpl::variable<int> _index = -1;
	QString _hash;

	Fn<ShareBoxResult(ShareBoxDescriptor)> _showShareBox;
	std::unique_ptr<Ui::RpWidget> _shareWrap;
	std::unique_ptr<QWidget> _shareContainer;
	Fn<void()> _shareFocus;
	Fn<void()> _shareHide;
	bool _shareHidesContent = false;

	std::vector<Prepared> _pages;
	base::flat_map<QString, int> _indices;
	QString _navigateToHashWhenReady;
	int _navigateToIndexWhenReady = -1;

	rpl::lifetime _lifetime;

};

} // namespace Iv
