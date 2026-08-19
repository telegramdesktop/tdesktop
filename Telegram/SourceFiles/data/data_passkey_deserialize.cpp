/*
This file is part of Telegram Desktop,
the official desktop application for the Telegram messaging service.

For license and copyright information please follow this link:
https://github.com/telegramdesktop/tdesktop/blob/master/LEGAL
*/

#include "data/data_passkey_deserialize.h"

#include <QJsonDocument>
#include <QJsonObject>
#include <QJsonArray>

namespace Data::Passkey {
namespace {

[[nodiscard]] std::string SerializeClientData(
		const QByteArray &challenge,
		const QString &type) {
	auto obj = QJsonObject();
	obj["type"] = type;
	obj["challenge"] = QString::fromUtf8(
		challenge.toBase64(QByteArray::Base64UrlEncoding
			| QByteArray::OmitTrailingEquals));
	obj["origin"] = "https://telegram.org";
	obj["crossOrigin"] = false;
	return QJsonDocument(obj).toJson(QJsonDocument::Compact).toStdString();
}

} // namespace

std::optional<RegisterData> DeserializeRegisterData(
		const QByteArray &jsonData) {
	auto doc = QJsonDocument::fromJson(jsonData);
	if (!doc.isObject()) {
		return std::nullopt;
	}

	auto root = doc.object();
	auto publicKey = root["publicKey"].toObject();
	if (publicKey.isEmpty()) {
		return std::nullopt;
	}

	auto data = RegisterData();

	auto rp = publicKey["rp"].toObject();
	data.rp.id = rp["id"].toString();
	data.rp.name = rp["name"].toString();

	auto user = publicKey["user"].toObject();
	data.user.id = QByteArray::fromBase64(
		user["id"].toString().toUtf8());
	data.user.name = user["name"].toString();
	data.user.displayName = user["displayName"].toString();

	data.challenge = QByteArray::fromBase64(
		publicKey["challenge"].toString().toUtf8(),
		QByteArray::Base64UrlEncoding);

	auto params = publicKey["pubKeyCredParams"].toArray();
	for (const auto &param : params) {
		auto obj = param.toObject();
		CredentialParameter cp;
		cp.type = obj["type"].toString();
		cp.alg = obj["alg"].toInt();
		data.pubKeyCredParams.push_back(cp);
	}

	data.timeout = publicKey["timeout"].toInt(60000);

	return data;
}

std::string SerializeClientDataCreate(const QByteArray &challenge) {
	return SerializeClientData(challenge, "webauthn.create");
}

std::string SerializeClientDataGet(const QByteArray &challenge) {
	return SerializeClientData(challenge, "webauthn.get");
}

std::optional<LoginData> DeserializeLoginData(
		const QByteArray &jsonData) {
	auto doc = QJsonDocument::fromJson(jsonData);
	if (!doc.isObject()) {
		return std::nullopt;
	}

	auto root = doc.object();
	auto publicKey = root["publicKey"].toObject();
	if (publicKey.isEmpty()) {
		return std::nullopt;
	}

	auto data = LoginData();
	data.challenge = QByteArray::fromBase64(
		publicKey["challenge"].toString().toUtf8(),
		QByteArray::Base64UrlEncoding);
	data.rpId = publicKey["rpId"].toString();
	data.timeout = publicKey["timeout"].toInt(60000);
	data.userVerification = publicKey["userVerification"].toString();

	if (publicKey.contains("allowCredentials")) {
		auto allowList = publicKey["allowCredentials"].toArray();
		for (const auto &cred : allowList) {
			auto credObj = cred.toObject();
			Credential credential;
			credential.id = QByteArray::fromBase64(
				credObj["id"].toString().toUtf8(),
				QByteArray::Base64UrlEncoding);
			credential.type = credObj["type"].toString();
			data.allowCredentials.push_back(credential);
		}
	}

	return data;
}

} // namespace Data::Passkey
