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
#include "ui/platform/ui_platform_utility.h"
#include "ui/rect_part.h"
#include "ui/widgets/separate_panel.h"
#include "webview/webview_common.h"
#include <crl/crl_time.h>
#include <QtCore/QRect>
#include <QtCore/QSize>
#include <QtGui/QColor>
#include <QtGui/QImage>

class QJsonObject;
class QJsonValue;

namespace Ui {
class FlatLabel;
class BoxContent;
class RpWidget;
class StandaloneLayerStack;
enum class LayerOption;
using LayerOptions = base::flags<LayerOption>;
} // namespace Ui

namespace Webview {
struct Available;
struct PopupArgs;
struct PopupResult;
} // namespace Webview

namespace Ui::Text {
struct MarkedContext;
} // namespace Ui::Text

namespace Ui::BotWebView::LinuxShell {
struct ResolvedColors;
} // namespace Ui::BotWebView::LinuxShell

namespace Ui::BotWebView {

struct DownloadsProgress;
struct DownloadsEntry;
enum class DownloadsAction;

[[nodiscard]] TextWithEntities ErrorText(const Webview::Available &info);

enum class MenuButton {
	None               = 0x00,
	OpenBot            = 0x01,
	RemoveFromMenu     = 0x02,
	RemoveFromMainMenu = 0x04,
	ShareGame          = 0x08,
};
inline constexpr bool is_flag_type(MenuButton) { return true; }
using MenuButtons = base::flags<MenuButton>;

using CustomMethodResult = base::expected<QByteArray, QString>;
struct CustomMethodRequest {
	QString method;
	QByteArray params;
	Fn<void(CustomMethodResult)> callback;
};

struct SetEmojiStatusRequest {
	uint64 customEmojiId = 0;
	TimeId duration = 0;
	Fn<void(QString)> callback;
};

struct DownloadFileRequest {
	QString url;
	QString name;
	Fn<void(bool)> callback;
};

struct ResolveButtonEmojiRequest {
	uint64 customEmojiId = 0;
	QColor textColor;
	int size = 0;
	Fn<void(QImage)> callback;
};

struct SendPreparedMessageRequest {
	QString id = 0;
	Fn<void(QString)> callback;
};

struct RequestChatRequest {
	QString requestId;
	Fn<void(QString)> callback;
};

class Delegate {
public:
	[[nodiscard]] virtual Webview::ThemeParams botThemeParams() = 0;
	[[nodiscard]] virtual Ui::Text::MarkedContext botTextContext() = 0;
	[[nodiscard]] virtual auto botDownloads(bool forceCheck = false)
		-> const std::vector<DownloadsEntry> & = 0;
	virtual void botDownloadsAction(uint32 id, DownloadsAction type) = 0;
	virtual bool botHandleLocalUri(QString uri, bool keepOpen) = 0;
	virtual void botHandleInvoice(QString slug) = 0;
	virtual void botHandleMenuButton(MenuButton button) = 0;
	virtual bool botValidateExternalLink(QString uri) = 0;
	virtual void botOpenIvLink(QString uri) = 0;
	virtual void botSendData(QByteArray data) = 0;
	virtual void botSwitchInlineQuery(
		std::vector<QString> chatTypes,
		QString query) = 0;
	virtual void botCheckWriteAccess(Fn<void(bool allowed)> callback) = 0;
	virtual void botAllowWriteAccess(Fn<void(bool allowed)> callback) = 0;
	virtual bool botStorageWrite(
		QString key,
		std::optional<QString> value) = 0;
	[[nodiscard]] virtual std::optional<QString> botStorageRead(
		QString key) = 0;
	virtual void botStorageClear() = 0;
	virtual void botRequestEmojiStatusAccess(
		Fn<void(bool allowed)> callback) = 0;
	virtual void botSharePhone(Fn<void(bool shared)> callback) = 0;
	virtual void botInvokeCustomMethod(CustomMethodRequest request) = 0;
	virtual void botSetEmojiStatus(SetEmojiStatusRequest request) = 0;
	virtual void botDownloadFile(DownloadFileRequest request) = 0;
	virtual void botResolveButtonEmoji(ResolveButtonEmojiRequest request) = 0;
	virtual void botSendPreparedMessage(
		SendPreparedMessageRequest request) = 0;
	virtual void botRequestChat(RequestChatRequest request) = 0;
	virtual void botVerifyAge(int age) = 0;
	virtual void botOpenPrivacyPolicy() = 0;
	virtual void botClose() = 0;
};

struct Args {
	QString url;
	Webview::StorageId storageId;
	rpl::producer<QString> title;
	Ui::TitleBadgeDescriptor titleBadge;
	rpl::producer<QString> bottom;
	not_null<Delegate*> delegate;
	MenuButtons menuButtons;
	bool fullscreen = false;
	bool sameOrigin = false;
	bool allowClipboardRead = false;
	rpl::producer<DownloadsProgress> downloadsProgress;
};

class Panel final : public base::has_weak_ptr {
public:
	explicit Panel(Args &&args);
	~Panel();

