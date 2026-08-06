/*
This file is part of Telegram Desktop,
the official desktop application for the Telegram messaging service.

For license and copyright information please follow this link:
https://github.com/telegramdesktop/tdesktop/blob/master/LEGAL
*/
#pragma once

#include "base/basic_types.h"
#include "gram/api/gram_api_request.h"
#include "gram/ton/gram_address.h"

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

struct NftItem {
	Address address;
	Address collection;
	Address realOwner;
	QString index;
	QString contentUri;
	QString domain;
	QString key;
	NftKind kind = NftKind::Generic;
	bool contentUriHttps = false;
	bool onSale = false;
};

struct NftPage {
	std::vector<NftItem> list;
	bool hasNext = false;
};

struct NftDescriptor {
	QString name;
	QString imageUrl;
};

[[nodiscard]] HttpRequest NftItemsByOwnerRequest(
	const QString &owner,
	int limit,
	int offset);
[[nodiscard]] std::optional<NftPage> ParseNftItems(
	const QByteArray &json,
	int limit);
[[nodiscard]] std::optional<NftDescriptor> ParseNftDescriptor(
	const QByteArray &json);
[[nodiscard]] std::optional<NftDescriptor> ParseNftCollectionDescriptor(
	const QByteArray &json);

} // namespace Gram
