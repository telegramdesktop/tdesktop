/*
This file is part of Telegram Desktop,
the official desktop application for the Telegram messaging service.

For license and copyright information please follow this link:
https://github.com/telegramdesktop/tdesktop/blob/master/LEGAL
*/
#pragma once

namespace Ui {

class GenericBox;

void ChooseFontBox(
	not_null<GenericBox*> box,
	Fn<QImage()> generatePreviewBg,
	const QString &family,
	Fn<void(QString)> save);

} // namespace Ui
