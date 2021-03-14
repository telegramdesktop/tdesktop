/*
This file is part of Telegram Desktop,
the official desktop application for the Telegram messaging service.

For license and copyright information please follow this link:
https://github.com/telegramdesktop/tdesktop/blob/master/LEGAL
*/
#pragma once

#include "ui/rp_widget.h"

#include "editor/photo_editor_common.h"

namespace Editor {

class Crop;
class Paint;

class PhotoEditorContent final : public Ui::RpWidget {
public:
	PhotoEditorContent(
		not_null<Ui::RpWidget*> parent,
		std::shared_ptr<QPixmap> photo,
		PhotoModifications modifications);

	void applyModifications(PhotoModifications modifications);
	void applyMode(const PhotoEditorMode &mode);
	void save(PhotoModifications &modifications);

private:

	const base::unique_qptr<Paint> _paint;
	const base::unique_qptr<Crop> _crop;
	const std::shared_ptr<QPixmap> _photo;

	rpl::variable<PhotoModifications> _modifications;

	QRect _imageRect;
	QMatrix _imageMatrix;
	PhotoEditorMode _mode;

};

} // namespace Editor
