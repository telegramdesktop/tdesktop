/*
This file is part of Telegram Desktop,
the official desktop application for the Telegram messaging service.

For license and copyright information please follow this link:
https://github.com/telegramdesktop/tdesktop/blob/master/LEGAL
*/
#pragma once

#include "base/object_ptr.h"

namespace Data {
class BotInfoDraft;
} // namespace Data

namespace Ui {
class AbstractButton;
class PopupMenu;
} // namespace Ui

struct BotInfoLanguageMenuArgs {
	not_null<const Data::BotInfoDraft*> draft;
	QString current;
	rpl::producer<QString> loading;
	Fn<void(QString)> choose;
	Fn<void()> add;
	Fn<void()> translate;
};

[[nodiscard]] QString BotInfoLanguageName(const QString &langCode);
[[nodiscard]] object_ptr<Ui::AbstractButton> CreateBotInfoLanguageButton(
	rpl::producer<QString> text);
void FillBotInfoLanguageMenu(
	not_null<Ui::PopupMenu*> menu,
	BotInfoLanguageMenuArgs &&args);
