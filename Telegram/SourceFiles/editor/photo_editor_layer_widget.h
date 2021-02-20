/*
This file is part of Telegram Desktop,
the official desktop application for the Telegram messaging service.

For license and copyright information please follow this link:
https://github.com/telegramdesktop/tdesktop/blob/master/LEGAL
*/
#pragma once

#include "ui/layers/layer_widget.h"

#include "base/unique_qptr.h"
#include "editor/photo_editor_common.h"
#include "ui/image/image.h"

namespace Window {
class Controller;
} // namespace Window

namespace Editor {

class PhotoEditor;

class LayerWidget : public Ui::LayerWidget {
public:
	LayerWidget(
		not_null<Ui::RpWidget*> parent,
		not_null<Window::Controller*> window,
		std::shared_ptr<Image> photo,
		PhotoModifications modifications,
		Fn<void(PhotoModifications)> &&doneCallback);

	void parentResized() override;
	bool closeByOutsideClick() const override;

protected:
	int resizeGetHeight(int newWidth) override;

private:
	const base::unique_qptr<PhotoEditor> _content;

};

} // namespace Editor
