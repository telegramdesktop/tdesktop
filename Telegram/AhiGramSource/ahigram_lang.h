/*
This file is part of AhiGram,
a fork of Telegram Desktop with additional features.

For license and copyright information please follow this link:
https://github.com/telegramdesktop/tdesktop/blob/master/LEGAL

// This code is licensed under GPLv3. Attribution to original source is appreciated.
// Author: https://github.com/DyingLay

AhiGram: Localization header
*/

#pragma once

#include <QString>
#include <QMap>
#include "rpl/producer.h"

namespace AhiGram {

enum class Language {
    English,
    Russian,
};

[[nodiscard]] Language GetCurrentLanguage();
[[nodiscard]] QString GetLanguageId();

void InitializeLang();

[[nodiscard]] QString tr(const QString &key);

[[nodiscard]] rpl::producer<QString> trReactive(const QString &key);
[[nodiscard]] rpl::producer<QString> languageChanges();



} // namespace AhiGram