/*
This file is part of Telegram Desktop,
the official desktop application for the Telegram messaging service.

For license and copyright information please follow this link:
https://github.com/telegramdesktop/tdesktop/blob/master/LEGAL
*/
#include "wallet/wallet_gift_move_presentation.h"

#include "base/unixtime.h"
#include "lang/lang_keys.h"
#include "main/main_session.h"
#include "ui/controls/table_rows.h"
#include "wallet/wallet_send_preparation.h"

namespace Wallet {
namespace {

struct ComposedPreview {
	const GiftMovePreviewData *data = nullptr;
	bool live = false;
	bool active = false;
	bool represented = false;
};

[[nodiscard]] std::string PreviewKey(uint64 token) {
	return "gift-move-preview-" + std::to_string(token);
}

[[nodiscard]] uint64 FreshPreviewToken() {
	static auto last = uint64();
	return ++last;
}

[[nodiscard]] bool Superseded(
		const WindowSend &window,
		const GiftMovePreviewData &entry) {
	return !window.operationId.empty()
		&& (window.operationId != entry.operationId)
		&& (window.operationId != entry.selected);
}

[[nodiscard]] bool Identity(
		not_null<Session*> wallet,
		const GiftMovePreviewData &entry) {
	return entry.item.walletIdentity
		&& wallet->transferWalletIdentityCurrent(*entry.item.walletIdentity);
}

[[nodiscard]] bool Live(
		not_null<Session*> wallet,
		const GiftMovePreviewData &entry) {
	return !entry.alive.expired() && Identity(wallet, entry);
}

[[nodiscard]] bool Bridges(
		not_null<Session*> wallet,
		const GiftMovePreviewData &entry) {
	return wallet->listsGated()
		&& !entry.operationId.empty()
		&& Identity(wallet, entry)
		&& wallet->trackedTransaction(entry.operationId);
}

[[nodiscard]] bool Considered(
		not_null<Session*> wallet,
		const GiftMovePreviewData &entry) {
	return Live(wallet, entry) || Bridges(wallet, entry);
}

[[nodiscard]] const GiftMovePreviewData *NewestConsidered(
		not_null<Session*> wallet,
		const std::vector<GiftMovePreviewData> &previews) {
	const auto i = std::find_if(
		previews.rbegin(),
		previews.rend(),
		[&](const GiftMovePreviewData &entry) {
			return Considered(wallet, entry);
		});
	return (i != previews.rend()) ? &*i : nullptr;
}

[[nodiscard]] GiftMovePreviewData *FindPreview(
		not_null<Session*> wallet,
		WindowSend &value,
		uint64 token,
		uint64 generation) {
	const auto i = ranges::find_if(value.previews, [&](const auto &entry) {
		return (entry.token == token)
			&& (entry.generation == generation)
			&& Identity(wallet, entry);
	});
	return (i != end(value.previews)) ? &*i : nullptr;
}

void Prune(not_null<Session*> wallet, WindowSend &value) {
	value.previews.erase(
		ranges::remove_if(value.previews, [&](const auto &entry) {
			return !Considered(wallet, entry);
		}),
		end(value.previews));
}

void Publish(not_null<Session*> wallet, WindowSend value) {
	Prune(wallet, value);
	wallet->setWindowSend(std::move(value));
}

void PublishPruned(not_null<Session*> wallet) {
	auto value = wallet->windowSend();
	const auto count = value.previews.size();
	Prune(wallet, value);
	if (value.previews.size() != count) {
		wallet->setWindowSend(std::move(value));
	}
}

void SchedulePrune(not_null<Session*> wallet) {
	crl::on_main([
		wallet,
		guard = std::weak_ptr<bool>(wallet->windowSend().guard)
	] {
		const auto valid = guard.lock();
		if (valid && *valid) {
			PublishPruned(wallet);
		}
	});
}

void RetireDeadPreview(
		const std::weak_ptr<bool> &guard,
		const base::weak_ptr<Main::Session> &session,
		uint64 token,
		uint64 generation) {
	const auto valid = guard.lock();
	const auto strong = (valid && *valid) ? session.get() : nullptr;
	const auto wallet = strong ? &strong->wallet() : nullptr;
	if (!wallet) {
		return;
	}
	auto value = wallet->windowSend();
	const auto entry = FindPreview(wallet, value, token, generation);
	if (entry && entry->alive.expired()) {
		Publish(wallet, std::move(value));
	}
}

[[nodiscard]] std::vector<ComposedPreview> ComposePreviews(
		not_null<Session*> wallet,
		const WindowSend &window,
		std::vector<ListedSubmittedTransfer> &listed) {
	const auto gated = wallet->listsGated();
	auto result = std::vector<ComposedPreview>();
	auto obsolete = false;
	for (const auto &entry : window.previews) {
		if (!Considered(wallet, entry)) {
			obsolete = true;
			continue;
		}
		const auto &bound = entry.operationId;
		const auto active = !bound.empty()
			&& wallet->sendingTransaction(bound).has_value();
		const auto tracked = (bound.empty() || active)
			? std::optional<TransferItem>()
			: wallet->trackedTransaction(bound);
		if (gated
			&& tracked
			&& !ranges::contains(
				listed,
				bound,
				&ListedSubmittedTransfer::operationId)) {
			listed.push_back({ .operationId = bound, .item = *tracked });
		}
		result.push_back({
			.data = &entry,
			.live = !entry.alive.expired(),
			.active = active,
			.represented = active || tracked.has_value(),
		});
	}
	if (obsolete) {
		SchedulePrune(wallet);
	}
	return result;
}

[[nodiscard]] const ComposedPreview *PrimaryPreview(
		not_null<Session*> wallet,
		const WindowSend &window,
		const std::vector<ComposedPreview> &composed) {
	const auto active = ranges::find(composed, true, &ComposedPreview::active);
	if (active != end(composed)) {
		return &*active;
	} else if (wallet->sendingTransaction(window.operationId)) {
		return nullptr;
	}
	const auto live = std::find_if(
		composed.rbegin(),
		composed.rend(),
		[](const ComposedPreview &entry) { return entry.live; });
	const auto candidate = (live != composed.rend())
		? &*live
		: composed.empty()
		? nullptr
		: &composed.back();
	return (candidate && !Superseded(window, *candidate->data))
		? candidate
		: nullptr;
}

} // namespace

struct GiftMovePreview::Work {
	~Work();

