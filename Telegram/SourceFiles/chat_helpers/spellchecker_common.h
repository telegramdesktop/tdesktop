/*
This file is part of Telegram Desktop,
the official desktop application for the Telegram messaging service.

For license and copyright information please follow this link:
https://github.com/telegramdesktop/tdesktop/blob/master/LEGAL
*/
#pragma once

#ifndef TDESKTOP_DISABLE_SPELLCHECK

namespace Spellchecker {

struct Dict {
	int id = 0;
	int postId = 0;
	int size = 0;
	QString name;
};

[[nodiscard]] QString DictionariesPath();
[[nodiscard]] QString DictPathByLangId(int langId);
[[nodiscard]] bool IsGoodPartName(const QString &name);
bool UnpackDictionary(const QString &path, int langId);
[[nodiscard]] bool DictionaryExists(int langId);

bool WriteDefaultDictionary();
std::initializer_list<const Dict> Dictionaries();

} // namespace Spellchecker

#endif // !TDESKTOP_DISABLE_SPELLCHECK
