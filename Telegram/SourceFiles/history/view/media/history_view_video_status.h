/*
This file is part of Telegram Desktop,
the official desktop application for the Telegram messaging service.

For license and copyright information please follow this link:
https://github.com/telegramdesktop/tdesktop/blob/master/LEGAL
*/
#pragma once

#include "ui/chat/message_bubble.h"

class Painter;

namespace Ui {
class RadialAnimation;
struct ChatPaintContext;
} // namespace Ui

namespace HistoryView {

struct VideoCornerStatus {
	QString text;
	QString downloadSize;
	QPoint position;
	int outerWidth = 0;
	Ui::RadialAnimation *radial = nullptr;
	bool download = false;
	bool loading = false;
	bool mute = false;
};

void PaintVideoCornerStatus(
	Painter &p,
	const Ui::ChatPaintContext &context,
	const VideoCornerStatus &status);

[[nodiscard]] QRect VideoCornerDownloadRect(QPoint position);

void PaintVideoTimestampMark(
	Painter &p,
	QRect rthumb,
	std::optional<Ui::BubbleRounding> rounding,
	crl::time position,
	crl::time duration);

} // namespace HistoryView
