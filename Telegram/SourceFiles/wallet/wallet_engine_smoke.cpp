/*
This file is part of Telegram Desktop,
the official desktop application for the Telegram messaging service.

For license and copyright information please follow this link:
https://github.com/telegramdesktop/tdesktop/blob/master/LEGAL
*/
#include "wallet/wallet_engine_smoke.h"

#ifdef _DEBUG

#include "wallet_engine.hpp"

#include <cstdint>
#include <map>
#include <memory>
#include <optional>
#include <string>
#include <utility>
#include <vector>

namespace Wallet {
namespace {

using JournalMapKey = std::pair<std::string, std::string>;

class InMemoryWalletPlatformHost final
	: public wallet_engine::WalletPlatformHost {
public:
	[[nodiscard]] std::vector<uint8_t> read_protected_secret(
		const wallet_engine::ProtectedSecretRead &request) override;
	void store_protected_secret(
		const wallet_engine::ProtectedSecretStore &request) override;
	void delete_protected_secret(
		const wallet_engine::ProtectedSecretRef &secretRef) override;
	[[nodiscard]] std::optional<wallet_engine::JournalRecord> load_journal(
		const wallet_engine::JournalKey &key) override;
	[[nodiscard]] auto compare_exchange_journal(
		const wallet_engine::JournalCompareExchange &mutation)
	-> wallet_engine::JournalCompareExchangeResult override;

	[[nodiscard]] bool hasProtectedSecret(
		const wallet_engine::ProtectedSecretRef &secretRef) const;
	[[nodiscard]] int protectedStoreCalls() const;
	[[nodiscard]] int protectedReadCalls() const;
	[[nodiscard]] int protectedDeleteCalls() const;
	[[nodiscard]] int protectedSuccessfulEraseCount() const;
	[[nodiscard]] int journalLoadCalls() const;
	[[nodiscard]] int journalCompareExchangeCalls() const;

private:
	std::map<std::string, std::vector<uint8_t>> _protectedSecrets;
	std::map<JournalMapKey, wallet_engine::JournalRecord> _journals;
	int _protectedStoreCalls = 0;
	int _protectedReadCalls = 0;
	int _protectedDeleteCalls = 0;
	int _protectedSuccessfulEraseCount = 0;
	int _journalLoadCalls = 0;
	int _journalCompareExchangeCalls = 0;

};

[[noreturn]] void ThrowProtectedSecretNotFound() {
	auto error = wallet_engine::protected_secret_host_error::Failed(
		"Protected secret was not found.");
	error.kind = wallet_engine::ProtectedSecretHostErrorKind::kNotFound;
	error.diagnostic = "Protected secret was not found.";
	throw error;
}

JournalMapKey MakeJournalMapKey(const wallet_engine::JournalKey &key) {
	return { key.record_id, key.slot };
}

int CountPhraseWords(const std::string &phrase) {
	auto count = 0;
	auto inWord = false;
	for (const auto ch : phrase) {
		if (ch == ' ') {
			inWord = false;
		} else if (!inWord) {
			++count;
			inWord = true;
		}
	}
	return count;
}

std::vector<uint8_t> InMemoryWalletPlatformHost::read_protected_secret(
		const wallet_engine::ProtectedSecretRead &request) {
	++_protectedReadCalls;
	const auto i = _protectedSecrets.find(request.secret_ref.value);
	if (i == _protectedSecrets.end()) {
		ThrowProtectedSecretNotFound();
	}
	return i->second;
}

void InMemoryWalletPlatformHost::store_protected_secret(
		const wallet_engine::ProtectedSecretStore &request) {
	++_protectedStoreCalls;
	_protectedSecrets[request.secret_ref.value] = request.bytes;
}

void InMemoryWalletPlatformHost::delete_protected_secret(
		const wallet_engine::ProtectedSecretRef &secretRef) {
	++_protectedDeleteCalls;
	const auto i = _protectedSecrets.find(secretRef.value);
	if (i == _protectedSecrets.end()) {
		ThrowProtectedSecretNotFound();
	}
	_protectedSecrets.erase(i);
	++_protectedSuccessfulEraseCount;
}

auto InMemoryWalletPlatformHost::load_journal(
		const wallet_engine::JournalKey &key)
-> std::optional<wallet_engine::JournalRecord> {
	++_journalLoadCalls;
	const auto i = _journals.find(MakeJournalMapKey(key));
	if (i == _journals.end()) {
		return std::nullopt;
	}
	return i->second;
}

auto InMemoryWalletPlatformHost::compare_exchange_journal(
		const wallet_engine::JournalCompareExchange &mutation)
-> wallet_engine::JournalCompareExchangeResult {
	++_journalCompareExchangeCalls;
	const auto key = MakeJournalMapKey(mutation.key);
	const auto i = _journals.find(key);
	const auto absent = (i == _journals.end());
	const auto matches = absent
		? !mutation.expected_version.has_value()
		: (mutation.expected_version
			&& *mutation.expected_version == i->second.version);
	if (!matches) {
		if (absent) {
			return {
				.applied = false,
				.current = std::nullopt,
			};
		}
		return {
			.applied = false,
			.current = i->second,
		};
	}
	_journals[key] = mutation.replacement;
	return {
		.applied = true,
		.current = mutation.replacement,
	};
}

bool InMemoryWalletPlatformHost::hasProtectedSecret(
		const wallet_engine::ProtectedSecretRef &secretRef) const {
	return _protectedSecrets.find(secretRef.value) != _protectedSecrets.end();
}

int InMemoryWalletPlatformHost::protectedStoreCalls() const {
	return _protectedStoreCalls;
}

int InMemoryWalletPlatformHost::protectedReadCalls() const {
	return _protectedReadCalls;
}

int InMemoryWalletPlatformHost::protectedDeleteCalls() const {
	return _protectedDeleteCalls;
}

int InMemoryWalletPlatformHost::protectedSuccessfulEraseCount() const {
	return _protectedSuccessfulEraseCount;
}

int InMemoryWalletPlatformHost::journalLoadCalls() const {
	return _journalLoadCalls;
}

int InMemoryWalletPlatformHost::journalCompareExchangeCalls() const {
	return _journalCompareExchangeCalls;
}

} // namespace

WalletEngineSmokeResult RunWalletEngineSmoke() {
	const auto manifest = wallet_engine::parse_ton_connect_manifest(
		R"({"url":"https://wallet.example/app",)"
		R"("name":"Telegram Wallet Engine Smoke",)"
		R"("iconUrl":"https://wallet.example/icon.png"})");

	auto result = WalletEngineSmokeResult{
		.manifestUrl = QString::fromStdString(manifest.url),
		.manifestName = QString::fromStdString(manifest.name),
		.manifestIconUrl = QString::fromStdString(manifest.icon_url),
		.manifestDomain = QString::fromStdString(manifest.domain),
	};
	const auto host = std::make_shared<InMemoryWalletPlatformHost>();
	const auto lifecycle = wallet_engine::WalletLifecycle::init(host);
	const auto descriptor = [&] {
		auto created = lifecycle->create_wallet({
			.record_id = "00000000-0000-4000-8000-000000000001",
			.network = wallet_engine::Network::kMainnet,
		});
		return std::move(created.descriptor);
	}();
	result.descriptorAddress = QString::fromStdString(descriptor.address);
	result.descriptorIsMainnet
		= (descriptor.network == wallet_engine::Network::kMainnet);
	result.secretPresentBeforeReveal = host->hasProtectedSecret(
		descriptor.secret_ref);
	{
		const auto phrase = lifecycle->reveal_recovery_phrase(descriptor);
		result.phraseWordCount = CountPhraseWords(phrase.phrase);
	}
	result.secretPresentBeforeDelete = host->hasProtectedSecret(
		descriptor.secret_ref);
	lifecycle->delete_wallet(descriptor);
	result.secretPresentAfterDelete = host->hasProtectedSecret(
		descriptor.secret_ref);
	result.protectedStoreCalls = host->protectedStoreCalls();
	result.protectedReadCalls = host->protectedReadCalls();
	result.protectedDeleteCalls = host->protectedDeleteCalls();
	result.protectedSuccessfulEraseCount
		= host->protectedSuccessfulEraseCount();
	result.journalLoadCalls = host->journalLoadCalls();
	result.journalCompareExchangeCalls = host->journalCompareExchangeCalls();
	result.deleteSucceeded = result.secretPresentBeforeDelete
		&& (result.protectedDeleteCalls == 1)
		&& (result.protectedSuccessfulEraseCount == 1)
		&& !result.secretPresentAfterDelete;
	return result;
}

void ThrowWalletEngineInvalidRecoveryPhrase() {
	const auto host = std::make_shared<InMemoryWalletPlatformHost>();
	const auto lifecycle = wallet_engine::WalletLifecycle::init(host);
	lifecycle->import_wallet({
		.record_id = "00000000-0000-4000-8000-000000000002",
		.network = wallet_engine::Network::kMainnet,
		.recovery_words = {
			"fancy",
			"carpet",
			"hello",
			"mandate",
			"penalty",
			"trial",
			"consider",
			"property",
			"top",
			"vicious",
			"exit",
			"rebuild",
			"tragic",
			"profit",
			"urban",
			"major",
			"total",
			"month",
			"holiday",
			"sudden",
			"rib",
			"gather",
			"media",
		},
	});
}

} // namespace Wallet

#endif // _DEBUG
