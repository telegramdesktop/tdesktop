/*
This file is part of Telegram Desktop,
the official desktop application for the Telegram messaging service.

For license and copyright information please follow this link:
https://github.com/telegramdesktop/tdesktop/blob/master/LEGAL
*/
#pragma once

#include "platform/platform_main_window.h"
#include "ui/layers/layer_widget.h"

class MainWidget;

namespace Intro {
class Widget;
enum class EnterPoint : uchar;
} // namespace Intro

namespace Window {
class MediaPreviewWidget;
class SectionMemento;
struct SectionShow;
class PasscodeLockWidget;
namespace Theme {
struct BackgroundUpdate;
class WarningWidget;
} // namespace Theme
} // namespace Window

namespace Ui {
class LinkButton;
class BoxContent;
class LayerStackWidget;
} // namespace Ui

class MediaPreviewWidget;

extern const char kOptionAutoScrollInactiveChat[];

class MainWindow : public Platform::MainWindow {
public:
	explicit MainWindow(not_null<Window::Controller*> controller);
	~MainWindow();

	void finishFirstShow();

	void preventOrInvoke(Fn<void()> callback);

	void setupPasscodeLock();
	void clearPasscodeLock();
	void setupIntro(Intro::EnterPoint point, QPixmap oldContentCache);
	void setupMain(MsgId singlePeerShowAtMsgId, QPixmap oldContentCache);

	void showSettings();

	void setInnerFocus() override;

	MainWidget *sessionContent() const;

	void checkActivation() override;
	[[nodiscard]] bool markingAsRead() const;

	bool takeThirdSectionFromLayer();

	void sendPaths();

	[[nodiscard]] bool contentOverlapped(const QRect &globalRect);
	[[nodiscard]] bool contentOverlapped(QWidget *w, QPaintEvent *e) {
		return contentOverlapped(
			QRect(w->mapToGlobal(e->rect().topLeft()), e->rect().size()));
	}
	[[nodiscard]] bool contentOverlapped(QWidget *w, const QRegion &r) {
		return contentOverlapped(QRect(
			w->mapToGlobal(r.boundingRect().topLeft()),
			r.boundingRect().size()));
	}

	void showMainMenu();
	void fixOrder() override;

	[[nodiscard]] QPixmap grabForSlideAnimation();

	void showOrHideBoxOrLayer(
		std::variant<
			v::null_t,
			object_ptr<Ui::BoxContent>,
			std::unique_ptr<Ui::LayerWidget>> &&layer,
		Ui::LayerOptions options,
		anim::type animated);
	void showSpecialLayer(
		object_ptr<Ui::LayerWidget> layer,
		anim::type animated);
	bool showSectionInExistingLayer(
		not_null<Window::SectionMemento*> memento,
		const Window::SectionShow &params);
	void ui_hideSettingsAndLayer(anim::type animated);
	void ui_removeLayerBlackout();
	[[nodiscard]] bool ui_isLayerShown() const;
	bool showMediaPreview(
		Data::FileOrigin origin,
		not_null<DocumentData*> document);
	bool showMediaPreview(
		Data::FileOrigin origin,
		not_null<PhotoData*> photo);
	void hideMediaPreview();

	void updateControlsGeometry() override;

protected:
	bool eventFilter(QObject *o, QEvent *e) override;
	void closeEvent(QCloseEvent *e) override;

	void initHook() override;
	void clearWidgetsHook() override;

private:
	void applyInitialWorkMode();
	void ensureLayerCreated();
	void destroyLayer();

	void themeUpdated(const Window::Theme::BackgroundUpdate &data);

	QPoint _lastMousePosition;

	object_ptr<Window::PasscodeLockWidget> _passcodeLock = { nullptr };
	object_ptr<Intro::Widget> _intro = { nullptr };
	object_ptr<MainWidget> _main = { nullptr };
	base::unique_qptr<Ui::LayerStackWidget> _layer;
	object_ptr<Window::MediaPreviewWidget> _mediaPreview = { nullptr };

	object_ptr<Window::Theme::WarningWidget> _testingThemeWarning = { nullptr };

};
