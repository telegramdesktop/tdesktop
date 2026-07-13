/*
This file is part of Telegram Desktop,
the official desktop application for the Telegram messaging service.

For license and copyright information please follow this link:
https://github.com/telegramdesktop/tdesktop/blob/master/LEGAL
*/
#pragma once

#include "base/timer.h"
#include "iv/markdown/iv_markdown_common.h"
#include "ui/effects/radial_animation.h"
#include "ui/rp_widget.h"

#include <functional>
#include <memory>
#include <string>

#include <QtCore/QPointer>
#include <QtCore/QRect>
#include <QtCore/QSize>

class QEvent;
class QJsonDocument;
class QMouseEvent;
class QObject;
class QPaintEvent;
class QWidget;
struct TextWithEntities;

namespace Ui {
class FlatLabel;
class IconButton;
template <typename Widget>
class PaddingWrap;
class RpWidget;
} // namespace Ui

namespace Webview {
struct DataRequest;
enum class DataResult;
class Window;
struct WindowConfig;
} // namespace Webview

namespace Iv::Markdown {

class EmbedOverlay final : public Ui::RpWidget {
public:
	enum class Mode {
		Hidden,
		EmbeddedPreload,
		EmbeddedVisible,
		Error,
		External,
	};

	EmbedOverlay(
		QWidget *parent,
		std::function<void(QString)> linkActivationCallback,
		Webview::StorageId storageId);
	~EmbedOverlay();

	[[nodiscard]] bool preloadEmbed(
		const EmbedRequest &request,
		std::function<void()> shownCallback = {},
		std::function<void()> failedCallback = {});
	[[nodiscard]] bool showEmbed(const EmbedRequest &request);
	[[nodiscard]] bool showExternalEmbed(const EmbedRequest &request);
	void cancelPreload();
	void closeEmbed();
	void updateGeometry(QRect geometry, int contentWidth);
	void testHandleWebviewMessage(const QJsonDocument &message);
	void testHandleNavigationDone(bool success);
	[[nodiscard]] bool testLoadingCoverVisible() const;
	[[nodiscard]] bool testReadyDelayScheduled() const;
	void testFireReadyDelay();
	[[nodiscard]] const Webview::StorageId &testEffectiveStorageId() const;
	[[nodiscard]] QRect testBodyGeometry() const;
	[[nodiscard]] Mode testMode() const;

protected:
	bool eventFilter(QObject *object, QEvent *event) override;
	void paintEvent(QPaintEvent *e) override;
	void mousePressEvent(QMouseEvent *e) override;
	void mouseReleaseEvent(QMouseEvent *e) override;

private:
	void installEscapeFilter();
	void removeEscapeFilter();
	[[nodiscard]] bool eventFromOverlayWindow(QObject *object) const;
	void resetState();
	[[nodiscard]] bool startEmbed(
		const EmbedRequest &request,
		Mode mode,
		bool showErrorOnFailure,
		std::function<void()> shownCallback,
		std::function<void()> failedCallback);
	void ensureWebview();
	[[nodiscard]] Webview::WindowConfig makeWindowConfig() const;
	[[nodiscard]] QSize externalInitialSize() const;
	void handleWebviewMessage(const QJsonDocument &message);
	void handleNavigationDone(bool success);
	void cancelReadyDelay();
	void setReady();
	void revealReadyEmbed();
	void applyPreferredBodySize(QSize size);
	void applyPreferredBodyHeight(int height);
	void updateCssToQtScale(int viewportWidth);
	void updateContentGeometry();
	void updateWebviewGeometry();
	void raiseSurfaces();
	void hideWebview();
	void destroyWebview();
	void showWebviewError();
	void showWebviewError(const TextWithEntities &text);
	void clearWebviewError();
	void restoreFocus();
	[[nodiscard]] QRect bodyGeometry() const;
	[[nodiscard]] QRect bodyRectInContent() const;
	[[nodiscard]] QMargins contentPadding() const;
	[[nodiscard]] int cssPixelsToQt(int value) const;
	[[nodiscard]] QByteArray htmlRequestId();
	[[nodiscard]] Webview::DataResult handleDataRequest(
		Webview::DataRequest request);

	const QPointer<QWidget> _webviewParent;
	const std::function<void(QString)> _linkActivationCallback;
	const Webview::StorageId _storageId;
	Ui::RpWidget *_content = nullptr;
	Ui::IconButton *_close = nullptr;
	Ui::PaddingWrap<Ui::FlatLabel> *_error = nullptr;
	Ui::FlatLabel *_errorLabel = nullptr;
	std::unique_ptr<Webview::Window> _webview;
	base::Timer _readyDelayTimer;
	Ui::InfiniteRadialAnimation _loadingAnimation;
	EmbedRequest _request;
	QRect _contentGeometry;
	QSize _preferredBodySize;
	QSize _pendingPreferredBodySize;
	QByteArray _htmlRequestId;
	QPointer<QWidget> _focusRestore;
	std::function<void()> _shownCallback;
	std::function<void()> _failedCallback;
	int _contentWidth = 0;
	int _dataResourceGeneration = 0;
	int _webviewGeneration = 0;
	double _cssToQtScale = 1.;
	Mode _mode = Mode::Hidden;
	bool _pressedOutside = false;
	bool _externalWindowCloseReported = false;
	bool _ready = false;
	bool _loading = false;
	bool _showErrorOnFailure = false;
	bool _escapeFilterInstalled = false;

};

} // namespace Iv::Markdown
