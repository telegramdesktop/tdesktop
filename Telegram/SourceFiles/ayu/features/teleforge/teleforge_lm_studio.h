#pragma once

#include <functional>

#include <QtCore/QString>
#include <QtNetwork/QNetworkAccessManager>

namespace TeleForge {

struct LmStudioRequestOptions {
	QString model;
	double temperature = 0.7;
	int maxTokens = 512;
};

class LmStudioBridge final {
public:
	using SuccessCallback = std::function<void(const QString &)>;
	using ErrorCallback = std::function<void(const QString &)>;

	static LmStudioBridge &instance();

	void requestCompletion(
		const QString &systemPrompt,
		const QString &userPrompt,
		SuccessCallback onSuccess,
		ErrorCallback onError = {},
		const LmStudioRequestOptions &options = {});

private:
	LmStudioBridge() = default;

	QNetworkAccessManager _manager;
};

} // namespace TeleForge
