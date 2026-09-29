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

#include <array>

namespace Gram {
namespace {

constexpr auto kRawHashChars = 64;

struct FragmentCollection {
	const char16_t *raw = nullptr;
	const char16_t *segment = nullptr;
	NftKind kind = NftKind::Generic;
};

constexpr auto kFragmentCollections = std::array{
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

[[nodiscard]] bool IsHash64(const QString &text) {
	if (text.size() != kRawHashChars) {
		return false;
	}
	for (const auto ch : text) {
		const auto code = ch.unicode();
		const auto hex = (code >= '0' && code <= '9')
			|| (code >= 'a' && code <= 'f')
			|| (code >= 'A' && code <= 'F');
		if (!hex) {
			return false;
		}
	}
	return true;
}

[[nodiscard]] QString CanonicalRawAddress(const QJsonValue &value) {
	if (!value.isString()) {
		return QString();
	}
	const auto text = value.toString();
	const auto colon = text.indexOf(QChar(':'));
	if (colon < 0) {
		return QString();
	}
	const auto hash = text.mid(colon + 1);
	auto ok = false;
	const auto workchain = text.left(colon).toInt(&ok, 10);
	if (!ok || !IsHash64(hash)) {
		return QString();
	}
	return QString::number(workchain) + u":"_q + hash.toLower();
}

[[nodiscard]] const FragmentCollection *FragmentEntry(
		const QString &collection) {
	const auto raw = collection.toUpper();
	for (const auto &entry : kFragmentCollections) {
		if (raw == QStringView(entry.raw)) {
			return &entry;
		}
	}
	return nullptr;
}

[[nodiscard]] bool IsSlugChar(QChar ch) {
	return (ch == QChar('_'))
		|| (ch == QChar('-'))
		|| (ch.isLetterOrNumber() && ch.unicode() < 128);
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
		if (ch != QChar('.') && !IsSlugChar(ch)) {
			return QString();
		}
	}
	return slug;
}

[[nodiscard]] QString FragmentMediaSlug(
		const QByteArray &url,
		QStringView segment) {
	const auto prefix = u"https://nft.fragment.com/"_q
		+ segment.toString()
		+ u"/"_q;
	const auto text = QString::fromUtf8(url);
	if (!text.startsWith(prefix)) {
		return QString();
	}
	auto slug = text.mid(prefix.size());
	const auto dot = slug.indexOf(QChar('.'));
	if (dot >= 0) {
		slug.truncate(dot);
	}
	if (slug.isEmpty()) {
		return QString();
	}
	for (const auto ch : slug) {
		if (!IsSlugChar(ch)) {
			return QString();
		}
	}
	return slug;
}

[[nodiscard]] QString MediaSlug(const NftItem &item, QStringView segment) {
	const auto documents = {
		&item.image,
		&item.lottie,
		&item.imageSmall,
		&item.contentUrl,
	};
	for (const auto document : documents) {
		if (*document) {
			auto slug = FragmentMediaSlug((*document)->url, segment);
			if (!slug.isEmpty()) {
				return slug;
			}
		}
	}
	return QString();
}

[[nodiscard]] QString KeyFromName(const QString &name, NftKind kind) {
	if (kind == NftKind::TelegramUsername) {
		const auto key = name.startsWith(QChar('@')) ? name.mid(1) : name;
		if (key.isEmpty()) {
			return QString();
		}
		for (const auto ch : key) {
			if (ch != QChar('_')
				&& !(ch.isLetterOrNumber() && ch.unicode() < 128)) {
				return QString();
			}
		}
		return key;
	} else if (kind == NftKind::TelegramNumber) {
		if (!name.startsWith(QChar('+'))) {
			return QString();
		}
		auto result = QString();
		for (const auto ch : QStringView(name).mid(1)) {
			if (ch >= QChar('0') && ch <= QChar('9')) {
				result.append(ch);
			} else if (ch != QChar(' ')) {
				return QString();
			}
		}
		return result;
	}
	return QString();
}

[[nodiscard]] std::optional<NftItem> ParseNftItem(const QJsonObject &object) {
	const auto address = CanonicalRawAddress(object.value(u"address"_q));
	if (address.isEmpty()) {
		return std::nullopt;
	}
	auto result = NftItem();
	result.address = address;
	const auto index = object.value(u"index"_q);
	if (index.isString()) {
		result.index = index.toString();
	}
	result.collection = CanonicalRawAddress(
		object.value(u"collection_address"_q));
	result.realOwner = CanonicalRawAddress(object.value(u"real_owner"_q));
	const auto content = object.value(u"content"_q).toObject();
	const auto uri = content.value(u"uri"_q);
	if (uri.isString()) {
		result.contentUri = uri.toString();
	}
	const auto domain = content.value(u"domain"_q);
	if (domain.isString()) {
		result.domain = domain.toString();
	}
	result.onSale = object.value(u"on_sale"_q).toBool();
	ClassifyNftKind(result);
	return result;
}

} // namespace

void ClassifyNftKind(NftItem &item) {
	// Numbers and usernames each live in one authoritative Fragment
	// collection, so the on-chain collection address is the authenticity
	// gate for them. Gifts mint one collection per model, which no address
	// list can enumerate, so a gift content URI only nominates a candidate
	// slug; the wallet trusts it after payments.getUniqueStarGift returns
	// this exact item's address as the resolved gift's gift_address.
	if (const auto entry = FragmentEntry(item.collection)) {
		item.kind = entry->kind;
		item.key = FragmentSlug(item.contentUri, entry->segment);
		if (item.key.isEmpty()) {
			item.key = MediaSlug(item, entry->segment);
		}
		if (item.key.isEmpty()) {
			item.key = KeyFromName(item.name, entry->kind);
		}
	} else {
		auto slug = FragmentSlug(item.contentUri, u"gift");
		if (slug.isEmpty()) {
			slug = MediaSlug(item, u"gift");
		}
		if (!slug.isEmpty()) {
			item.kind = NftKind::TelegramGift;
			item.key = std::move(slug);
		}
	}
}

HttpRequest NftItemByAddressRequest(const QString &item) {
	auto result = HttpRequest();
	result.post = false;
	result.endpoint = u"/api/v3/nft/items"_q;
	result.query = u"address="_q + ApiDetails::PercentEncoded(item.toUpper());
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

} // namespace Gram
