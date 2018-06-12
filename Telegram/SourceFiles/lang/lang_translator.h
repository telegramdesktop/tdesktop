/*
This file is part of Telegram Desktop,
the official desktop application for the Telegram messaging service.

For license and copyright information please follow this link:
https://github.com/telegramdesktop/tdesktop/blob/master/LEGAL
*/
#pragma once

namespace Lang {

class Translator : public QTranslator {
public:
	QString translate(const char *context, const char *sourceText, const char *disambiguation = 0, int n = -1) const override;

};

} // namespace Lang
