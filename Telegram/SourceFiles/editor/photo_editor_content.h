/*
This file is part of Telegram Desktop,
the official desktop application for the Telegram messaging service.

For license and copyright information please follow this link:
https://github.com/telegramdesktop/tdesktop/blob/master/LEGAL
*/
#pragma once

#include "ui/rp_widget.h"

#include "editor/photo_editor_common.h"
#include "ui/image/image.h"

namespace Editor {

class Crop;
class Paint;
class UndoController;

class PhotoEditorContent final : public Ui::RpWidget {
public:
	PhotoEditorContent(
		not_null<Ui::RpWidget*> parent,
		std::shared_ptr<Image> photo,
		PhotoModifications modifications,
		std::shared_ptr<UndoController> undoController,
		EditorData data);

	void applyModifications(PhotoModifications modifications);
	void applyMode(const PhotoEditorMode &mode);
	void applyBrush(const Brush &brush);
	void save(PhotoModifications &modifications);

private:

	const QSize _photoSize;
	const base::unique_qptr<Paint> _paint;
	const base::unique_qptr<Crop> _crop;
	const std::shared_ptr<Image> _photo;

	rpl::variable<PhotoModifications> _modifications;

	QRect _imageRect;
	QMatrix _imageMatrix;
	PhotoEditorMode _mode;

};

} // namespace Editor