	static void Started(
		std::shared_ptr<Work> work,
		Fn<void(SendStarted)> forward,
		SendStarted value);
	static void Finish(
		std::shared_ptr<Work> work,
		Fn<void(SendError)> forward,
		SendError error);

	[[nodiscard]] Session *resolve() const;
	[[nodiscard]] bool forwardable(uint64 expected) const;
	void bind(const std::string &operationId) const;
	void retire(bool adopt) const;

	std::weak_ptr<bool> guard;
	base::weak_ptr<Main::Session> session;
	base::weak_ptr<GiftMovePreview> owner;
	TransferWalletIdentity identity;
	uint64 token = 0;
	uint64 generation = 0;
	bool started = false;
	bool finished = false;
};

bool ShowsGiftMove(const TransferItem &item) {
	return (item.kind == TransferItem::Kind::Collectible)
		&& !item.collectible.isEmpty()
		&& (item.giftMove != TransferGiftMove::None);
}

QString CollectibleRowKindText(const TransferItem &item) {
	return (item.giftMove == TransferGiftMove::OwnProfile)
		? (item.incoming
			? tr::lng_wallet_row_moved_from_profile(tr::now)
			: tr::lng_wallet_row_moved_to_profile(tr::now))
		: item.incoming
		? tr::lng_wallet_row_collectible_in(tr::now)
		: tr::lng_wallet_row_collectible_out(tr::now);
}

void AddGiftMoveDetailsRows(
		not_null<Ui::TableLayout*> table,
		const TransferItem &item) {
	Ui::AddTableRow(
		table,
		(item.incoming
			? tr::lng_wallet_details_sender()
			: tr::lng_wallet_details_recipient()),
		tr::lng_wallet_row_telegram(tr::marked));
	if (item.giftMove == TransferGiftMove::OwnProfile) {
		Ui::AddTableRow(
			table,
			tr::lng_wallet_details_operation(),
			(item.incoming
				? tr::lng_wallet_details_moved_from_profile(tr::marked)
				: tr::lng_wallet_details_moved_to_profile(tr::marked)));
	}
}

void SetWindowSendOperation(
		not_null<Session*> wallet,
		std::string operationId) {
	auto value = wallet->windowSend();
	value.operationId = std::move(operationId);
	Publish(wallet, std::move(value));
}

const GiftMovePreviewData *CurrentGiftMovePreview(
		not_null<Session*> wallet) {
	return NewestConsidered(wallet, wallet->windowSend().previews);
}

const std::vector<TransferItem> &ListedHistory(not_null<Session*> wallet) {
	static const auto kGated = std::vector<TransferItem>();
	return wallet->listsGated() ? kGated : wallet->history();
}

WindowSendingRow WindowSendingRowFor(
		not_null<Session*> wallet,
		std::vector<ListedSubmittedTransfer> &listed) {
	auto carrier = ListedSubmittedTransfer();
	const auto i = ranges::find_if(listed, [](const auto &entry) {
		return !entry.servedId.isEmpty();
	});
	if (i != end(listed)) {
		carrier = std::move(*i);
		listed.erase(i);
	}
	if (wallet->listsGated()) {
		listed.clear();
	}
	const auto &window = wallet->windowSend();
	const auto composed = ComposePreviews(wallet, window, listed);
	const auto primary = PrimaryPreview(wallet, window, composed);
	auto result = WindowSendingRow{ .operationId = window.operationId };
	for (const auto &entry : composed) {
		if (&entry != primary && !entry.represented) {
			listed.push_back({
				.operationId = PreviewKey(entry.data->token),
				.item = entry.data->item,
			});
		}
	}
	if (primary) {
		const auto key = PreviewKey(primary->data->token);
		if (primary->represented) {
			result.operationId = primary->data->operationId;
			result.predecessor = key;
		} else {
			result.operationId = key;
			result.sending = primary->data->item;
		}
	}
	if (!result.sending) {
		result.sending = wallet->sendingTransaction(result.operationId);
	}
	if (result.sending && (carrier.operationId == result.operationId)) {
		result.servedId = carrier.servedId;
	}
	return result;
}

GiftMovePreview::Work::~Work() {
	crl::on_main([
		guard = guard,
		session = session,
		token = token,
		generation = generation
	] {
		RetireDeadPreview(guard, session, token, generation);
	});
}

void GiftMovePreview::Work::Started(
		std::shared_ptr<Work> work,
		Fn<void(SendStarted)> forward,
		SendStarted value) {
	if (work->finished || work->started || !work->resolve()) {
		return;
	}
	work->started = true;
	work->bind(value.operationId);
	if (work->forwardable(work->generation)) {
		forward(value);
	}
}

void GiftMovePreview::Work::Finish(
		std::shared_ptr<Work> work,
		Fn<void(SendError)> forward,
		SendError error) {
	if (work->finished) {
		return;
	}
	work->finished = true;
	const auto handOff = work->started
		|| (error == SendError::None)
		|| (error == SendError::SubmissionUnknown);
	const auto retry = !handOff
		&& ((error == SendError::QuoteExpired)
			|| (error == SendError::SigningUnavailable));
	const auto owner = work->owner.get();
	auto expected = work->generation;
	if (retry && work->forwardable(expected)) {
		expected = owner->resume(*work);
	} else {
		work->retire(handOff);
	}
	if (work->forwardable(expected)) {
		forward(error);
	}
}

Session *GiftMovePreview::Work::resolve() const {
	const auto valid = guard.lock();
	const auto strong = (valid && *valid) ? session.get() : nullptr;
	const auto wallet = strong ? &strong->wallet() : nullptr;
	return (wallet && wallet->transferWalletIdentityCurrent(identity))
		? wallet
		: nullptr;
}

bool GiftMovePreview::Work::forwardable(uint64 expected) const {
	const auto strong = owner.get();
	return resolve()
		&& strong
		&& !strong->_closed
		&& (strong->_token == token)
		&& (strong->_generation == expected);
}

void GiftMovePreview::Work::bind(const std::string &operationId) const {
	const auto wallet = finished ? nullptr : resolve();
	if (!wallet || operationId.empty()) {
		return;
	}
	auto value = wallet->windowSend();
	const auto entry = FindPreview(wallet, value, token, generation);
	if (!entry || (entry->operationId == operationId)) {
		return;
	}
	entry->operationId = operationId;
	Publish(wallet, std::move(value));
}

void GiftMovePreview::Work::retire(bool adopt) const {
	const auto wallet = resolve();
	if (!wallet) {
		return;
	}
	auto value = wallet->windowSend();
	const auto entry = FindPreview(wallet, value, token, generation);
	if (!entry) {
		return;
	}
	if (adopt
		&& !entry->operationId.empty()
		&& !Superseded(value, *entry)
		&& (NewestConsidered(wallet, value.previews) == entry)) {
		value.operationId = entry->operationId;
	}
	entry->alive.reset();
	Publish(wallet, std::move(value));
}

GiftMovePreview::GiftMovePreview(not_null<Main::Session*> session)
: _session(session) {
}

Fn<SendCallbacks(SendCallbacks)> GiftMovePreview::attempts() {
	return [weak = base::make_weak(this)](SendCallbacks callbacks) {
		if (const auto strong = weak.get()) {
			return strong->submitting(std::move(callbacks));
		}
		return callbacks;
	};
}

void GiftMovePreview::show(
		const TransferWalletIdentity &identity,
		const SendArgs &args) {
	const auto wallet = resolveWallet();
	if (_closed
		|| !wallet
		|| !wallet->transferWalletIdentityCurrent(identity)) {
		return;
	}
	auto value = wallet->windowSend();
	if (!value.guard || !*value.guard) {
		value.guard = std::make_shared<bool>(true);
	}
	_token = FreshPreviewToken();
	_lease = fresh(value.guard, identity);
	value.previews.push_back(GiftMovePreviewData{
		.token = _token,
		.generation = _lease->generation,
		.selected = value.operationId,
		.item = ItemFromPending({
			.walletIdentity = identity,
			.posted = base::unixtime::now(),
			.amountNano = args.amountNano,
			.destination = CanonicalAddress(args.destination),
			.collectible = args.collectible,
			.comment = (args.comment.isPublic
				? args.comment.text
				: QString()),
			.recipient = args.userId,
			.bounce = args.bounce,
			.giftMove = TransferGiftMove::OwnProfile,
		}),
		.alive = _lease,
	});
	Publish(wallet, std::move(value));
}

void GiftMovePreview::stop() {
	if (const auto lease = base::take(_lease)) {
		lease->retire(false);
	}
}

void GiftMovePreview::close() {
	_closed = true;
	stop();
}

Session *GiftMovePreview::resolveWallet() const {
	const auto strong = _session.get();
	return strong ? &strong->wallet() : nullptr;
}

std::shared_ptr<GiftMovePreview::Work> GiftMovePreview::fresh(
		std::weak_ptr<bool> guard,
		const TransferWalletIdentity &identity) {
	auto result = std::make_shared<Work>();
	result->guard = std::move(guard);
	result->session = _session;
	result->owner = base::make_weak(this);
	result->identity = identity;
	result->token = _token;
	result->generation = ++_generation;
	return result;
}

SendCallbacks GiftMovePreview::submitting(SendCallbacks callbacks) {
	const auto lease = base::take(_lease);
	const auto wallet = lease ? lease->resolve() : nullptr;
	if (!wallet) {
		return callbacks;
	}
	auto value = wallet->windowSend();
	const auto entry = FindPreview(
		wallet,
		value,
		lease->token,
		lease->generation);
	if (!entry) {
		return callbacks;
	}
	const auto work = fresh(lease->guard, lease->identity);
	entry->generation = work->generation;
	entry->alive = work;
	auto result = SendCallbacks{
		.started = [work, forward = std::move(callbacks.started)](
				SendStarted started) {
			Work::Started(work, forward, std::move(started));
		},
		.sent = [work, forward = std::move(callbacks.sent)](SendError error) {
			Work::Finish(work, forward, error);
		},
		.preparing = [work](std::string operationId) {
			work->bind(operationId);
		},
	};
	Publish(wallet, std::move(value));
	return result;
}

uint64 GiftMovePreview::resume(const Work &work) {
	_lease = fresh(work.guard, work.identity);
	const auto generation = _generation;
	const auto wallet = work.resolve();
	if (!wallet) {
		return generation;
	}
	auto value = wallet->windowSend();
	const auto entry = FindPreview(
		wallet,
		value,
		work.token,
		work.generation);
	if (entry) {
		entry->generation = generation;
		entry->operationId.clear();
		entry->alive = _lease;
		Publish(wallet, std::move(value));
	}
	return generation;
}

} // namespace Wallet
