/*
This file is part of Telegram Desktop,
the official desktop application for the Telegram messaging service.

For license and copyright information please follow this link:
https://github.com/telegramdesktop/tdesktop/blob/master/LEGAL
*/
#include "ui/webview_helpers.h"

#include "lang/lang_keys.h"

namespace Ui {

[[nodiscard]] QByteArray ComputeStyles(
		const base::flat_map<QByteArray, const style::color*> &colors,
		const base::flat_map<QByteArray, tr::phrase<>> &phrases,
		bool nightTheme) {
	static const auto serialize = [](const style::color *color) {
		const auto qt = (*color)->c;
		if (qt.alpha() == 255) {
			return '#'
				+ QByteArray::number(qt.red(), 16).right(2)
				+ QByteArray::number(qt.green(), 16).right(2)
				+ QByteArray::number(qt.blue(), 16).right(2);
		}
		return "rgba("
			+ QByteArray::number(qt.red()) + ","
			+ QByteArray::number(qt.green()) + ","
			+ QByteArray::number(qt.blue()) + ","
			+ QByteArray::number(qt.alpha() / 255.) + ")";
	};
	static const auto escape = [](tr::phrase<> phrase) {
		const auto text = phrase(tr::now);

		auto result = QByteArray();
		for (auto i = 0; i != text.size(); ++i) {
			uint ucs4 = text[i].unicode();
			if (QChar::isHighSurrogate(ucs4) && i + 1 != text.size()) {
				ushort low = text[i + 1].unicode();
				if (QChar::isLowSurrogate(low)) {
					ucs4 = QChar::surrogateToUcs4(ucs4, low);
					++i;
				}
			}
			if (ucs4 == '\'' || ucs4 == '\"' || ucs4 == '\\') {
				result.append('\\').append(char(ucs4));
			} else if (ucs4 < 32 || ucs4 > 127) {
				result.append('\\' + QByteArray::number(ucs4, 16) + ' ');
			} else {
				result.append(char(ucs4));
			}
		}
		return result;
	};
	auto result = QByteArray();
	for (const auto &[name, phrase] : phrases) {
		result += "--td-lng-"_q + name + ":'"_q + escape(phrase) + "'; "_q;
	}
	for (const auto &[name, color] : colors) {
		result += "--td-"_q + name + ':' + serialize(color) + ';';
	}
	result += "--td-night:"_q + (nightTheme ? "1" : "0") + ';';
	return result;
}

QByteArray EscapeForAttribute(QByteArray value) {
	return value
		.replace('&', "&amp;")
		.replace('"', "&quot;")
		.replace('\'', "&#039;")
		.replace('<', "&lt;")
		.replace('>', "&gt;");
}

QByteArray EscapeForScriptString(QByteArray value) {
	return value
		.replace('\\', "\\\\")
		.replace('"', "\\\"")
		.replace('\'', "\\\'");
}

} // namespace Ui
