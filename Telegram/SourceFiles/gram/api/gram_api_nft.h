/*
This file is part of Telegram Desktop,
the official desktop application for the Telegram messaging service.

For license and copyright information please follow this link:
https://github.com/telegramdesktop/tdesktop/blob/master/LEGAL
*/
#pragma once

#include "base/basic_types.h"
#include "gram/api/gram_api_request.h"

#include <QtCore/QByteArray>
#include <QtCore/QString>

#include <optional>
#include <vector>

namespace Gram {

enum class NftKind {
	Generic,
	TelegramGift,
	TelegramNumber,
	TelegramUsername,
};

struct NftWebDocument {
	QByteArray url;
	uint64 accessHash = 0;
	QString mimeType;

	friend bool operator==(
		const NftWebDocument &,
		const NftWebDocument &) = default;
};

struct NftItem {
	// Canonical raw addresses, lowercase `workchain:64-hex`.
	QString address;
	QString collection;
	QString realOwner;
	QString index;
	QString contentUri;
	QString domain;
	QString key;
	QString collectionName;
	QString name;
	std::optional<NftWebDocument> image;
	std::optional<NftWebDocument> imageSmall;
	std::optional<NftWebDocument> contentUrl;
	std::optional<NftWebDocument> lottie;
	NftKind kind = NftKind::Generic;
	bool onSale = false;

	friend bool operator==(
		const NftItem &,
		const NftItem &) = default;
};

struct NftPage {
	std::vector<NftItem> list;
	bool hasNext = false;
};

[[nodiscard]] HttpRequest NftItemByAddressRequest(const QString &item);
[[nodiscard]] std::optional<NftPage> ParseNftItems(
	const QByteArray &json,
	int limit);
void ClassifyNftKind(NftItem &item);

} // namespace Gram
