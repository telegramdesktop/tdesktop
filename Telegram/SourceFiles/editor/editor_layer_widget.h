/*
This file is part of Telegram Desktop,
the official desktop application for the Telegram messaging service.

For license and copyright information please follow this link:
https://github.com/telegramdesktop/tdesktop/blob/master/LEGAL
*/
#pragma once

#include "ui/layers/layer_widget.h"
#include "ui/image/image.h"
#include "editor/photo_editor_common.h"
#include "base/unique_qptr.h"
#include "base/timer.h"

enum class ImageRoundRadius;

namespace Window {
class Controller;
class SessionController;
} // namespace Window

namespace Editor {

class LayerWidget final : public Ui::LayerWidget {
public:
	LayerWidget(
		not_null<QWidget*> parent,
		base::unique_qptr<Ui::RpWidget> content);

	void parentResized() override;
	bool closeByOutsideClick() const override;

private:
	bool eventHook(QEvent *e) override;
	void keyPressEvent(QKeyEvent *e) override;
	int resizeGetHeight(int newWidth) override;

	void start();
	void cacheBackground();
	void checkBackgroundStale();
	void checkCacheBackground();
	[[nodiscard]] QImage renderBackground();
	void backgroundReady(QImage background, bool night);
	void startBackgroundFade();

	const base::unique_qptr<Ui::RpWidget> _content;
	QImage _backgroundBack;
	QImage _background;
	QImage _backgroundNext;
	Ui::Animations::Simple _backgroundFade;
	base::Timer _backgroundTimer;
	crl::time _lastAreaChangeTime = 0;
	bool _backgroundCaching = false;
	bool _backgroundNight = false;

};

} // namespace Editor
