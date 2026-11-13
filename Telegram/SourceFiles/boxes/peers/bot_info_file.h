/*
This file is part of Telegram Desktop,
the official desktop application for the Telegram messaging service.

For license and copyright information please follow this link:
https://github.com/telegramdesktop/tdesktop/blob/master/LEGAL
*/
#pragma once

#include "data/data_bot_info_draft.h"

enum class BotInfoFileError : uchar {
	None,
	TooLarge,
	Syntax,
	Shape,
	UnknownKey,
	NotString,
	BadCode,
	Duplicate,
};

struct BotInfoFileLanguage {
	QString code;
	Data::BotInfoChange texts;
};

struct BotInfoFile {
	QString bot;
	std::vector<BotInfoFileLanguage> languages;
	BotInfoFileError error = BotInfoFileError::None;
	QString where;
	QString details;
	int line = 0;
	int column = 0;
};

[[nodiscard]] BotInfoFile ParseBotInfoFile(const QByteArray &content);
[[nodiscard]] QByteArray SerializeBotInfoFile(
	const QString &bot,
	const Data::BotInfoDraft &draft);
