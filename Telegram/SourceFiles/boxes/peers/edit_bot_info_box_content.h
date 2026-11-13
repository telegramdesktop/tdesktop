/*
This file is part of Telegram Desktop,
the official desktop application for the Telegram messaging service.

For license and copyright information please follow this link:
https://github.com/telegramdesktop/tdesktop/blob/master/LEGAL
*/
#pragma once

#include "base/object_ptr.h"

namespace Data {
class BotInfoLocalization;
} // namespace Data

namespace Ui {
class GenericBox;
class InputField;
class RpWidget;
} // namespace Ui

struct EditBotInfoBoxArgs {
	not_null<Data::BotInfoLocalization*> localization;
	QString username;
	Fn<object_ptr<Ui::RpWidget>(
		not_null<QWidget*> parent,
		rpl::producer<QString> text)> introPreview;
	Fn<void(not_null<Ui::InputField*> field)> addEmojiButton;
	Fn<void(
		not_null<Ui::InputField*> field,
		Fn<QString()> source)> addAiButton;
	Fn<void(
		Fn<void(QByteArray content, QString path)> done)> chooseImportFile;
	Fn<void(QString name, Fn<void(QString path)> done)> chooseExportFile;
};

void EditBotInfoBoxContent(
	not_null<Ui::GenericBox*> box,
	EditBotInfoBoxArgs &&args);
