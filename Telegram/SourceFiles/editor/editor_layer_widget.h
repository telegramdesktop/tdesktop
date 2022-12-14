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

protected:
	void keyPressEvent(QKeyEvent *e) override;
	int resizeGetHeight(int newWidth) override;

private:
	const base::unique_qptr<Ui::RpWidget> _content;

};

} // namespace Editor
