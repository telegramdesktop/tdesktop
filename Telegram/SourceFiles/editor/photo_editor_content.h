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

class PhotoEditorContent final : public Ui::RpWidget {
public:
	PhotoEditorContent(
		not_null<Ui::RpWidget*> parent,
		std::shared_ptr<QPixmap> photo,
		PhotoModifications modifications);

	void applyModifications(PhotoModifications modifications);

private:

	rpl::variable<PhotoModifications> _modifications;

	QRect _imageRect;
	QMatrix _imageMatrix;

};

} // namespace Editor
