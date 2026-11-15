/*
This file is part of Telegram Desktop,
the official desktop application for the Telegram messaging service.

For license and copyright information please follow this link:
https://github.com/telegramdesktop/tdesktop/blob/master/LEGAL
*/
#include "ui/webview_helpers.h"

namespace Ui {
namespace {

[[nodiscard]] QByteArray Serialize(const QColor &qt) {
	if (qt.alpha() == 255) {
		return '#'
			+ QByteArray::number(qt.red(), 16).rightJustified(2, '0')
			+ QByteArray::number(qt.green(), 16).rightJustified(2, '0')
			+ QByteArray::number(qt.blue(), 16).rightJustified(2, '0');
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
		int zoom,
		bool nightTheme) {
	static const auto serialize = [](const style::color *color) {
		return Serialize((*color)->c);
	};
	auto result = QByteArray();
	for (const auto &[name, color] : colors) {
		result += "--td-"_q + name + ':' + serialize(color) + ';';
	}
	result += "--td-night:"_q + (nightTheme ? "1" : "0") + ';';
	result += "--td-zoom-percentage:"_q
		+ (QString::number(zoom).toUtf8() + '%')
		+ ';';
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
