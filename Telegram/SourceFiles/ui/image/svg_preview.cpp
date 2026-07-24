/*
This file is part of Telegram Desktop,
the official desktop application for the Telegram messaging service.

For license and copyright information please follow this link:
https://github.com/telegramdesktop/tdesktop/blob/master/LEGAL
*/
#include "ui/image/svg_preview.h"

#include "base/debug_log.h"

#include <QtCore/QXmlStreamReader>
#include <QtGui/QPainter>
#include <QtSvg/QSvgRenderer>

namespace Ui {
namespace {

constexpr auto kMaxSvgSize = 1 * 1024 * 1024;
constexpr auto kMaxDefaultRenderSize = 4096;

[[nodiscard]] bool IsName(const QString &value, const char *name) {
	return (value.compare(QLatin1String(name), Qt::CaseInsensitive) == 0);
}

[[nodiscard]] bool IsDisallowedElement(const QString &name) {
	return IsName(name, "script")
		|| IsName(name, "foreignObject")
		|| IsName(name, "iframe")
		|| IsName(name, "object")
		|| IsName(name, "embed")
		|| IsName(name, "audio")
		|| IsName(name, "video")
		|| IsName(name, "animate")
		|| IsName(name, "animateColor")
		|| IsName(name, "animateMotion")
		|| IsName(name, "animateTransform")
		|| IsName(name, "set");
}

[[nodiscard]] QString TrimAndUnquote(QString value) {
	value = value.trimmed();
	if (value.size() < 2) {
		return value;
	}
	const auto first = value.at(0);
	const auto last = value.at(value.size() - 1);
	if (((first == QLatin1Char('"')) && (last == QLatin1Char('"')))
		|| ((first == QLatin1Char('\'')) && (last == QLatin1Char('\'')))) {
		value = value.mid(1, value.size() - 2).trimmed();
	}
	return value;
}

[[nodiscard]] bool IsAllowedReference(QString value) {
	value = TrimAndUnquote(value);
	return !value.isEmpty() && value.startsWith(QLatin1Char('#'));
}

[[nodiscard]] bool HasDisallowedCss(const QString &text) {
	if (text.contains(QLatin1String("@import"), Qt::CaseInsensitive)) {
		return true;
	}
	if (text.contains(QLatin1String("@keyframes"), Qt::CaseInsensitive)
		|| text.contains(QLatin1String("animation"), Qt::CaseInsensitive)) {
		return true;
	}
	auto index = 0;
	while (true) {
		index = text.indexOf(
			QLatin1String("url("),
			index,
			Qt::CaseInsensitive);
		if (index < 0) {
			return false;
		}
		const auto start = index + 4;
		const auto end = text.indexOf(QLatin1Char(')'), start);
		if (end < 0) {
			return true;
		}
		if (!IsAllowedReference(text.mid(start, end - start))) {
			return true;
		}
		index = end + 1;
	}
}

[[nodiscard]] bool AttributesAreSafe(
		const QXmlStreamAttributes &attributes) {
	for (const auto &attribute : attributes) {
		const auto name = attribute.name().toString();
		const auto value = attribute.value().toString();
		if (name.startsWith(QLatin1String("on"), Qt::CaseInsensitive)) {
			return false;
		}
		if (IsName(name, "href") && !IsAllowedReference(value)) {
			return false;
		}
		if (IsName(name, "style") && HasDisallowedCss(value)) {
			return false;
		}
	}
	return true;
}

[[nodiscard]] QByteArray SanitizeSvg(const QByteArray &bytes) {
	auto reader = QXmlStreamReader(bytes);
	auto hasRoot = false;
	auto inStyle = 0;
	while (!reader.atEnd()) {
		switch (reader.readNext()) {
		case QXmlStreamReader::StartElement: {
			const auto name = reader.name().toString();
			if (!hasRoot) {
				hasRoot = true;
				if (!IsName(name, "svg")) {
					LOG(("Svg Sanitize: Invalid root element."));
					return {};
				}
			}
			if (IsDisallowedElement(name)
				|| !AttributesAreSafe(reader.attributes())) {
				LOG(("Svg Sanitize: Disallowed element or attribute."));
				return {};
			}
			if (IsName(name, "style")) {
				++inStyle;
			}
		} break;
		case QXmlStreamReader::EndElement: {
			if ((inStyle > 0) && IsName(reader.name().toString(), "style")) {
				--inStyle;
			}
		} break;
		case QXmlStreamReader::Characters: {
			if ((inStyle > 0) && HasDisallowedCss(reader.text().toString())) {
				LOG(("Svg Sanitize: Disallowed CSS."));
				return {};
			}
		} break;
		case QXmlStreamReader::DTD:
		case QXmlStreamReader::EntityReference:
		case QXmlStreamReader::ProcessingInstruction:
		case QXmlStreamReader::Invalid:
			LOG(("Svg Sanitize: Disallowed XML token."));
			return {};
		default: break;
		}
	}
	if (reader.hasError() || !hasRoot) {
		LOG(("Svg Sanitize: Parse error."));
		return {};
	}
	return bytes;
}

} // namespace

int SvgPreviewBytesLimit() {
	return kMaxSvgSize;
}

QImage RenderSvgPreview(const QByteArray &bytes, QSize maxSize) {
	if (bytes.isEmpty()) {
		return {};
	}
	if (bytes.size() > kMaxSvgSize) {
		LOG(("Svg Error: File too large (%1 bytes).").arg(bytes.size()));
		return {};
	}
	const auto sanitized = SanitizeSvg(bytes);
	if (sanitized.isEmpty()) {
		return {};
	}
	auto renderer = QSvgRenderer();
	if (!renderer.load(sanitized) || !renderer.isValid()) {
		LOG(("Svg Error: Invalid data."));
		return {};
	}
	auto size = renderer.defaultSize();
	if (!maxSize.isEmpty()) {
		size = size.scaled(maxSize, Qt::KeepAspectRatio);
	} else if ((size.width() > kMaxDefaultRenderSize)
		|| (size.height() > kMaxDefaultRenderSize)) {
		size = size.scaled(
			kMaxDefaultRenderSize,
			kMaxDefaultRenderSize,
			Qt::KeepAspectRatio);
	}
	if ((size.width() <= 0) || (size.height() <= 0)) {
		LOG(("Svg Error: Bad size %1x%2."
			).arg(renderer.defaultSize().width()
			).arg(renderer.defaultSize().height()));
		return {};
	}
	auto rendered = QImage(size, QImage::Format_ARGB32_Premultiplied);
	rendered.fill(Qt::transparent);
	{
		QPainter p(&rendered);
		renderer.render(&p, QRect(QPoint(), size));
	}
	return rendered;
}

} // namespace Ui
