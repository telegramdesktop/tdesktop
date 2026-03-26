/*
This file is part of Telegram Desktop,
the official desktop application for the Telegram messaging service.

For license and copyright information please follow this link:
https://github.com/telegramdesktop/tdesktop/blob/master/LEGAL
*/
#include "lang/translate_url_provider.h"

#include "spellcheck/platform/platform_language.h"

#include <QtCore/QJsonArray>
#include <QtCore/QJsonDocument>
#include <QtCore/QJsonObject>
#include <QtCore/QJsonParseError>
#include <QtCore/QUrl>
#include <QtNetwork/QNetworkAccessManager>
#include <QtNetwork/QNetworkReply>
#include <QtNetwork/QNetworkRequest>

namespace Ui {
namespace {

[[nodiscard]] bool SkipJsonKey(const QString &key) {
	return (key.compare(u"code"_q, Qt::CaseInsensitive) == 0);
}

[[nodiscard]] QString DetectFromLanguage(const QString &text) {
#ifndef TDESKTOP_DISABLE_SPELLCHECK
	const auto result = Platform::Language::Recognize(text);
	return result.known() ? result.twoLetterCode() : u"auto"_q;
#else // TDESKTOP_DISABLE_SPELLCHECK
	return u"auto"_q;
#endif // !TDESKTOP_DISABLE_SPELLCHECK
}

[[nodiscard]] QString JsonValueToText(const QJsonValue &v) {
	switch (v.type()) {
	case QJsonValue::Null: return u"null"_q;
	case QJsonValue::Bool: return v.toBool()
		? u"true"_q
		: u"false"_q;
	case QJsonValue::Double: return QString::number(v.toDouble(), 'g', 15);
	case QJsonValue::String: return v.toString().trimmed();
	case QJsonValue::Array: return QString::fromUtf8(
		QJsonDocument(v.toArray()).toJson(QJsonDocument::Compact));
	case QJsonValue::Object: return QString::fromUtf8(
		QJsonDocument(v.toObject()).toJson(QJsonDocument::Compact));
	case QJsonValue::Undefined: return QString();
	}
	return QString();
}

[[nodiscard]] std::optional<QString> ParseSegmentedArrayResponse(
		const QJsonDocument &parsed) {
	if (!parsed.isArray()) {
		return std::nullopt;
	}
	const auto root = parsed.array();
	if (root.isEmpty() || !root[0].isArray()) {
		return std::nullopt;
	}
	const auto segments = root[0].toArray();
	auto translated = QString();
	for (const auto &segmentValue : segments) {
		if (!segmentValue.isArray()) {
			return std::nullopt;
		}
		const auto segment = segmentValue.toArray();
		if (segment.isEmpty()) {
			return std::nullopt;
		}
		if (!segment[0].isString()) {
			return std::nullopt;
		}
		translated += segment[0].toString();
	}
	if (translated.trimmed().isEmpty()) {
		return std::nullopt;
	}
	return translated;
}

struct JsonLine {
	QString name;
	QString value;
	int length = 0;
};

void CollectJsonLines(
		const QString &name,
		const QJsonValue &value,
		std::vector<JsonLine> &lines) {
	if (value.isObject()) {
		const auto object = value.toObject();
		for (auto i = object.constBegin(); i != object.constEnd(); ++i) {
			if (SkipJsonKey(i.key())) {
				continue;
			}
			CollectJsonLines(
				name.isEmpty() ? i.key() : (name + '.' + i.key()),
				i.value(),
				lines);
		}
		return;
	}
	if (value.isArray()) {
		const auto array = value.toArray();
		for (auto i = 0; i != array.size(); ++i) {
			CollectJsonLines(
				u"%1[%2]"_q.arg(name).arg(i),
				array.at(i),
				lines);
		}
		return;
	}
	const auto text = JsonValueToText(value);
	if (text.isEmpty()) {
		return;
	}
	lines.push_back(JsonLine{
		.name = name,
		.value = text,
		.length = int(text.size()),
	});
}

[[nodiscard]] std::optional<QString> FormatJsonResponse(
		const QByteArray &body) {
	auto error = QJsonParseError();
	const auto parsed = QJsonDocument::fromJson(body, &error);
	if (error.error != QJsonParseError::NoError) {
		return std::nullopt;
	}
	if (const auto parsedArray = ParseSegmentedArrayResponse(parsed)) {
		return parsedArray;
	}
	auto lines = std::vector<JsonLine>();
	if (parsed.isObject()) {
		const auto object = parsed.object();
		for (auto i = object.constBegin(); i != object.constEnd(); ++i) {
			if (SkipJsonKey(i.key())) {
				continue;
			}
			CollectJsonLines(i.key(), i.value(), lines);
		}
	} else if (parsed.isArray()) {
		const auto array = parsed.array();
		for (auto i = 0; i != array.size(); ++i) {
			CollectJsonLines(u"[%1]"_q.arg(i), array.at(i), lines);
		}
	}
	if (lines.empty()) {
		return QString::fromUtf8(body);
	}
	ranges::sort(lines, [](const JsonLine &a, const JsonLine &b) {
		return (a.length != b.length)
			? (a.length > b.length)
			: (a.name < b.name);
	});
	auto result = QString();
	result.reserve(lines.size() * 16);
	for (auto i = 0; i != int(lines.size()); ++i) {
		const auto &line = lines[i];
		if (!line.name.isEmpty()) {
			result += line.name;
			result += '\n';
		}
		result += line.value;
		if (i + 1 != int(lines.size())) {
			result += "\n\n";
		}
	}
	return result;
}

class UrlTranslateProvider final : public TranslateProvider {
public:
	explicit UrlTranslateProvider(QString urlTemplate)
	: _urlTemplate(std::move(urlTemplate)) {
	}