	void requestActivate();
	void toggleProgress(bool shown);

	void showBox(object_ptr<BoxContent> box);
	void showBox(
		object_ptr<BoxContent> box,
		LayerOptions options,
		anim::type animated);
	void hideLayer(anim::type animated);
	void showToast(TextWithEntities &&text);
	not_null<QWidget*> toastParent() const;
	void showCriticalError(const TextWithEntities &text);
	void showWebviewError(
		const QString &text,
		const Webview::Available &information);

	void updateThemeParams(const Webview::ThemeParams &params);

	void hideForPayment();
	void invoiceClosed(const QString &slug, const QString &status);

	[[nodiscard]] rpl::lifetime &lifetime();

private:
	struct ButtonArgs {
		bool isActive = false;
		bool isVisible = false;
		bool isProgressVisible = false;
		uint64 iconCustomEmojiId = 0;
		QString text;
	};
	struct ExternalButtonState {
		ButtonArgs args;
		QColor color;
		QColor textColor;
		QString position;
		uint64 iconGeneration = 0;
	};
	struct ExternalShellColorState {
		bool titleUsesTheme = true;
		bool bodyUsesTheme = true;
		bool bottomUsesTheme = true;
		std::optional<QColor> title;
		std::optional<QColor> body;
		std::optional<QColor> bottom;
	};
	struct ExternalShellAnchor {
		std::optional<QRect> anchorGeometry;
		std::optional<QSize> outerSize;
		Platform::ForeignParent transientParent;
	};
	class Button;
	struct Progress;
	struct WebviewWithLifetime;

	bool showWebview(Args &&args, const Webview::ThemeParams &params);
	void invalidateExternalShellSession();
	void showExternalShellError(TextWithEntities text);

	bool createWebview(const Webview::ThemeParams &params);
	void resetExternalShellIdentity();
	[[nodiscard]] QWidget *webviewWindowForPopup() const;
	void installExternalShellDocument();
	void sendExternalShellBootstrap();
	void sendExternalShellMethod(
		const QByteArray &method,
		const QJsonObject &data);
	void sendExternalShellEvent(
		const QString &event,
		const QJsonObject &data);
	void sendExternalShellButton(
		const char *name,
		const QJsonObject &args);
	void sendExternalShellMenu();
	void sendExternalShellAssets();
	void handleExternalShellMenuAction(const QString &id);
	void requestExternalShellButtonEmoji(const QString &name);
	void applyExternalShellFullscreen(bool fullscreen);
	void sendExternalShellChrome();
	void setExternalShellBlocked(bool blocked);
	void closeExternalShellLayer();
	[[nodiscard]] ExternalShellAnchor externalShellAnchor() const;
	void showPopup(
		Webview::PopupArgs &&args,
		Fn<void(Webview::PopupResult)> done);
	void createWebviewBottom();
	void showWebviewProgress();
	void hideWebviewProgress();
	void setupDownloadsProgress(
		not_null<RpWidget*> button,
		rpl::producer<DownloadsProgress> progress,
		bool fullscreen);
	void setTitle(rpl::producer<QString> title);
	void sendDataMessage(const QJsonObject &args);
	void switchInlineQueryMessage(const QJsonObject &args);
	void processSendMessageRequest(const QJsonObject &args);
	void processRequestChat(const QJsonObject &args);
	void processEmojiStatusRequest(const QJsonObject &args);
	void processEmojiStatusAccessRequest();
	void processStorageSaveKey(const QJsonObject &args);
	void processStorageGetKey(const QJsonObject &args);
	void processStorageClear(const QJsonObject &args);
	void processButtonMessage(
		std::unique_ptr<Button> &button,
		const QJsonObject &args);
	void processBackButtonMessage(const QJsonObject &args);
	void processSettingsButtonMessage(const QJsonObject &args);
	void processHeaderColor(const QJsonObject &args);
	void processBackgroundColor(const QJsonObject &args);
	void processBottomBarColor(const QJsonObject &args);
	void setExternalShellTitleColor(std::optional<QColor> color);
	void setExternalShellBodyColor(std::optional<QColor> color);
	void setExternalShellBottomColor(std::optional<QColor> color);
	[[nodiscard]] LinuxShell::ResolvedColors externalShellColors(
		const Webview::ThemeParams &params) const;
	void sendExternalShellColors(const Webview::ThemeParams &params);
	void processDownloadRequest(const QJsonObject &args);
	void openTgLink(const QJsonObject &args);
	void openExternalLink(const QJsonObject &args);
	void openInvoice(const QJsonObject &args);
	void openPopup(const QJsonObject &args);
	void openScanQrPopup(const QJsonObject &args);
	void openShareStory(const QJsonObject &args);
	void requestWriteAccess();
	void replyRequestWriteAccess(bool allowed);
	void requestPhone();
	void replyRequestPhone(bool shared);
	void invokeCustomMethod(const QJsonObject &args);
	void replyCustomMethod(QJsonValue requestId, QJsonObject response);
	void requestClipboardText(const QJsonObject &args);
	void setupClosingBehaviour(const QJsonObject &args);
	void replyDeviceStorage(
		const QJsonObject &args,
		const QString &event,
		QJsonObject response);
	void deviceStorageFailed(const QJsonObject &args, QString error);
	void secureStorageFailed(const QJsonObject &args);
	void createButton(std::unique_ptr<Button> &button);
	void scheduleCloseWithConfirmation();
	void closeWithConfirmation();
	void sendViewport();
	void sendSafeArea();
	void sendContentSafeArea();
	void sendFullScreen();

