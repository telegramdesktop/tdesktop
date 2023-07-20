/*
This file is part of Telegram Desktop,
the official desktop application for the Telegram messaging service.

For license and copyright information please follow this link:
https://github.com/telegramdesktop/tdesktop/blob/master/LEGAL
*/
#pragma once

#include "ui/rp_widget.h"
#include "ui/round_rect.h"

namespace Main {
class Session;
} // namespace Main

namespace Ui {
class FlatLabel;
class ScrollArea;
} // namespace Ui

namespace Media::Stories {

class CaptionFullView final : private Ui::RpWidget {
public:
	CaptionFullView(
		not_null<Ui::RpWidget*> parent,
		not_null<Main::Session*> session,
		const TextWithEntities &text,
		Fn<void()> close);
	~CaptionFullView();

private:
	void paintEvent(QPaintEvent *e) override;
	void resizeEvent(QResizeEvent *e) override;
	void keyPressEvent(QKeyEvent *e) override;
	void mousePressEvent(QMouseEvent *e) override;

	std::unique_ptr<Ui::ScrollArea> _scroll;
	const not_null<Ui::FlatLabel*> _text;
	Fn<void()> _close;
	Ui::RoundRect _background;

};

} // namespace Media::Stories
