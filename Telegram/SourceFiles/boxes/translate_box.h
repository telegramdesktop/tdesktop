/*
This file is part of Telegram Desktop,
the official desktop application for the Telegram messaging service.

For license and copyright information please follow this link:
https://github.com/telegramdesktop/tdesktop/blob/master/LEGAL
*/
#pragma once

class PeerData;

namespace Ui {
namespace Translate {
[[nodiscard]] std::vector<QLocale> LocalesFromSettings();
} // namespace Translate

class GenericBox;

[[nodiscard]] QString LanguageName(const QLocale &locale);

void TranslateBox(
	not_null<Ui::GenericBox*> box,
	not_null<PeerData*> peer,
	MsgId msgId,
	TextWithEntities text,
	bool hasCopyRestriction);

[[nodiscard]] bool SkipTranslate(TextWithEntities textWithEntities);

void ChooseLanguageBox(
	not_null<Ui::GenericBox*> box,
	Fn<void(std::vector<QLocale>)> callback,
	std::vector<QLocale> toggled);

} // namespace Ui
