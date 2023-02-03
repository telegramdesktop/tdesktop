/*
This file is part of Telegram Desktop,
the official desktop application for the Telegram messaging service.

For license and copyright information please follow this link:
https://github.com/telegramdesktop/tdesktop/blob/master/LEGAL
*/
#pragma once

struct LanguageId;

namespace Ui {

class GenericBox;

[[nodiscard]] QString LanguageNameTranslated(const QString &twoLetterCode);
[[nodiscard]] QString LanguageName(LanguageId id);
[[nodiscard]] QString LanguageNameNative(LanguageId id);

void ChooseLanguageBox(
	not_null<GenericBox*> box,
	rpl::producer<QString> title,
	Fn<void(std::vector<LanguageId>)> callback,
	std::vector<LanguageId> selected,
	bool multiselect);

} // namespace Ui
