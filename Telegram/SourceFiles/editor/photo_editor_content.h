/*
This file is part of Telegram Desktop,
the official desktop application for the Telegram messaging service.

For license and copyright information please follow this link:
https://github.com/telegramdesktop/tdesktop/blob/master/LEGAL
*/
#pragma once

#include "ui/rp_widget.h"

namespace Editor {

class PhotoEditorContent final : public Ui::RpWidget {
public:
	PhotoEditorContent(
		not_null<Ui::RpWidget*> parent,
		std::shared_ptr<QPixmap> photo);

private:

	QRect _imageRect;

};

} // namespace Editor
