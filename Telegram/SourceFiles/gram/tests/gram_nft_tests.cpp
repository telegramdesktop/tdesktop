/*
This file is part of Telegram Desktop,
the official desktop application for the Telegram messaging service.

For license and copyright information please follow this link:
https://github.com/telegramdesktop/tdesktop/blob/master/LEGAL
*/
#include "gram/tests/gram_tests.h"

#include "gram/api/gram_api_nft.h"

#include <vector>

namespace Gram::Tests {
namespace {

const auto kOwner1Raw = u"0:9DA971AF38D2F03ABDF308D5F91636A97E5A2B07A66C39D71D7CBAE3B032EDDC"_q;

} // namespace

std::vector<Check> NftChecks() {
	return {
		{ u"nft_builder_item_by_address"_q, [] {
			const auto raw = u"0:ca0cfd519f763102b5bec9d9e3af4359"
				u"2ea362fb773fa319ea09c4f162c171e0"_q;
			return CheckRequest(
				NftItemByAddressRequest(raw),
				false,
				u"/api/v3/nft/items"_q,
				u"address=0%3ACA0CFD519F763102B5BEC9D9E3AF4359"
				u"2EA362FB773FA319EA09C4F162C171E0"_q,
				QByteArray());
		} },
		{ u"nft_items_owner1_fixture"_q, [] {
			const auto name = u"api-nft-items-owner1.json"_q;
			const auto bytes = ReadFixture(name);
			if (bytes.isEmpty()) {
				return u"fixture read failed: "_q + name;
			}
			const auto page = ParseNftItems(bytes, 100);
			if (!page) {
				return u"parse failed: "_q + name;
			} else if (int(page->list.size()) != 70) {
				return u"count: got "_q
					+ QString::number(page->list.size())
					+ u", expected 70"_q;
			} else if (page->hasNext) {
				return u"limit 100: expected hasNext false"_q;
			}
			const auto exact = ParseNftItems(bytes, 70);
			if (!exact) {
				return u"parse failed: "_q + name;
			} else if (!exact->hasNext) {
				return u"limit 70: expected hasNext true"_q;
			}
			const auto address = u"0:8A81E2A362D3E09E35AA8FB32EE175E4"
				u"690AFD746B71C1B5503F130191E88FA7"_q;
			const auto collection = u"0:0C8F3FCC4ABD589206A2CDF1469E3709"
				u"2C1ADAD272FBE7DB97104569C16F0FF2"_q;
			const auto uri = u"http://192.236.162.114/nft/1.json"_q;
			const auto &item = page->list.front();
			if (item.address.toUpper() != address) {
				return u"address: got "_q
					+ item.address.toUpper()
					+ u", expected "_q
					+ address;
			} else if (item.index != u"1076"_q) {
				return u"index: got "_q + item.index + u", expected 1076"_q;
			} else if (item.collection.toUpper() != collection) {
				return u"collection: got "_q
					+ item.collection.toUpper()
					+ u", expected "_q
					+ collection;
			} else if (item.contentUri != uri) {
				return u"contentUri: got "_q
					+ item.contentUri
					+ u", expected "_q
					+ uri;
			} else if (item.onSale) {
				return u"onSale: got true, expected false"_q;
			} else if (item.kind != NftKind::Generic) {
				return u"kind: got "_q
					+ QString::number(int(item.kind))
					+ u", expected Generic"_q;
			} else if (!item.key.isEmpty()) {
				return u"key: got "_q + item.key + u", expected none"_q;
			} else if (item.realOwner.toUpper() != kOwner1Raw) {
				return u"realOwner: got "_q
					+ item.realOwner.toUpper()
					+ u", expected "_q
					+ kOwner1Raw;
			}
			const auto pagedName = u"api-nft-items-owner1-paged.json"_q;
			const auto pagedBytes = ReadFixture(pagedName);
			if (pagedBytes.isEmpty()) {
				return u"fixture read failed: "_q + pagedName;
			}
			const auto paged = ParseNftItems(pagedBytes, 50);
			if (!paged) {
				return u"parse failed: "_q + pagedName;
			} else if (int(paged->list.size()) != 50) {
				return u"paged count: got "_q
					+ QString::number(paged->list.size())
					+ u", expected 50"_q;
			} else if (!paged->hasNext) {
				return u"paged: expected hasNext true"_q;
			}
			return QString();
		} },
		{ u"nft_items_index_precision"_q, [] {
			const auto name = u"api-nft-items-address-gift.json"_q;
			const auto bytes = ReadFixture(name);
			if (bytes.isEmpty()) {
				return u"fixture read failed: "_q + name;
			}
			const auto page = ParseNftItems(bytes, 100);
			if (!page) {
				return u"parse failed: "_q + name;
			} else if (int(page->list.size()) != 1) {
				return u"gift count: got "_q
					+ QString::number(page->list.size())
					+ u", expected 1"_q;
			}
			const auto expected = u"5800280859629342981385244539048694670709"
				u"8687748635984871519305117335532662285"_q;
			const auto &index = page->list.front().index;
			if (index != expected) {
				return u"gift index: got "_q
					+ index
					+ u", expected "_q
					+ expected;
			} else if (int(index.size()) != 77) {
				return u"gift index size: got "_q
					+ QString::number(index.size())
					+ u", expected 77"_q;
			}
			auto ok = false;
			const auto asInt64 = index.toLongLong(&ok);
			if (ok) {
				return u"gift index: an int64 accepted 77 digits as "_q
					+ QString::number(asInt64)
					+ u", so the value must stay a string"_q;
			}
			const auto owner = u"api-nft-items-owner1.json"_q;
			const auto ownerBytes = ReadFixture(owner);
			if (ownerBytes.isEmpty()) {
				return u"fixture read failed: "_q + owner;
			}
			const auto items = ParseNftItems(ownerBytes, 100);
			if (!items) {
				return u"parse failed: "_q + owner;
			}
			auto longest = QString();
			auto longestCount = 0;
			for (const auto &item : items->list) {
				if (item.index.size() > longest.size()) {
					longest = item.index;
					longestCount = 1;
				} else if (item.index.size() == longest.size()) {
					++longestCount;
				}
			}
			const auto longestExpected =
				u"1054708645066200327782942852958057359233"
				u"77271801864155263396403385820771314635"_q;
			if (int(longest.size()) != 78) {
				return u"owner1 longest index size: got "_q
					+ QString::number(longest.size())
					+ u", expected 78"_q;
			} else if (longestCount != 5) {
				return u"owner1 78-digit indexes: got "_q
					+ QString::number(longestCount)
					+ u", expected 5"_q;
			} else if (longest != longestExpected) {
				return u"owner1 longest index: got "_q
					+ longest
					+ u", expected "_q
					+ longestExpected;
			}
			return QString();
		} },
		{ u"nft_items_content_uri_spread"_q, [] {
			const auto name = u"api-nft-items-owner1.json"_q;
			const auto bytes = ReadFixture(name);
			if (bytes.isEmpty()) {
				return u"fixture read failed: "_q + name;
			}
			const auto page = ParseNftItems(bytes, 100);
			if (!page) {
				return u"parse failed: "_q + name;
			} else if (int(page->list.size()) != 70) {
				return u"count: got "_q
					+ QString::number(page->list.size())
					+ u", expected 70"_q;
			}
			auto withUri = 0;
			auto withoutUri = 0;
			auto https = 0;
			auto http = 0;
			auto ipfs = 0;
			auto tonstorage = 0;
			auto domains = 0;
			auto ipfsUri = QString();
			auto storageUri = QString();
			for (const auto &item : page->list) {
				if (item.contentUri.isEmpty()) {
					++withoutUri;
					if (!item.domain.isEmpty()) {
						++domains;
					}
				} else {
					++withUri;
					if (item.contentUri.startsWith(u"https://"_q)) {
						++https;
					} else if (item.contentUri.startsWith(u"http://"_q)) {
						++http;
					} else if (item.contentUri.startsWith(u"ipfs:"_q)) {
						++ipfs;
						ipfsUri = item.contentUri;
					} else if (item.contentUri.startsWith(u"tonstorage:"_q)) {
						++tonstorage;
						storageUri = item.contentUri;
					}
				}
			}
			if (withUri != 57 || withoutUri != 13) {
				return u"uri spread: got "_q
					+ QString::number(withUri)
					+ u" with a uri and "_q
					+ QString::number(withoutUri)
					+ u" without, expected 57 and 13"_q;
			} else if (https != 50 || http != 5) {
				return u"http spread: got "_q
					+ QString::number(https)
					+ u" https and "_q
					+ QString::number(http)
					+ u" http, expected 50 and 5"_q;
			} else if (ipfs != 1 || tonstorage != 1) {
				return u"exotic spread: got "_q
					+ QString::number(ipfs)
					+ u" ipfs and "_q
					+ QString::number(tonstorage)
					+ u" tonstorage, expected 1 and 1"_q;
			} else if (domains != 12) {
				return u"uriless domains: got "_q
					+ QString::number(domains)
					+ u", expected 12 of the 13 (the 13th carries an image)"_q;
			}
			const auto expectedIpfs =
				u"ipfs://QmZUbAvRbmwbn26fq2312fp67c6iZD2vK1NmwmimWTnA38"_q;
			const auto expectedStorage =
				u"tonstorage://6BF34B39DC5FD6AC4FF845722E16EA85"
				u"74A2504B7C6E1F0DF375805B69AAABE5/2006.json"_q;
			if (ipfsUri != expectedIpfs) {
				return u"ipfs uri: got "_q
					+ ipfsUri
					+ u", expected "_q
					+ expectedIpfs;
			} else if (storageUri != expectedStorage) {
				return u"tonstorage uri: got "_q
					+ storageUri
					+ u", expected "_q
					+ expectedStorage;
			}
			return QString();
		} },
		{ u"nft_items_null_collection"_q, [] {
			const auto owner1 = u"api-nft-items-owner1.json"_q;
			const auto firstBytes = ReadFixture(owner1);
			if (firstBytes.isEmpty()) {
				return u"fixture read failed: "_q + owner1;
			}
			const auto first = ParseNftItems(firstBytes, 100);
			if (!first) {
				return u"parse failed: "_q + owner1;
			}
			auto firstCount = 0;
			for (const auto &item : first->list) {
				if (!item.collection.isEmpty()) {
					continue;
				}
				++firstCount;
				if (item.kind != NftKind::Generic) {
					return u"owner1 null-collection: got kind "_q
						+ QString::number(int(item.kind))
						+ u" for "_q
						+ item.address.toUpper()
						+ u", expected Generic"_q;
				} else if (!item.key.isEmpty()) {
					return u"owner1 null-collection: got key "_q
						+ item.key
						+ u" for "_q
						+ item.address.toUpper()
						+ u", expected none"_q;
				}
			}
			if (firstCount != 8) {
				return u"owner1 null-collection: got "_q
					+ QString::number(firstCount)
					+ u", expected 8 (the 11 in task.md is the six-capture "
					u"total)"_q;
			}
			const auto owner2 = u"api-nft-items-owner2.json"_q;
			const auto secondBytes = ReadFixture(owner2);
			if (secondBytes.isEmpty()) {
				return u"fixture read failed: "_q + owner2;
			}
			const auto second = ParseNftItems(secondBytes, 100);
			if (!second) {
				return u"parse failed: "_q + owner2;
			}
			auto secondCount = 0;
			for (const auto &item : second->list) {
				if (!item.collection.isEmpty()) {
					continue;
				}
				++secondCount;
				if (item.kind != NftKind::Generic || !item.key.isEmpty()) {
					return u"owner2 null-collection: expected Generic with "
						u"no key for "_q
						+ item.address.toUpper();
				}
			}
			if (secondCount != 3) {
				return u"owner2 null-collection: got "_q
					+ QString::number(secondCount)
					+ u", expected 3"_q;
			} else if (firstCount + secondCount != 11) {
				return u"owner listings null-collection: got "_q
					+ QString::number(firstCount + secondCount)
					+ u", expected 11 (8 + 3)"_q;
			}
			return QString();
		} },
		{ u"nft_kind_fragment_gift_slug"_q, [] {
			const auto name = u"api-nft-items-address-gift.json"_q;
			const auto bytes = ReadFixture(name);
			if (bytes.isEmpty()) {
				return u"fixture read failed: "_q + name;
			}
			const auto page = ParseNftItems(bytes, 100);
			if (!page) {
				return u"parse failed: "_q + name;
			} else if (int(page->list.size()) != 1) {
				return u"count: got "_q
					+ QString::number(page->list.size())
					+ u", expected 1"_q;
			}
			const auto uri =
				u"https://nft.fragment.com/gift/deskcalendar-45754.json"_q;
			const auto &item = page->list.front();
			if (item.contentUri != uri) {
				return u"contentUri: got "_q
					+ item.contentUri
					+ u", expected "_q
					+ uri;
			} else if (item.kind != NftKind::TelegramGift) {
				return u"kind: got "_q
					+ QString::number(int(item.kind))
					+ u", expected TelegramGift; the kind comes from the "
					u"content.uri nft.fragment.com/<segment>/ shape, because "
					u"each gift model mints its own collection contract and "
					u"no address list can enumerate them"_q;
			} else if (item.key != u"deskcalendar-45754"_q) {
				return u"key: got "_q
					+ item.key
					+ u", expected deskcalendar-45754; the slug goes verbatim "
					u"into payments.getUniqueStarGift, so the server's own "
					u"capitalization of it is not derivable from any committed "
					u"byte and is deliberately not asserted"_q;
			}
			const auto nested = QByteArray(
				"{\"nft_items\":[{"
					"\"address\":\"0:CA0CFD519F763102B5BEC9D9E3AF4359"
						"2EA362FB773FA319EA09C4F162C171E0\","
					"\"index\":\"1\","
					"\"collection_address\":"
						"\"0:4C71F300665314AF55B75FC91D130DDF"
						"24C5006961F8F9772613947945F14863\","
					"\"content\":{\"uri\":\"https://nft.fragment.com/gift/"
						"sub/dir.json\"},"
					"\"on_sale\":false,"
					"\"real_owner\":null"
				"}]}");
			const auto nestedPage = ParseNftItems(nested, 100);
			if (!nestedPage) {
				return u"nested slug: parse failed"_q;
			} else if (int(nestedPage->list.size()) != 1) {
				return u"nested slug: got "_q
					+ QString::number(nestedPage->list.size())
					+ u" items, expected 1"_q;
			}
			const auto &nestedItem = nestedPage->list.front();
			if (nestedItem.kind != NftKind::Generic) {
				return u"nested slug: got kind "_q
					+ QString::number(int(nestedItem.kind))
					+ u", expected Generic; a remainder that leaves "
					u"[a-zA-Z0-9._-] - the only class the app's "
					u"t.me/nft/<slug> handler accepts - yields no slug, and "
					u"without a resolvable slug the gift kind is useless, so "
					u"the item stays Generic"_q;
			} else if (!nestedItem.key.isEmpty()) {
				return u"nested slug: got key "_q
					+ nestedItem.key
					+ u", expected none"_q;
			}
			const auto foreign = QByteArray(
				"{\"nft_items\":[{"
					"\"address\":\"0:CA0CFD519F763102B5BEC9D9E3AF4359"
						"2EA362FB773FA319EA09C4F162C171E0\","
					"\"index\":\"1\","
					"\"collection_address\":"
						"\"0:0C8F3FCC4ABD589206A2CDF1469E3709"
						"2C1ADAD272FBE7DB97104569C16F0FF2\","
					"\"content\":{\"uri\":\"https://nft.fragment.com/gift/"
						"deskcalendar-45754.json\"},"
					"\"on_sale\":false,"
					"\"real_owner\":null"
				"}]}");
			const auto synthetic = ParseNftItems(foreign, 100);
			if (!synthetic) {
				return u"foreign collection: parse failed"_q;
			} else if (int(synthetic->list.size()) != 1) {
				return u"foreign collection: got "_q
					+ QString::number(synthetic->list.size())
					+ u" items, expected 1"_q;
			}
			const auto &other = synthetic->list.front();
			if (other.kind != NftKind::TelegramGift) {
				return u"foreign collection: got kind "_q
					+ QString::number(int(other.kind))
					+ u", expected TelegramGift; a gift uri nominates a "
					u"candidate whatever its collection, because every gift "
					u"model mints its own collection contract. The uri is "
					u"item-controlled, so the wallet shows the gift view "
					u"only after payments.getUniqueStarGift resolves the "
					u"slug and the reply's gift_address equals this exact "
					u"item's address; a forged uri fails that binding and "
					u"lands in the generic preview fallback"_q;
			} else if (other.key != u"deskcalendar-45754"_q) {
				return u"foreign collection: got key "_q
					+ other.key
					+ u", expected deskcalendar-45754"_q;
			}
			return QString();
		} },
		{ u"nft_kind_fragment_number_and_username"_q, [] {
			const auto numbers = u"api-nft-items-collection-numbers.json"_q;
			const auto numbersBytes = ReadFixture(numbers);
			if (numbersBytes.isEmpty()) {
				return u"fixture read failed: "_q + numbers;
			}
			const auto numbersPage = ParseNftItems(numbersBytes, 100);
			if (!numbersPage) {
				return u"parse failed: "_q + numbers;
			} else if (int(numbersPage->list.size()) != 1) {
				return u"numbers count: got "_q
					+ QString::number(numbersPage->list.size())
					+ u", expected 1"_q;
			}
			const auto &number = numbersPage->list.front();
			if (number.kind != NftKind::TelegramNumber) {
				return u"numbers kind: got "_q
					+ QString::number(int(number.kind))
					+ u", expected TelegramNumber"_q;
			} else if (number.key != u"88807684929"_q) {
				return u"numbers key: got "_q
					+ number.key
					+ u", expected 88807684929"_q;
			} else if (!number.domain.isEmpty()) {
				return u"numbers domain: got "_q
					+ number.domain
					+ u", expected none"_q;
			} else if (number.onSale) {
				return u"numbers onSale: got true, expected false"_q;
			}
			const auto usernames =
				u"api-nft-items-collection-usernames.json"_q;
			const auto usernamesBytes = ReadFixture(usernames);
			if (usernamesBytes.isEmpty()) {
				return u"fixture read failed: "_q + usernames;
			}
			const auto usernamesPage = ParseNftItems(usernamesBytes, 100);
			if (!usernamesPage) {
				return u"parse failed: "_q + usernames;
			} else if (int(usernamesPage->list.size()) != 1) {
				return u"usernames count: got "_q
					+ QString::number(usernamesPage->list.size())
					+ u", expected 1"_q;
			}
			const auto owner = u"0:E10225471558A1CBB59CF73C8265CBE2"
				u"ACA02F804C1A893992DFF468D21C15E9"_q;
			const auto sale = u"0:71315F35CFE3911C7BBC651E11131353"
				u"16C7EADF7280EE0105D7E2D9F80D46A7"_q;
			const auto &username = usernamesPage->list.front();
			if (username.kind != NftKind::TelegramUsername) {
				return u"usernames kind: got "_q
					+ QString::number(int(username.kind))
					+ u", expected TelegramUsername"_q;
			} else if (username.key != u"katrinakaif"_q) {
				return u"usernames key: got "_q
					+ username.key
					+ u", expected katrinakaif"_q;
			} else if (username.domain != u"katrinakaif.t.me"_q) {
				return u"usernames domain: got "_q
					+ username.domain
					+ u", expected katrinakaif.t.me"_q;
			} else if (!username.onSale) {
				return u"usernames onSale: got false, expected true"_q;
			} else if (username.realOwner.toUpper() == sale) {
				return u"usernames realOwner: got the sale contract, so the "
					u"parser read owner_address or sale_contract_address "
					u"instead of real_owner"_q;
			} else if (username.realOwner.toUpper() != owner) {
				return u"usernames realOwner: got "_q
					+ username.realOwner.toUpper()
					+ u", expected "_q
					+ owner;
			}
			const auto forged = QByteArray(
				"{\"nft_items\":[{"
					"\"address\":\"0:CA0CFD519F763102B5BEC9D9E3AF4359"
						"2EA362FB773FA319EA09C4F162C171E0\","
					"\"index\":\"1\","
					"\"collection_address\":"
						"\"0:0C8F3FCC4ABD589206A2CDF1469E3709"
						"2C1ADAD272FBE7DB97104569C16F0FF2\","
					"\"content\":{\"uri\":\"https://nft.fragment.com/"
						"number/88807684929.json\"},"
					"\"on_sale\":false,"
					"\"real_owner\":null"
				"}]}");
			const auto forgedPage = ParseNftItems(forged, 100);
			if (!forgedPage || int(forgedPage->list.size()) != 1) {
				return u"forged number: parse failed"_q;
			}
			const auto &fake = forgedPage->list.front();
			if (fake.kind != NftKind::Generic || !fake.key.isEmpty()) {
				return u"forged number: got kind "_q
					+ QString::number(int(fake.kind))
					+ u" key "_q
					+ fake.key
					+ u", expected Generic with none; numbers and "
					u"usernames have exactly one authoritative Fragment "
					u"collection each, so a real-shaped uri under any "
					u"other collection is forged metadata and must not "
					u"classify"_q;
			}
			return QString();
		} },
		{ u"nft_classify_kind_direct"_q, [] {
			const auto foreignRaw = u"0:0c8f3fcc4abd589206a2cdf1469e3709"
				u"2c1adad272fbe7db97104569c16f0ff2"_q;
			auto gift = NftItem();
			gift.collection = foreignRaw;
			gift.contentUri =
				u"https://nft.fragment.com/gift/deskcalendar-45754.json"_q;
			ClassifyNftKind(gift);
			if (gift.kind != NftKind::TelegramGift) {
				return u"gift kind: got "_q
					+ QString::number(int(gift.kind))
					+ u", expected TelegramGift; every gift model mints its "
					u"own collection contract, so the gift uri nominates the "
					u"candidate whatever collection holds the item"_q;
			} else if (gift.key != u"deskcalendar-45754"_q) {
				return u"gift key: got "_q
					+ gift.key
					+ u", expected deskcalendar-45754"_q;
			}
			const auto numberRaw = u"0:0e41dc1dc3c9067ed24248580e12b335"
				u"9818d83dee0304fabcf80845eafafdb2"_q;
			const auto numberUri =
				u"https://nft.fragment.com/number/88807684929.json"_q;
			auto number = NftItem();
			number.collection = numberRaw;
			number.contentUri = numberUri;
			ClassifyNftKind(number);
			if (number.kind != NftKind::TelegramNumber) {
				return u"number kind: got "_q
					+ QString::number(int(number.kind))
					+ u", expected TelegramNumber"_q;
			} else if (number.key != u"88807684929"_q) {
				return u"number key: got "_q
					+ number.key
					+ u", expected 88807684929"_q;
			}
			auto forged = NftItem();
			forged.collection = foreignRaw;
			forged.contentUri = numberUri;
			ClassifyNftKind(forged);
			if (forged.kind != NftKind::Generic || !forged.key.isEmpty()) {
				return u"forged number: got kind "_q
					+ QString::number(int(forged.kind))
					+ u" key "_q
					+ forged.key
					+ u", expected Generic with none; numbers and usernames "
					u"each have exactly one authoritative Fragment "
					u"collection, so the address still gates them and only "
					u"a gift uri nominates without one"_q;
			}
			return QString();
		} },
		{ u"nft_classify_kind_media"_q, [] {
			const auto foreignRaw = u"0:0c8f3fcc4abd589206a2cdf1469e3709"
				u"2c1adad272fbe7db97104569c16f0ff2"_q;
			const auto numberRaw = u"0:0e41dc1dc3c9067ed24248580e12b335"
				u"9818d83dee0304fabcf80845eafafdb2"_q;
			const auto usernameRaw = u"0:80d78a35f955a14b679faa887ff4cd5b"
				u"fc0f43b4a4eea2a7e6927f3701b273c2"_q;
			const auto document = [](const char *url) {
				return NftWebDocument{ .url = QByteArray(url) };
			};
			const auto expect = [](
					const QString &label,
					const NftItem &item,
					NftKind kind,
					const QString &key) {
				if (item.kind == kind && item.key == key) {
					return QString();
				}
				return label
					+ u": got kind "_q
					+ QString::number(int(item.kind))
					+ u" key '"_q
					+ item.key
					+ u"', expected kind "_q
					+ QString::number(int(kind))
					+ u" key '"_q
					+ key
					+ u"'"_q;
			};
			auto result = QString();
			const auto check = [&](
					const QString &label,
					NftItem item,
					NftKind kind,
					const QString &key) {
				ClassifyNftKind(item);
				if (result.isEmpty()) {
					result = expect(label, item, kind, key);
				}
			};

			auto giftImage = NftItem();
			giftImage.collection = foreignRaw;
			giftImage.image = document(
				"https://nft.fragment.com/gift/deskcalendar-45754.webp");
			check(
				u"gift from image"_q,
				giftImage,
				NftKind::TelegramGift,
				u"deskcalendar-45754"_q);

			auto giftLottie = NftItem();
			giftLottie.collection = foreignRaw;
			giftLottie.imageSmall = document(
				"https://imgproxy.toncenter.com/CLisjZYndcPa0HF7lUeL2LkBthE4"
				"L7OJ04Gp8GEDknE/pr:small/aHR0cHM6Ly9uZnQuZnJhZ21lbnQuY29tL"
				"2dpZnQvZGVza2NhbGVuZGFyLTQ1NzU0LndlYnA");
			giftLottie.lottie = document(
				"https://nft.fragment.com/gift/deskcalendar-45754.lottie.json");
			check(
				u"gift from lottie"_q,
				giftLottie,
				NftKind::TelegramGift,
				u"deskcalendar-45754"_q);

			auto numberImage = NftItem();
			numberImage.collection = numberRaw;
			numberImage.name = u"+888 0768 4929"_q;
			numberImage.image = document(
				"https://nft.fragment.com/number/88807684929.webp");
			check(
				u"number from image"_q,
				numberImage,
				NftKind::TelegramNumber,
				u"88807684929"_q);

			auto numberName = NftItem();
			numberName.collection = numberRaw;
			numberName.name = u"+888 0768 4929"_q;
			check(
				u"number from name"_q,
				numberName,
				NftKind::TelegramNumber,
				u"88807684929"_q);

			auto usernameName = NftItem();
			usernameName.collection = usernameRaw;
			usernameName.name = u"@tolya"_q;
			check(
				u"username from name"_q,
				usernameName,
				NftKind::TelegramUsername,
				u"tolya"_q);

			auto forged = NftItem();
			forged.collection = foreignRaw;
			forged.name = u"+888 0768 4929"_q;
			forged.image = document(
				"https://nft.fragment.com/number/88807684929.webp");
			check(
				u"forged number media"_q,
				forged,
				NftKind::Generic,
				QString());

			auto foreignHost = NftItem();
			foreignHost.collection = foreignRaw;
			foreignHost.image = document("https://example.com/gift/x.webp");
			check(
				u"foreign host"_q,
				foreignHost,
				NftKind::Generic,
				QString());

			auto traversal = NftItem();
			traversal.collection = foreignRaw;
			traversal.image = document(
				"https://nft.fragment.com/gift/../x.webp");
			check(
				u"dot segment"_q,
				traversal,
				NftKind::Generic,
				QString());

			auto encoded = NftItem();
			encoded.collection = foreignRaw;
			encoded.image = document(
				"https://nft.fragment.com/gift/x%2Fy.webp");
			check(
				u"encoded slash"_q,
				encoded,
				NftKind::Generic,
				QString());

			auto bare = NftItem();
			bare.collection = foreignRaw;
			bare.name = u"Some Item #1"_q;
			check(u"bare generic"_q, bare, NftKind::Generic, QString());
			return result;
		} },
		{ u"nft_items_negative"_q, [] {
			const auto name = u"api-nft-items-camel.json"_q;
			const auto bytes = ReadFixture(name);
			if (bytes.isEmpty()) {
				return u"fixture read failed: "_q + name;
			} else if (ParseNftItems(bytes, 100)) {
				return u"expected nullopt for the error body: "_q
					+ QString::fromUtf8(bytes);
			}
			const auto bad = std::vector<QByteArray>{
				QByteArray(""),
				QByteArray("{"),
				QByteArray("[]"),
				QByteArray("{}"),
				QByteArray("null"),
				QByteArray("42"),
				QByteArray("{\"nft_items\":{}}"),
				QByteArray("{\"nft_items\":[42]}"),
				QByteArray("{\"nft_items\":[{}]}"),
			};
			for (const auto &json : bad) {
				if (ParseNftItems(json, 100)) {
					return u"expected nullopt for: "_q
						+ QString::fromUtf8(json);
				}
			}
			return QString();
		} },
	};
}

} // namespace Gram::Tests
