/*
This file is part of Telegram Desktop,
the official desktop application for the Telegram messaging service.

For license and copyright information please follow this link:
https://github.com/telegramdesktop/tdesktop/blob/master/LEGAL
*/
#pragma once

class QWidget;

namespace style {
struct Toast;
} // namespace style

namespace Ui {

void AddLottieToToast(
	not_null<QWidget*> widget,
	const style::Toast &st,
	QSize iconSize,
	const QString &name);

} // namespace Ui
