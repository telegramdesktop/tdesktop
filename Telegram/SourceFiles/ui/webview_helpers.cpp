/*
This file is part of Telegram Desktop,
the official desktop application for the Telegram messaging service.

For license and copyright information please follow this link:
https://github.com/telegramdesktop/tdesktop/blob/master/LEGAL
*/
#include "ui/webview_helpers.h"

#include "lang/lang_keys.h"

namespace Ui {
namespace {

[[nodiscard]] QByteArray Serialize(const QColor &qt) {
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
}

} // namespace

QByteArray ComputeStyles(
		const base::flat_map<QByteArray, const style::color*> &colors,
		const base::flat_map<QByteArray, tr::phrase<>> &phrases,
		int zoom,
		bool nightTheme) {
	static const auto serialize = [](const style::color *color) {
		return Serialize((*color)->c);
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
	result += "--td-zoom-percentage:"_q
		+ (QString::number(zoom).toUtf8() + '%')
		+ ';';
	return result;
}

QByteArray ComputeSemiTransparentOverStyle(
		const QByteArray &name,
		const style::color &over,
		const style::color &bg) {
	const auto result = [&](const QColor &c) {
		return "--td-"_q + name + ':' + Serialize(c) + ';';
	};
	if (over->c.alpha() < 255) {
		return result(over->c);
	}
	// The most transparent color that will still give the same result.
	const auto r0 = bg->c.red();
	const auto g0 = bg->c.green();
	const auto b0 = bg->c.blue();
	const auto r1 = over->c.red();
	const auto g1 = over->c.green();
	const auto b1 = over->c.blue();
	const auto mina = [](int c0, int c1) {
		return (c0 == c1)
			? 0
			: (c0 > c1)
			? (((c0 - c1) * 255) / c0)
			: (((c1 - c0) * 255) / (255 - c0));
	};
	const auto rmina = mina(r0, r1);
	const auto gmina = mina(g0, g1);
	const auto bmina = mina(b0, b1);
	const auto a = std::max({ rmina, gmina, bmina });
	const auto r = (r1 * 255 - r0 * (255 - a)) / a;
	const auto g = (g1 * 255 - g0 * (255 - a)) / a;
	const auto b = (b1 * 255 - b0 * (255 - a)) / a;
	return result(QColor(r, g, b, a));
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
