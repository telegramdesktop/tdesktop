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

namespace Ui {
struct PreparedFile;
} // namespace Ui

namespace Window {
class Controller;
class SessionController;
} // namespace Window

namespace Editor {

void OpenWithPreparedFile(
	not_null<Ui::RpWidget*> parent,
	not_null<Window::SessionController*> controller,
	not_null<Ui::PreparedFile*> file,
	int previewWidth,
	Fn<void()> &&doneCallback);

void PrepareProfilePhoto(
	not_null<Ui::RpWidget*> parent,
	not_null<Window::Controller*> controller,
	Fn<void(QImage &&image)> &&doneCallback);

class PhotoEditor;

class LayerWidget : public Ui::LayerWidget {
public:
	LayerWidget(
		not_null<Ui::RpWidget*> parent,
		not_null<Window::Controller*> window,
		std::shared_ptr<Image> photo,
		PhotoModifications modifications,
		Fn<void(PhotoModifications)> &&doneCallback,
		EditorData data = EditorData());

	void parentResized() override;
	bool closeByOutsideClick() const override;

protected:
	void keyPressEvent(QKeyEvent *e) override;
	int resizeGetHeight(int newWidth) override;

private:
	const base::unique_qptr<PhotoEditor> _content;

};

} // namespace Editor
