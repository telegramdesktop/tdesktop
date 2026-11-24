/*
This file is part of Telegram Desktop,
the official desktop application for the Telegram messaging service.

For license and copyright information please follow this link:
https://github.com/telegramdesktop/tdesktop/blob/master/LEGAL
*/
#pragma once

#include "ui/effects/animations.h"

namespace Dialogs::Stories {

class UploadCancelOverlay final {
public:
	explicit UploadCancelOverlay(Fn<void()> update);

	void setHovered(uint64 id);
	[[nodiscard]] uint64 hovered() const;
	void paint(QPainter &p, uint64 id, const QRectF &rect);

private:
	Fn<void()> _update;
	Ui::Animations::Simple _fade;
	uint64 _hovered = 0;
	uint64 _painted = 0;

};

} // namespace Dialogs::Stories
