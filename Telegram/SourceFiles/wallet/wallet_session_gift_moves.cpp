/*
This file is part of Telegram Desktop,
the official desktop application for the Telegram messaging service.

For license and copyright information please follow this link:
https://github.com/telegramdesktop/tdesktop/blob/master/LEGAL
*/
#include "wallet/wallet_session_gift_moves.h"

namespace Wallet {
namespace {

struct GiftMoveLocal {
	std::string operationId;
	std::optional<TransferWalletIdentity> identity;
	QString collectible;
	TimeId posted = 0;
	bool allowed = false;
	bool covered = false;
};

[[nodiscard]] bool OrderedPage(const std::vector<TransferItem> &page) {
	auto previous = std::optional<TimeId>();
	for (const auto &item : page) {
		if (!item.date || (previous && (*item.date > *previous))) {
			return false;
		}
		previous = item.date;
	}
	return true;
}

[[nodiscard]] bool VerifiedRow(
		const GiftMoveHistoryCoverage &coverage,
		const std::vector<TransferItem> &history,
		const QString &id) {
	return !id.isEmpty() && ranges::any_of(history, [&](const auto &item) {
		return (item.id == id)
			&& (coverage.complete
				|| (coverage.oldest
					&& item.date
					&& (*item.date > *coverage.oldest)));
	});
}

[[nodiscard]] GiftMoveHistoryCoverage HeadCoverage(
		const GiftMoveHistoryCoverage &was,
		const std::vector<TransferItem> &history,
		const std::vector<TransferItem> &page,
		const QString &next) {
	auto own = GiftMoveHistoryCoverage{
		.oldest = page.empty() ? std::nullopt : page.back().date,
		.cursor = next,
		.complete = next.isEmpty(),
	};
	const auto connected = !own.complete
		&& ranges::any_of(page, [&](const TransferItem &item) {
			return VerifiedRow(was, history, item.id);
		});
	if (!connected) {
		return own;
	} else if (was.complete) {
		return was;
	}
	return (*own.oldest <= *was.oldest) ? own : was;
}

[[nodiscard]] GiftMoveHistoryCoverage OlderCoverage(
		const GiftMoveHistoryCoverage &was,
		const std::vector<TransferItem> &history,
		const std::vector<TransferItem> &page,
		const QString &next) {
	const auto unheld = [&](const TransferItem &item) {
		return was.oldest
			&& (*item.date > *was.oldest)
			&& (item.id.isEmpty()
				|| !ranges::contains(history, item.id, &TransferItem::id));
	};
	if (ranges::any_of(page, unheld)) {
		return GiftMoveHistoryCoverage();
	}
	auto oldest = was.oldest;
	if (!page.empty() && (!oldest || (*page.back().date < *oldest))) {
		oldest = page.back().date;
	}
	return {
		.oldest = oldest,
		.cursor = next,
		.complete = next.isEmpty(),
	};
}

[[nodiscard]] bool LocalCollectible(const TransferItem &item) {
	return (item.kind == TransferItem::Kind::Collectible)
		&& !item.collectible.isEmpty()
		&& !item.incoming
		&& item.walletIdentity
		&& item.date
		&& (item.status == TransferItem::Status::Pending);
}

[[nodiscard]] GiftMoveLocal LocalFromItem(
		const SubmittedListFacts &facts,
		const std::string &operationId,
		const TransferItem &item,
		bool sending) {
	const auto &coverage = facts.coverage;
	const auto posted = *item.date;
	return {
		.operationId = operationId,
		.identity = item.walletIdentity,
		.collectible = item.collectible,
		.posted = posted,
		.allowed = (item.giftMove == TransferGiftMove::OwnProfile)
			|| (!sending
				&& (item.giftMove == TransferGiftMove::None)
				&& !item.counterpartyPeer
				&& item.comment.isEmpty()
				&& item.counterpartyBounceable),
		.covered = coverage.complete
			|| (coverage.oldest && (*coverage.oldest < posted - facts.skew)),
	};
}

[[nodiscard]] std::vector<GiftMoveLocal> GiftMoveLocals(
		const SubmittedListFacts &facts) {
	auto result = std::vector<GiftMoveLocal>();
	for (const auto &entry : facts.entries) {
		if (entry.shown
			&& entry.canonicalId.isEmpty()
			&& LocalCollectible(*entry.shown)) {
			result.push_back(LocalFromItem(
				facts,
				entry.operationId,
				*entry.shown,
				(facts.sending && (entry.operationId == facts.sendingId))));
		}
	}
	if (facts.sending
		&& LocalCollectible(*facts.sending)
		&& !ranges::contains(
			facts.entries,
			facts.sendingId,
			&SubmittedListEntry::operationId)) {
		result.push_back(LocalFromItem(
			facts,
			facts.sendingId,
			*facts.sending,
			true));
	}
	return result;
}

[[nodiscard]] std::vector<QString> GiftMoveCandidates(
		const GiftMoveLocal &local,
		const SubmittedListFacts &facts,
		const base::flat_set<QString> &claimed) {
	const auto from = local.posted - facts.skew;
	const auto same = [&](const TransferItem &item) {
		return (item.kind == TransferItem::Kind::Collectible)
			&& (item.collectible == local.collectible)
			&& (item.walletIdentity == local.identity)
			&& item.date;
	};
	auto returned = std::optional<TimeId>();
	for (const auto &item : facts.history) {
		if (same(item)
			&& item.incoming
			&& (item.status == TransferItem::Status::Success)
			&& (*item.date >= from)
			&& (!returned || (*item.date < *returned))) {
			returned = item.date;
		}
	}
	auto result = std::vector<QString>();
	for (const auto &item : facts.history) {
		if (!same(item)
			|| item.incoming
			|| (item.giftMove != TransferGiftMove::OwnProfile)
			|| item.id.isEmpty()
			|| claimed.contains(item.id)
			|| (*item.date < from)
			|| ranges::contains(result, item.id)) {
			continue;
		}
		const auto till = std::max(local.posted + facts.skew, *item.date);
		if (!returned || (*returned > till)) {
			result.push_back(item.id);
		}
	}
	return result;
}

// WHY: a gift can leave, come back and leave again, so a served row stands
// for a local one only within one stay and only when neither has a rival;
// it is recomputed from the facts on every listing and never stored.
[[nodiscard]] base::flat_map<std::string, QString> AssociateGiftMoves(
		const SubmittedListFacts &facts) {
	auto claimed = base::flat_set<QString>();
	for (const auto &entry : facts.entries) {
		if (!entry.canonicalId.isEmpty()) {
			claimed.emplace(entry.canonicalId);
		}
	}
	const auto locals = GiftMoveLocals(facts);
	auto candidates = std::vector<std::vector<QString>>();
	auto owners = base::flat_map<QString, int>();
	for (const auto &local : locals) {
		candidates.push_back(GiftMoveCandidates(local, facts, claimed));
		for (const auto &id : candidates.back()) {
			++owners[id];
		}
	}
	auto result = base::flat_map<std::string, QString>();
	for (auto i = 0; i != int(locals.size()); ++i) {
		const auto &local = locals[i];
		const auto &list = candidates[i];
		if (local.allowed
			&& local.covered
			&& (list.size() == 1)
			&& (owners[list.front()] == 1)
			&& (ranges::count(
				facts.history,
				list.front(),
				&TransferItem::id) == 1)) {
			result.emplace(local.operationId, list.front());
		}
	}
	return result;
}

} // namespace

// Only a gift deposit carries a payload, and it names the own profile.
TransferGiftMove SentGiftMove(const SendArgs &args) {
	return (args.payload.isEmpty() || args.collectible.isEmpty())
		? TransferGiftMove::None
		: TransferGiftMove::OwnProfile;
}

TransferItem ItemFromPending(const PendingSendInfo &pending) {
	return TransferItem{
		.walletIdentity = pending.walletIdentity,
		.kind = (!pending.collectible.isEmpty()
			? TransferItem::Kind::Collectible
			: pending.recipient
			? TransferItem::Kind::PeerTransfer
			: TransferItem::Kind::Transfer),
		.incoming = false,
		.giftMove = pending.giftMove,
		.counterparty = pending.destination,
		.counterpartyBounceable = pending.bounce,
		.counterpartyPeer = (pending.recipient
			? peerFromUser(pending.recipient).value
			: quint64()),
		.collectible = pending.collectible,
		.amountNano = pending.amountNano,
		.comment = pending.comment,
		.date = pending.posted,
		.status = TransferItem::Status::Pending,
	};
}

bool UpdateGiftMoveCoverage(
		GiftMoveHistoryCoverage &coverage,
		const std::vector<TransferItem> &history,
		const std::vector<TransferItem> &page,
		const QString &next,
		const QString &offset) {
	const auto head = offset.isEmpty();
	if (!head && (offset != coverage.cursor)) {
		return false;
	}
	auto now = !OrderedPage(page)
		? GiftMoveHistoryCoverage()
		: head
		? HeadCoverage(coverage, history, page, next)
		: OlderCoverage(coverage, history, page, next);
	const auto changed = (now.oldest != coverage.oldest)
		|| (now.complete != coverage.complete);
	coverage = std::move(now);
	return changed;
}

std::vector<ListedSubmittedTransfer> ListedSubmittedTransfers(
		const SubmittedListFacts &facts) {
	const auto served = AssociateGiftMoves(facts);
	const auto carried = facts.sending
		? served.find(facts.sendingId)
		: end(served);
	auto result = std::vector<ListedSubmittedTransfer>();
	for (const auto &entry : facts.entries) {
		const auto item = entry.shown;
		if (!item
			|| served.contains(entry.operationId)
			|| (facts.boundary
				&& item->date
				&& (*item->date < *facts.boundary))) {
			continue;
		}
		result.push_back({ entry.operationId, *item });
	}
	if (carried != end(served)) {
		result.push_back({
			.operationId = facts.sendingId,
			.item = *facts.sending,
			.servedId = carried->second,
		});
	}
	return result;
}

} // namespace Wallet
