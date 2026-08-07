/*
This file is part of Telegram Desktop,
the official desktop application for the Telegram messaging service.

For license and copyright information please follow this link:
https://github.com/telegramdesktop/tdesktop/blob/master/LEGAL
*/
#include "gram/api/gram_api_nft.h"

#include <QtCore/QJsonArray>
#include <QtCore/QJsonDocument>
#include <QtCore/QJsonObject>
#include <QtCore/QJsonValue>
#include <QtCore/QStringView>

#include <algorithm>
#include <array>

namespace Gram {
namespace {

struct FragmentCollection {
	const char16_t *raw = nullptr;
	const char16_t *segment = nullptr;
	NftKind kind = NftKind::Generic;
};

constexpr auto kFragmentCollections = std::array{
	FragmentCollection{
		u"0:4C71F300665314AF55B75FC91D130DDF"
		u"24C5006961F8F9772613947945F14863",
		u"gift",
		NftKind::TelegramGift,
	},
	FragmentCollection{
		u"0:0E41DC1DC3C9067ED24248580E12B335"
		u"9818D83DEE0304FABCF80845EAFAFDB2",
		u"number",
		NftKind::TelegramNumber,
	},
	FragmentCollection{
		u"0:80D78A35F955A14B679FAA887FF4CD5B"
		u"FC0F43B4A4EEA2A7E6927F3701B273C2",
		u"username",
		NftKind::TelegramUsername,
	},
};

[[nodiscard]] std::optional<Address> ParseRawAddress(const QJsonValue &value) {
	if (!value.isString()) {
		return std::nullopt;
	}
	const auto parsed = ParseAddress(value.toString());
	if (!parsed) {
		return std::nullopt;
	}
	return parsed->address;
}

[[nodiscard]] const FragmentCollection *FragmentEntry(
		const Address &collection) {
	const auto raw = FormatRaw(collection).toUpper();
	for (const auto &entry : kFragmentCollections) {
		if (raw == QStringView(entry.raw)) {
			return &entry;
		}
	}
	return nullptr;
}

[[nodiscard]] QString FragmentSlug(
		const QString &uri,
		QStringView segment) {
	const auto prefix = u"https://nft.fragment.com/"_q
		+ segment.toString()
		+ u"/"_q;
	if (!uri.startsWith(prefix)) {
		return QString();
	}
	auto slug = uri.mid(prefix.size());
	const auto extension = u".json"_q;
	if (slug.endsWith(extension)) {
		slug.chop(extension.size());
	}
	if (slug.isEmpty()) {
		return QString();
	}
	for (const auto ch : slug) {
		const auto allowed = (ch == QChar('.'))
			|| (ch == QChar('_'))
			|| (ch == QChar('-'))
			|| (ch.isLetterOrNumber() && ch.unicode() < 128);
		if (!allowed) {
			return QString();
		}
	}
	return slug;
}

[[nodiscard]] std::optional<NftItem> ParseNftItem(const QJsonObject &object) {
	const auto address = ParseRawAddress(object.value(u"address"_q));
	if (!address) {
		return std::nullopt;
	}
	auto result = NftItem();
	result.address = *address;
	const auto index = object.value(u"index"_q);
	if (index.isString()) {
		result.index = index.toString();
	}
	if (const auto collection = ParseRawAddress(
			object.value(u"collection_address"_q))) {
		result.collection = *collection;
	}
	if (const auto owner = ParseRawAddress(object.value(u"real_owner"_q))) {
		result.realOwner = *owner;
	}
	const auto content = object.value(u"content"_q).toObject();
	const auto uri = content.value(u"uri"_q);
	if (uri.isString()) {
		result.contentUri = uri.toString();
	}
	const auto domain = content.value(u"domain"_q);
	if (domain.isString()) {
		result.domain = domain.toString();
	}
	const auto collectionContent = object.value(u"collection"_q)
		.toObject()
		.value(u"collection_content"_q)
		.toObject()
		.value(u"uri"_q);
	if (collectionContent.isString()) {
		result.collectionContentUri = collectionContent.toString();
	}
	result.contentUriHttps = result.contentUri.startsWith(u"https://"_q);
	result.collectionContentUriHttps
		= result.collectionContentUri.startsWith(u"https://"_q);
	result.onSale = object.value(u"on_sale"_q).toBool();
	if (const auto entry = FragmentEntry(result.collection)) {
		result.kind = entry->kind;
		result.key = FragmentSlug(result.contentUri, entry->segment);
	}
	return result;
}

} // namespace

HttpRequest NftItemsByOwnerRequest(
		const QString &owner,
		int limit,
		int offset) {
	auto result = HttpRequest();
	result.post = false;
	result.endpoint = u"/api/v3/nft/items"_q;
	result.query = u"owner_address="_q
		+ ApiDetails::PercentEncoded(owner)
		+ u"&limit="_q
		+ QString::number(std::clamp(limit, 0, 100))
		+ u"&offset="_q
		+ QString::number(std::max(offset, 0));
	return result;
}

HttpRequest NftItemByAddressRequest(const Address &item) {
	auto result = HttpRequest();
	result.post = false;
	result.endpoint = u"/api/v3/nft/items"_q;
	result.query = u"address="_q
		+ ApiDetails::PercentEncoded(FormatRaw(item).toUpper());
	return result;
}

std::optional<NftPage> ParseNftItems(const QByteArray &json, int limit) {
	if (ParseApiError(json)) {
		return std::nullopt;
	}
	const auto document = QJsonDocument::fromJson(json);
	if (document.isNull() || !document.isObject()) {
		return std::nullopt;
	}
	const auto itemsValue = document.object().value(u"nft_items"_q);
	if (!itemsValue.isArray()) {
		return std::nullopt;
	}
	const auto items = itemsValue.toArray();

	auto page = NftPage();
	for (const auto &value : items) {
		if (!value.isObject()) {
			return std::nullopt;
		}
		auto item = ParseNftItem(value.toObject());
		if (!item) {
			return std::nullopt;
		}
		page.list.push_back(std::move(*item));
	}
	page.hasNext = int(items.size()) >= limit;
	return page;
}

std::optional<NftDescriptor> ParseNftDescriptor(const QByteArray &json) {
	const auto document = QJsonDocument::fromJson(json);
	if (document.isNull() || !document.isObject()) {
		return std::nullopt;
	}
	const auto object = document.object();
	const auto name = object.value(u"name"_q);
	if (!name.isString() || name.toString().isEmpty()) {
		return std::nullopt;
	}
	auto result = NftDescriptor();
	result.name = name.toString();
	const auto image = object.value(u"image"_q);
	if (image.isString()) {
		result.imageUrl = image.toString();
	}
	return result;
}

std::optional<NftDescriptor> ParseNftCollectionDescriptor(
		const QByteArray &json) {
	return ParseNftDescriptor(json);
}

} // namespace Gram