	[[nodiscard]] bool supportsMessageId() const override {
		return false;
	}

	void request(
			TranslateProviderRequest request,
			LanguageId to,
			Fn<void(TranslateProviderResult)> done) override {
		if (request.text.text.isEmpty()) {
			done(TranslateProviderResult{
				.error = TranslateProviderError::Unknown,
			});
			return;
		}
		auto url = _urlTemplate;
		const auto from = DetectFromLanguage(request.text.text);
		const auto toCode = to.twoLetterCode();
		url.replace(
			u"%q"_q,
			QString::fromLatin1(
				QUrl::toPercentEncoding(request.text.text.toHtmlEscaped())));
		url.replace(
			u"%f"_q,
			QString::fromLatin1(QUrl::toPercentEncoding(from)));
		url.replace(
			u"%t"_q,
			QString::fromLatin1(QUrl::toPercentEncoding(toCode)));
		const auto requestUrl = QUrl(url);
		if (!requestUrl.isValid()) {
			done(TranslateProviderResult{
				.error = TranslateProviderError::Unknown,
			});
			return;
		}
		auto networkRequest = QNetworkRequest(requestUrl);
		const auto reply = _network.get(networkRequest);
		QObject::connect(reply, &QNetworkReply::finished, [=] {
			auto result = TranslateProviderResult();
			if (reply->error() != QNetworkReply::NoError) {
				result.error = TranslateProviderError::Unknown;
			} else {
				const auto body = reply->readAll();
				const auto formatted = FormatJsonResponse(body).value_or(
					QString::fromUtf8(body));
				result.text = TextWithEntities{ formatted };
			}
			done(std::move(result));
			reply->deleteLater();
		});
	}

private:
	const QString _urlTemplate;
	QNetworkAccessManager _network;

};

} // namespace

std::unique_ptr<TranslateProvider> CreateUrlTranslateProvider(
		QString urlTemplate) {
	return std::make_unique<UrlTranslateProvider>(std::move(urlTemplate));
}

} // namespace Ui
