/*
This file is part of Telegram Desktop,
the official desktop application for the Telegram messaging service.

For license and copyright information please follow this link:
https://github.com/telegramdesktop/tdesktop/blob/master/LEGAL
*/
#include "data/data_star_gift.h"

#include "api/api_premium.h"
#include "apiwrap.h"
#include "data/data_document.h"
#include "data/data_session.h"
#include "data/data_user.h"
#include "lang/lang_tag.h"
#include "main/main_session.h"
#include "ui/controls/ton_common.h"
#include "ui/text/text_utilities.h"
#include "styles/style_credits.h"

namespace Data {
namespace {

constexpr auto kMyGiftsPerPage = 50;
constexpr auto kResaleGiftsPerPage = 50;

[[nodiscard]] MTPStarGiftAttributeId AttributeToTL(GiftAttributeId id) {
	switch (id.type) {
	case GiftAttributeIdType::Backdrop:
		return MTP_starGiftAttributeIdBackdrop(
			MTP_int(int32(uint32(id.value))));
	case GiftAttributeIdType::Model:
		return MTP_starGiftAttributeIdModel(MTP_long(id.value));
	case GiftAttributeIdType::Pattern:
		return MTP_starGiftAttributeIdPattern(MTP_long(id.value));
	}
	Unexpected("Invalid attribute id type");
}

[[nodiscard]] GiftAttributeId FromTL(const MTPStarGiftAttributeId &id) {
	return id.match([&](const MTPDstarGiftAttributeIdBackdrop &data) {
		return GiftAttributeId{
			.value = uint64(uint32(data.vbackdrop_id().v)),
			.type = GiftAttributeIdType::Backdrop,
		};
	}, [&](const MTPDstarGiftAttributeIdModel &data) {
		return GiftAttributeId{
			.value = data.vdocument_id().v,
			.type = GiftAttributeIdType::Model,
		};
	}, [&](const MTPDstarGiftAttributeIdPattern &data) {
		return GiftAttributeId{
			.value = data.vdocument_id().v,
			.type = GiftAttributeIdType::Pattern,
		};
	});
}

} // namespace

QString UniqueGiftName(const UniqueGift &gift) {
	return gift.title + u" #"_q + QString::number(gift.number);
}

CreditsAmount UniqueGiftResaleStars(const UniqueGift &gift) {
	return CreditsAmount(gift.starsForResale);
}

CreditsAmount UniqueGiftResaleTon(const UniqueGift &gift) {
	return CreditsAmount(
		gift.nanoTonForResale / Ui::kNanosInOne,
		gift.nanoTonForResale % Ui::kNanosInOne,
		CreditsType::Ton);
}

CreditsAmount UniqueGiftResaleAsked(const UniqueGift &gift) {
	return gift.onlyAcceptTon
		? UniqueGiftResaleTon(gift)
		: UniqueGiftResaleStars(gift);
}

TextWithEntities FormatGiftResaleStars(const UniqueGift &gift) {
	return Ui::Text::IconEmoji(
		&st::starIconEmoji
	).append(Lang::FormatCountDecimal(gift.starsForResale));
}

TextWithEntities FormatGiftResaleTon(const UniqueGift &gift) {
	return Ui::Text::IconEmoji(
		&st::tonIconEmoji
	).append(Lang::FormatCreditsAmountDecimal(UniqueGiftResaleTon(gift)));
}

TextWithEntities FormatGiftResaleAsked(const UniqueGift &gift) {
	return gift.onlyAcceptTon
		? FormatGiftResaleTon(gift)
		: FormatGiftResaleStars(gift);
}

GiftAttributeId IdFor(const UniqueGiftBackdrop &value) {
	return {
		.value = uint64(uint32(value.id)),
		.type = GiftAttributeIdType::Backdrop,
	};
}

GiftAttributeId IdFor(const UniqueGiftModel &value) {
	return {
		.value = value.document->id,
		.type = GiftAttributeIdType::Model,
	};
}

GiftAttributeId IdFor(const UniqueGiftPattern &value) {
	return {
		.value = value.document->id,
		.type = GiftAttributeIdType::Pattern,
	};
}

rpl::producer<MyGiftsDescriptor> MyUniqueGiftsSlice(
		not_null<Main::Session*> session,
		MyUniqueType type,
		QString offset) {
	return [=](auto consumer) {
		using Flag = MTPpayments_GetSavedStarGifts::Flag;
		const auto user = session->user();
		const auto requestId = session->api().request(
			MTPpayments_GetSavedStarGifts(
			MTP_flags(Flag::f_exclude_upgradable
				| Flag::f_exclude_unupgradable
				| Flag::f_exclude_unlimited
				| ((type == MyUniqueType::OnlyOwned)
					? Flag::f_exclude_hosted
					: Flag())),
			user->input,
			MTP_int(0), // collection_id
			MTP_string(offset),
			MTP_int(kMyGiftsPerPage)
		)).done([=](const MTPpayments_SavedStarGifts &result) {
			auto gifts = MyGiftsDescriptor();
			const auto &data = result.data();
			if (const auto next = data.vnext_offset()) {
				gifts.offset = qs(*next);
			}

			const auto owner = &session->data();
			owner->processUsers(data.vusers());
			owner->processChats(data.vchats());

			gifts.list.reserve(data.vgifts().v.size());
			for (const auto &gift : data.vgifts().v) {
				if (auto parsed = Api::FromTL(user, gift)) {
					gifts.list.push_back(std::move(*parsed));
				}
			}
			consumer.put_next(std::move(gifts));
			consumer.put_done();
		}).fail([=] {
			consumer.put_next({});
			consumer.put_done();
		}).send();

		auto lifetime = rpl::lifetime();
		lifetime.add([=] { session->api().request(requestId).cancel(); });
		return lifetime;
	};
}

rpl::producer<ResaleGiftsDescriptor> ResaleGiftsSlice(
		not_null<Main::Session*> session,
		uint64 giftId,
		ResaleGiftsFilter filter,
		QString offset) {
	return [=](auto consumer) {
		using Flag = MTPpayments_GetResaleStarGifts::Flag;
		const auto requestId = session->api().request(
			MTPpayments_GetResaleStarGifts(
				MTP_flags(Flag::f_attributes_hash
					| ((filter.sort == ResaleGiftsSort::Price)
						? Flag::f_sort_by_price
						: (filter.sort == ResaleGiftsSort::Number)
						? Flag::f_sort_by_num
						: Flag())
					| (filter.attributes.empty()
						? Flag()
						: Flag::f_attributes)),
				MTP_long(filter.attributesHash),
				MTP_long(giftId),
				MTP_vector_from_range(filter.attributes
					| ranges::views::transform(AttributeToTL)),
				MTP_string(offset),
				MTP_int(kResaleGiftsPerPage)
			)).done([=](const MTPpayments_ResaleStarGifts &result) {
			const auto &data = result.data();
			session->data().processUsers(data.vusers());
			session->data().processChats(data.vchats());

			auto info = ResaleGiftsDescriptor{
				.giftId = giftId,
				.offset = qs(data.vnext_offset().value_or_empty()),
				.count = data.vcount().v,
			};
			const auto &list = data.vgifts().v;
			info.list.reserve(list.size());
			for (const auto &entry : list) {
				if (auto gift = Api::FromTL(session, entry)) {
					info.list.push_back(std::move(*gift));
				}
			}
			info.attributesHash = data.vattributes_hash().value_or_empty();
			const auto &attributes = data.vattributes()
				? data.vattributes()->v
				: QVector<MTPStarGiftAttribute>();
			const auto &counters = data.vcounters()
				? data.vcounters()->v
				: QVector<MTPStarGiftAttributeCounter>();
			auto counts = base::flat_map<GiftAttributeId, int>();
			counts.reserve(counters.size());
			for (const auto &counter : counters) {
				const auto &data = counter.data();
				counts.emplace(FromTL(data.vattribute()), data.vcount().v);
			}
			const auto count = [&](GiftAttributeId id) {
				const auto i = counts.find(id);
				return i != end(counts) ? i->second : 0;
			};
			info.models.reserve(attributes.size());
			info.patterns.reserve(attributes.size());
			info.backdrops.reserve(attributes.size());
			for (const auto &attribute : attributes) {
				attribute.match([&](const MTPDstarGiftAttributeModel &data) {
					const auto parsed = Api::FromTL(session, data);
					info.models.push_back({ parsed, count(IdFor(parsed)) });
				}, [&](const MTPDstarGiftAttributePattern &data) {
					const auto parsed = Api::FromTL(session, data);
					info.patterns.push_back({ parsed, count(IdFor(parsed)) });
				}, [&](const MTPDstarGiftAttributeBackdrop &data) {
					const auto parsed = Api::FromTL(data);
					info.backdrops.push_back({ parsed, count(IdFor(parsed)) });
				}, [](const MTPDstarGiftAttributeOriginalDetails &data) {
				});
			}
			consumer.put_next(std::move(info));
			consumer.put_done();
		}).fail([=](const MTP::Error &error) {
			consumer.put_next({});
			consumer.put_done();
		}).send();

		auto lifetime = rpl::lifetime();
		lifetime.add([=] { session->api().request(requestId).cancel(); });
		return lifetime;
	};
}

} // namespace Data
