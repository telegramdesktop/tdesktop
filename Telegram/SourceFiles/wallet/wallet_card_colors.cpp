/*
This file is part of Telegram Desktop,
the official desktop application for the Telegram messaging service.

For license and copyright information please follow this link:
https://github.com/telegramdesktop/tdesktop/blob/master/LEGAL
*/
#include "wallet/wallet_card_colors.h"

#include "api/api_premium.h"
#include "boxes/star_gift_resale_box.h"
#include "chat_helpers/compose/compose_show.h"
#include "core/ui_integration.h"
#include "data/stickers/data_custom_emoji.h"
#include "data/data_session.h"
#include "data/data_star_gift.h"
#include "data/data_user.h"
#include "lang/lang_keys.h"
#include "main/session/session_show.h"
#include "main/main_app_config.h"
#include "main/main_session.h"
#include "ui/toast/toast.h"
#include "wallet/wallet_chat_show.h"
#include "wallet/wallet_collectible_record.h"
#include "wallet/wallet_palette.h"
#include "window/window_controller.h"
#include "window/window_session_controller.h"

#include "styles/style_wallet.h"

namespace Wallet {
namespace {

constexpr auto kResolveWait = crl::time(5000);
constexpr auto kToastDuration = crl::time(8000);

[[nodiscard]] QString GiftCollectionSlug(const QString &slug) {
	const auto hyphen = slug.lastIndexOf('-');
	const auto number = QStringView(slug).mid(hyphen + 1);
	const auto digit = [](QChar ch) {
		return (ch.unicode() >= u'0') && (ch.unicode() <= u'9');
	};
	if (hyphen <= 0 || number.isEmpty() || !ranges::all_of(number, digit)) {
		return QString();
	}
	return slug.left(hyphen);
}

[[nodiscard]] TextWithEntities ComposeCollectionsList(
		const std::vector<TextWithEntities> &names) {
	const auto count = int(names.size());
	if (!count) {
		return tr::marked();
	} else if (count == 1) {
		return names.front();
	}
	auto full = names.front();
	for (auto i = 1; i != count - 1; ++i) {
		full = tr::lng_wallet_card_color_and_one(
			tr::now,
			lt_all,
			full,
			lt_one,
			names[i],
			tr::marked);
	}
	return tr::lng_wallet_card_color_and_last(
		tr::now,
		lt_all,
		full,
		lt_one,
		names.back(),
		tr::marked);
}

} // namespace

std::vector<QString> CardColorCollections(not_null<Main::Session*> session) {
	return session->appConfig().get<std::vector<QString>>(
		u"wallet_card_bg_gifts"_q,
		std::vector<QString>());
}

bool CardColorAllowed(
		const CollectibleRecord &record,
		const std::vector<QString> &collections) {
	const auto unique = record.gift ? record.gift->unique.get() : nullptr;
	if (!unique) {
		return false;
	}
	const auto collection = GiftCollectionSlug(unique->slug);
	return !collection.isEmpty() && ranges::contains(collections, collection);
}

CardColorToast::CardColorToast(std::shared_ptr<Main::SessionShow> show)
: _show(std::move(show))
, _linkFg([] { return st::mediaviewTextLinkFg->c; })
, _st(st::defaultMultilineToast)
, _api(&_show->session().mtp())
, _timeout([=] { showResolved(); }) {
	_st.palette.linkFg = _linkFg.color();
	_show->session().data().sessionDataAboutToBeCleared(
	) | rpl::on_next([=] {
		cleanup();
		_metadata.clear();
	}, _lifetime);
}

CardColorToast::~CardColorToast() {
	cleanup();
}

void CardColorToast::show(
		const std::vector<QString> &collections,
		const std::vector<CollectibleRecord> &held) {
	cancelResolution();
	dropToast();
	_collections = collections;
	for (auto i = begin(_metadata); i != end(_metadata);) {
		if (ranges::contains(_collections, i->first)) {
			++i;
		} else {
			i = _metadata.erase(i);
		}
	}
	for (const auto &record : held) {
		if (CardColorAllowed(record, _collections)) {
			const auto unique = record.gift->unique.get();
			remember(GiftCollectionSlug(unique->slug), unique);
		}
	}
	for (const auto &slug : _collections) {
		if (!slug.isEmpty()
			&& !_metadata.contains(slug)
			&& !_requests.contains(slug)) {
			resolve(slug);
		}
	}
	_pending = true;
	if (_requests.empty()) {
		showResolved();
	} else {
		_timeout.callOnce(kResolveWait);
	}
}

void CardColorToast::cancelResolution() {
	_timeout.cancel();
	for (const auto &[slug, requestId] : base::take(_requests)) {
		_api.request(requestId).cancel();
	}
}

void CardColorToast::resolve(const QString &slug) {
	const auto requestId = _api.request(MTPpayments_GetUniqueStarGift(
		MTP_string(slug + u"-1"_q)
	)).done([=](const MTPpayments_UniqueStarGift &result) {
		const auto session = &_show->session();
		const auto &data = result.data();
		session->data().processUsers(data.vusers());
		session->data().processChats(data.vchats());
		const auto gift = ::Api::FromTL(session, data.vgift());
		complete(slug, gift ? gift->unique.get() : nullptr);
	}).fail([=] {
		complete(slug, nullptr);
	}).handleAllErrors().send();
	_requests.emplace(slug, requestId);
}

void CardColorToast::remember(
		const QString &slug,
		const Data::UniqueGift *unique) {
	if (slug.isEmpty()
		|| !unique
		|| !unique->initialGiftId
		|| unique->title.isEmpty()
		|| GiftCollectionSlug(unique->slug) != slug) {
		return;
	}
	_metadata.emplace_or_assign(slug, Metadata{
		.title = unique->title,
		.giftId = unique->initialGiftId,
		.sticker = unique->model.document,
	});
}

void CardColorToast::complete(
		const QString &slug,
		const Data::UniqueGift *unique) {
	_requests.remove(slug);
	remember(slug, unique);
	if (_pending && _requests.empty()) {
		showResolved();
	}
}

void CardColorToast::showResolved() {
	cancelResolution();
	_pending = false;
	const auto collections = base::take(_collections);
	if (!_show->valid()) {
		return;
	}
	struct Link {
		QString url;
		uint64 giftId = 0;
		QString title;
	};
	auto links = std::vector<Link>();
	auto names = std::vector<TextWithEntities>();
	for (const auto &slug : collections) {
		const auto i = _metadata.find(slug);
		if (i == end(_metadata)) {
			continue;
		}
		const auto &metadata = i->second;
		const auto url = u"internal:wallet_card_collection/"_q
			+ QString::number(links.size());
		names.push_back(Data::SingleCustomEmoji(
			metadata.sticker
		).append(u' ').append(tr::link(metadata.title, url)));
		links.push_back({ url, metadata.giftId, metadata.title });
	}
	auto text = names.empty()
		? tr::lng_wallet_card_color_not_allowed(tr::now, tr::marked)
		: tr::lng_wallet_card_color_only(
			tr::now,
			lt_collections,
			ComposeCollectionsList(names),
			tr::marked);
	const auto filter = [=](
			const ClickHandlerPtr &handler,
			Qt::MouseButton button) {
		const auto url = handler ? handler->url() : QString();
		const auto i = ranges::find(links, url, &Link::url);
		if (button == Qt::LeftButton && i != end(links)) {
			openResale(i->giftId, i->title);
		}
		return false;
	};
	const auto scope = WindowPaletteScope(_show->toastParent());
	_toast = _show->showToast({
		.text = std::move(text),
		.textContext = Core::TextContext({ .session = &_show->session() }),
		.filter = crl::guard(this, filter),
		.icon = &st::walletCardColorToastIcon,
		.st = &_st,
		.duration = kToastDuration,
	});
}

void CardColorToast::openResale(uint64 giftId, const QString &title) {
	const auto window = MakeChatShow(_show, true)->resolveWindow();
	if (!window) {
		return;
	}
	_resaleLifetime.destroy();
	_resaleLifetime = Ui::ShowStarGiftResale(
		window,
		window->session().user(),
		giftId,
		title,
		crl::guard(this, [=] { _resaleLifetime.destroy(); }));
	window->window().activate();
}

void CardColorToast::dropToast() {
	// WHY: the toast's gift emoji must be released before the session
	// clears its documents, and a hidden toast is only deleted later.
	if (const auto strong = base::take(_toast).get()) {
		delete strong->widget();
	}
}

void CardColorToast::cleanup() {
	cancelResolution();
	_pending = false;
	_collections.clear();
	_resaleLifetime.destroy();
	dropToast();
}

} // namespace Wallet