	void updateColorOverrides(const Webview::ThemeParams &params);
	void overrideBodyColor(std::optional<QColor> color);

	using EventData = std::variant<QString, QJsonObject>;
	void postEvent(const QString &event);
	void postEvent(const QString &event, EventData data);

	[[nodiscard]] bool allowOpenLink() const;
	[[nodiscard]] bool allowClipboardQuery() const;
	[[nodiscard]] bool progressWithBackground() const;
	[[nodiscard]] QRect progressRect() const;
	void setupProgressGeometry();
	void layoutButtons();

	Webview::StorageId _storageId;
	const not_null<Delegate*> _delegate;
	QString _externalUrl;
	QString _externalTitle;
	int _externalBlockCount = 0;
	bool _closeNeedConfirmation = false;
	bool _hasSettingsButton = false;
	bool _externalTitleBadgeVisible = false;
	bool _externalShell = false;
	bool _externalShellBootstrapped = false;
	bool _externalWindowCloseRequested = false;
	QString _externalShellToken;
	QString _initialOrigin;
	QString _currentOrigin;
	uint64 _externalShellGeneration = 0;
	bool _externalBackVisible = false;
	ExternalShellColorState _externalShellColorState;
	MenuButtons _menuButtons = {};
	ExternalButtonState _externalMainButton;
	ExternalButtonState _externalSecondaryButton;
	std::unique_ptr<RpWidget> _externalPanelParent;
	std::unique_ptr<SeparatePanel> _widget;
	std::unique_ptr<WebviewWithLifetime> _webview;
	std::unique_ptr<StandaloneLayerStack> _externalLayer;
	std::unique_ptr<RpWidget> _externalWebviewParent;
	std::unique_ptr<RpWidget> _webviewBottom;
	QPointer<FlatLabel> _webviewBottomLabel;
	rpl::variable<QString> _bottomText;
	QPointer<RpWidget> _webviewParent;
	std::unique_ptr<RpWidget> _bottomButtonsBg;
	std::unique_ptr<Button> _mainButton;
	std::unique_ptr<Button> _secondaryButton;
	RectPart _secondaryPosition = RectPart::Left;
	rpl::variable<int> _footerHeight = 0;
	std::unique_ptr<Progress> _progress;
	rpl::event_stream<> _themeUpdateForced;
	std::optional<QColor> _bottomBarColor;
	rpl::lifetime _headerColorLifetime;
	rpl::lifetime _bodyColorLifetime;
	rpl::lifetime _bottomBarColorLifetime;
	rpl::event_stream<> _downloadsUpdated;
	rpl::variable<bool> _fullscreen = false;
	crl::time _lastWebviewInteraction = 0;
	bool _layerShown : 1 = false;
	bool _webviewProgress : 1 = false;
	bool _themeUpdateScheduled : 1 = false;
	bool _hiddenForPayment : 1 = false;
	bool _closeWithConfirmationScheduled : 1 = false;
	bool _allowClipboardRead : 1 = false;
	bool _sameOrigin : 1 = false;
	bool _inBlockingRequest : 1 = false;
	bool _headerColorReceived : 1 = false;
	bool _bodyColorReceived : 1 = false;
	bool _bottomColorReceived : 1 = false;

};

[[nodiscard]] std::unique_ptr<Panel> Show(Args &&args);

} // namespace Ui::BotWebView
