#include "ayu/features/teleforge/teleforge_lm_studio.h"

#include <QtCore/QJsonArray>
#include <QtCore/QJsonDocument>
#include <QtCore/QJsonObject>
#include <QtNetwork/QNetworkReply>
#include <QtNetwork/QNetworkRequest>

namespace TeleForge {

LmStudioBridge &LmStudioBridge::instance() {
	static auto bridge = LmStudioBridge();
	return bridge;
}

void LmStudioBridge::requestCompletion(
		const QString &systemPrompt,
		const QString &userPrompt,
		SuccessCallback onSuccess,
		ErrorCallback onError,
		const LmStudioRequestOptions &options) {
	auto payload = QJsonObject{
		{ "model", options.model.isEmpty() ? QString("local-model") : options.model },
		{ "temperature", options.temperature },
		{ "max_tokens", options.maxTokens },
		{ "messages", QJsonArray{
			QJsonObject{
				{ "role", "system" },
				{ "content", systemPrompt },
			},
			QJsonObject{
				{ "role", "user" },
				{ "content", userPrompt },
			},
		} },
	};

	auto request = QNetworkRequest(QUrl("http://127.0.0.1:1234/v1/chat/completions"));
	request.setHeader(QNetworkRequest::ContentTypeHeader, "application/json");
	const auto reply = _manager.post(request, QJsonDocument(payload).toJson(QJsonDocument::Compact));

	QObject::connect(reply, &QNetworkReply::finished, [reply, onSuccess = std::move(onSuccess), onError = std::move(onError)] {
		if (reply->error() != QNetworkReply::NoError) {
			if (onError) {
				onError(reply->errorString());
			}
			reply->deleteLater();
			return;
		}

		const auto document = QJsonDocument::fromJson(reply->readAll());
		const auto root = document.object();
		const auto choices = root.value("choices").toArray();
		if (choices.isEmpty()) {
			if (onError) {
				onError("LM Studio returned an empty choices array.");
			}
			reply->deleteLater();
			return;
		}

		const auto content = choices[0].toObject()
			.value("message").toObject()
			.value("content").toString();
		if (content.isEmpty()) {
			if (onError) {
				onError("LM Studio returned an empty content field.");
			}
			reply->deleteLater();
			return;
		}

		if (onSuccess) {
			onSuccess(content);
		}
		reply->deleteLater();
	});
}

} // namespace TeleForge
