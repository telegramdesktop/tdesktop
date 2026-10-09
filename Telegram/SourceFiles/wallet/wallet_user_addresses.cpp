/*
This file is part of Telegram Desktop,
the official desktop application for the Telegram messaging service.

For license and copyright information please follow this link:
https://github.com/telegramdesktop/tdesktop/blob/master/LEGAL
*/
#include "wallet/wallet_user_addresses.h"

#include "base/flat_map.h"
#include "base/flat_set.h"
#include "data/data_session.h"
#include "data/data_user.h"
#include "main/main_session.h"
#include "mtproto/mtproto_response.h"
#include "wallet/wallet_address.h"
#include "wallet/wallet_session.h"

namespace Wallet {
namespace {

constexpr auto kUserAddressesPerRequest = 100;
constexpr auto kPublicKeySize = 32;

[[nodiscard]] std::vector<std::vector<UserId>> ChunkUserIds(
		const std::vector<UserId> &ids) {
	auto result = std::vector<std::vector<UserId>>();
	auto added = base::flat_set<UserId>();
	added.reserve(ids.size());
	for (const auto id : ids) {
		if (!added.emplace(id).second) {
			continue;
		} else if (result.empty()
			|| int(result.back().size()) == kUserAddressesPerRequest) {
			result.emplace_back();
		}
		result.back().push_back(id);
	}
	return result;
}

[[nodiscard]] base::flat_map<UserId, QString> ChunkAnswer(
		const std::vector<UserId> &asked,
		const QVector<MTPWalletUserAddress> &reply) {
	auto result = base::flat_map<UserId, QString>();
	result.reserve(asked.size());
	for (const auto id : asked) {
		result.emplace(id, QString());
	}
	for (const auto &entry : reply) {
		const auto &data = entry.data();
		const auto id = UserId(data.vuser_id().value_or_empty());
		const auto i = result.find(id);
		if (i == end(result)) {
			LOG(("Wallet Error: wallet.getUserAddresses answered about "
				"user %1, which was not asked about."
				).arg(id.bare));
			continue;
		}
		auto address = CanonicalAddress(qs(data.vaddress()));
		if (address.isEmpty()) {
			LOG(("Wallet Error: wallet.getUserAddresses answered with "
				"an unusable address for user %1."
				).arg(id.bare));
		}
		i->second = std::move(address);
	}
	return result;
}

} // namespace

UserAddresses::UserAddresses(not_null<Main::Session*> session)
: _session(session)
, _api(&session->mtp()) {
}

void UserAddresses::resolve(std::vector<UserId> ids, Fn<void()> done) {
	const auto job = std::make_shared<Job>();
	job->done = std::move(done);
	if (unavailable()) {
		finish(job);
		return;
	}
	auto pending = std::vector<UserId>();
	pending.reserve(ids.size());
	for (const auto id : ids) {
		const auto user = _session->data().userLoaded(id);
		if (user && !user->gramAddress()) {
			pending.push_back(id);
		}
	}
	auto chunks = ChunkUserIds(pending);
	job->chunks = int(chunks.size());
	if (!job->chunks) {
		finish(job);
		return;
	}
	for (auto &chunk : chunks) {
		sendChunk(job, std::move(chunk));
	}
}

void UserAddresses::forceResolve(
		UserId id,
		Fn<void(QString)> done,
		Fn<void(ForceResolveError)> fail) {
	const auto refuse = [fail = std::move(fail)](ForceResolveError error) {
		LOG(("Wallet Error: forced wallet.getUserAddresses failed: %1"
			).arg(error.type));
		if (fail) {
			fail(std::move(error));
		}
	};
	if (const auto error = recipientError(id); !error.isEmpty()) {
		refuse({ .type = error });
		return;
	}
	auto &wallet = _session->wallet();
	if (wallet.presence() != Presence::Ready) {
		refuse({ .type = u"WALLET_NOT_READY"_q });
		return;
	}
	const auto readyError = forceResolveError(id);
	if (!readyError.isEmpty()) {
		refuse({ .type = readyError });
		return;
	}
	const auto user = _session->data().userLoaded(id);
	const auto serial = ++_requestSerial;
	_api.request(MTPwallet_GetUserAddresses(
		MTP_flags(MTPwallet_GetUserAddresses::Flag::f_force),
		MTP_vector<MTPInputUser>(1, user->inputUser()),
		MTP_vector<MTPstring>()
	)).done([=, done = std::move(done)](
			const MTPwallet_UserAddresses &result) {
		const auto &reply = processReply(result);
		const auto address = (reply.size() == 1
			&& UserId(reply.front().data().vuser_id().value_or_empty()) == id)
			? CanonicalAddress(qs(reply.front().data().vaddress()))
			: QString();
		if (address.isEmpty()) {
			refuse({ .type = u"WALLET_ADDRESS_INVALID"_q });
			return;
		} else if (answerIsNewest(id, serial)) {
			user->setGramAddress(address);
			noteAnswer(id, serial);
		}
		// A stale reply answers with the newer address it lost to.
		const auto current = user->gramAddress().value_or(QString());
		if (current.isEmpty()) {
			refuse({ .type = u"WALLET_ADDRESS_INVALID"_q });
			return;
		} else if (done) {
			done(current);
		}
	}).fail([=](const MTP::Error &error) {
		if (error.type() == u"WALLET_UNAVAILABLE"_q) {
			_unavailable = true;
		}
		refuse({
			.type = error.type(),
			.silent = MTP::IgnoreError(error),
		});
	}).handleAllErrors().send();
}

QString UserAddresses::forceResolveError(UserId id) const {
	if (const auto error = recipientError(id); !error.isEmpty()) {
		return error;
	}
	const auto &wallet = _session->wallet();
	if (wallet.presenceCurrent() != Presence::Ready) {
		return u"WALLET_NOT_READY"_q;
	}
	if (wallet.balanceNano() <= 0) {
		return u"WALLET_BALANCE_EMPTY"_q;
	}
	return QString();
}

QString UserAddresses::recipientError(UserId id) const {
	if (unavailable()) {
		return u"WALLET_UNAVAILABLE"_q;
	}
	const auto user = id ? _session->data().userLoaded(id) : nullptr;
	if (!user || (!user->isSelf() && !user->accessHash())) {
		return u"WALLET_USER_INVALID"_q;
	}
	if (user->isBot()
		|| user->isSupport()
		|| user->isInaccessible()
		|| user->isRepliesChat()
		|| user->isVerifyCodes()
		|| user->isNotificationsUser()) {
		return u"WALLET_USER_INELIGIBLE"_q;
	}
	return QString();
}

void UserAddresses::resolveOwner(
		QString address,
		Fn<void(AddressOwner)> done,
		Fn<void(bool silent)> fail) {
	const auto canonical = CanonicalAddress(address);
	if (canonical.isEmpty() || unavailable()) {
		if (done) {
			done({});
		}
		return;
	}
	const auto i = _owners.find(canonical);
	if (i != end(_owners)) {
		if (done) {
			done(i->second);
		}
		return;
	}
	_ownerWaiting[canonical].push_back({
		.done = std::move(done),
		.fail = std::move(fail),
	});
	if (!_ownerRequested.emplace(canonical).second) {
		return;
	}
	const auto serial = ++_requestSerial;
	_api.request(MTPwallet_GetUserAddresses(
		MTP_flags(0),
		MTP_vector<MTPInputUser>(),
		MTP_vector<MTPstring>(1, MTP_string(canonical))
	)).done([=](const MTPwallet_UserAddresses &result) {
		const auto &reply = processReply(result);
		auto owner = AddressOwner();
		for (const auto &entry : reply) {
			const auto &data = entry.data();
			if (CanonicalAddress(qs(data.vaddress())) != canonical) {
				LOG(("Wallet Error: wallet.getUserAddresses answered about "
					"an address, which was not asked about."));
				continue;
			}
			owner = AddressOwner{
				.userId = UserId(data.vuser_id().value_or_empty()),
				.address = canonical,
				.publicKey = data.vpublic_key().v,
			};
		}
		const auto user = owner.userId
			? _session->data().userLoaded(owner.userId)
			: nullptr;
		if (user && answerIsNewest(owner.userId, serial)) {
			user->setGramAddress(canonical);
			noteAnswer(owner.userId, serial);
		}
		finishOwner(canonical, std::move(owner), true, false);
	}).fail([=](const MTP::Error &error) {
		const auto unavailable = (error.type() == u"WALLET_UNAVAILABLE"_q);
		if (unavailable) {
			_unavailable = true;
		}
		LOG(("Wallet Error: wallet.getUserAddresses by address failed: %1"
			).arg(error.type()));
		finishOwner(
			canonical,
			unavailable ? std::make_optional(AddressOwner()) : std::nullopt,
			false,
			MTP::IgnoreError(error));
	}).send();
}

QByteArray UserAddresses::publicKey(const QString &address) const {
	const auto i = _publicKeys.find(CanonicalAddress(address));
	return (i != end(_publicKeys)) ? i->second : QByteArray();
}

UserAddress UserAddresses::known(UserId id) const {
	const auto user = _session->data().userLoaded(id);
	if (!user || !user->gramAddress()) {
		return {};
	}
	const auto &address = *user->gramAddress();
	if (address.isEmpty()) {
		return { .state = UserAddressState::Absent };
	}
	return {
		.state = UserAddressState::Known,
		.address = address,
	};
}

bool UserAddresses::unavailable() const {
	return _unavailable.current();
}

rpl::producer<bool> UserAddresses::unavailableValue() const {
	return _unavailable.value();
}

void UserAddresses::resetUnavailable() {
	_unavailable = false;
}

void UserAddresses::sendChunk(
		const std::shared_ptr<Job> &job,
		std::vector<UserId> ids) {
	auto users = MTP_vector_from_range(ids
		| ranges::views::transform([&](UserId id) {
			return _session->data().userLoaded(id)->inputUser();
		}));
	const auto serial = ++_requestSerial;
	_api.request(MTPwallet_GetUserAddresses(
		MTP_flags(0),
		std::move(users),
		MTP_vector<MTPstring>()
	)).done([=](const MTPwallet_UserAddresses &result) {
		applyChunk(ids, processReply(result), serial);
		finishChunk(job);
	}).fail([=](const MTP::Error &error) {
		if (error.type() == u"WALLET_UNAVAILABLE"_q) {
			LOG(("Wallet Error: wallet.getUserAddresses is unavailable."));
			_unavailable = true;
		} else {
			LOG(("Wallet Error: wallet.getUserAddresses failed: %1"
				).arg(error.type()));
		}
		finishChunk(job);
	}).send();
}

// A chunk's answer is only as fresh as the moment the chunk was sent.
// resolve() sends an id only while nothing has answered about it, and an
// answer is never taken back, so a value engaged when the reply lands was
// written after this chunk left and is the newer answer — it wins for
// every id the chunk answers about, not only the ones its reply stayed
// silent about. A chunk's silence still records absence for every id that
// is still unanswered when the reply lands.
void UserAddresses::applyChunk(
		const std::vector<UserId> &asked,
		const QVector<MTPWalletUserAddress> &reply,
		uint64 serial) {
	for (const auto &[id, address] : ChunkAnswer(asked, reply)) {
		const auto user = _session->data().userLoaded(id);
		if (user && !user->gramAddress()) {
			user->setGramAddress(address);
			noteAnswer(id, serial);
		}
	}
}

bool UserAddresses::answerIsNewest(UserId id, uint64 serial) const {
	const auto i = _answeredBy.find(id);
	return (i == end(_answeredBy)) || (i->second < serial);
}

void UserAddresses::noteAnswer(UserId id, uint64 serial) {
	auto &answered = _answeredBy[id];
	answered = std::max(answered, serial);
}

// The users come along so the peer an address names is showable at once.
const QVector<MTPWalletUserAddress> &UserAddresses::processReply(
		const MTPwallet_UserAddresses &result) {
	const auto &data = result.data();
	_session->data().processUsers(data.vusers());
	rememberKeys(data.vaddresses().v);
	return data.vaddresses().v;
}

// A key is public metadata about the address it comes with, and the engine
// verifies that it derives that address before it encrypts anything with it,
// so the newest answer simply wins for every address it names.
void UserAddresses::rememberKeys(const QVector<MTPWalletUserAddress> &reply) {
	for (const auto &entry : reply) {
		const auto &data = entry.data();
		const auto address = CanonicalAddress(qs(data.vaddress()));
		const auto &key = data.vpublic_key().v;
		if (address.isEmpty() || key.size() != kPublicKeySize) {
			continue;
		}
		_publicKeys[address] = key;
	}
}

void UserAddresses::finishOwner(
		const QString &address,
		std::optional<AddressOwner> owner,
		bool cache,
		bool silent) {
	_ownerRequested.remove(address);
	if (owner && cache) {
		_owners.emplace(address, *owner);
	}
	const auto i = _ownerWaiting.find(address);
	if (i == end(_ownerWaiting)) {
		return;
	}
	// A failure is not remembered, so a callback may ask about the same
	// address again and start a new request. Retire this wait list before
	// answering, so that new one is not dropped with it.
	auto waiting = std::move(i->second);
	_ownerWaiting.erase(i);
	for (const auto &waiter : waiting) {
		if (owner && waiter.done) {
			waiter.done(*owner);
		} else if (!owner && waiter.fail) {
			waiter.fail(silent);
		}
	}
}

void UserAddresses::finishChunk(const std::shared_ptr<Job> &job) {
	Expects(job->chunks > 0);

	if (!--job->chunks) {
		finish(job);
	}
}

void UserAddresses::finish(const std::shared_ptr<Job> &job) {
	if (const auto done = base::take(job->done)) {
		done();
	}
}

} // namespace Wallet
