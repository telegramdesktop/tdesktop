/*
This file is part of Telegram Desktop,
the official desktop application for the Telegram messaging service.

For license and copyright information please follow this link:
https://github.com/telegramdesktop/tdesktop/blob/master/LEGAL
*/
#include "wallet/wallet_onramp.h"

#include "main/main_session.h"

namespace Wallet {
namespace {

const auto kCryptoCurrency = u"GRAM"_q;

[[nodiscard]] std::vector<OnrampProvider> ParseProviders(
		const MTPVector<MTPOnrampProviderInfo> &result) {
	auto providers = std::vector<OnrampProvider>();
	providers.reserve(result.v.size());
	for (const auto &provider : result.v) {
		const auto &data = provider.data();
		auto cryptoCurrencies = std::vector<QString>();
		cryptoCurrencies.reserve(data.vcrypto_currencies().v.size());
		for (const auto &currency : data.vcrypto_currencies().v) {
			cryptoCurrencies.push_back(qs(currency));
		}
		providers.push_back({
			.id = qs(data.vid()),
			.name = qs(data.vname()),
			.cryptoCurrencies = std::move(cryptoCurrencies),
			.supportsBaseCurrencies = data.is_supports_base_currencies(),
			.supportsLimits = data.is_supports_limits(),
			.supportsQuote = data.is_supports_quote(),
		});
	}
	return providers;
}

[[nodiscard]] std::vector<QString> ParseBaseCurrencies(
		const MTPVector<MTPstring> &result) {
	auto currencies = std::vector<QString>();
	currencies.reserve(result.v.size());
	for (const auto &currency : result.v) {
		currencies.push_back(qs(currency));
	}
	return currencies;
}

[[nodiscard]] OnrampAvailability ParseAvailability(
		const MTPOnrampAvailability &result) {
	const auto &data = result.data();
	auto state = std::optional<QString>();
	if (const auto value = data.vstate()) {
		state = qs(*value);
	}
	auto methods = std::vector<OnrampMethod>();
	methods.reserve(data.vmethods().v.size());
	for (const auto &method : data.vmethods().v) {
		const auto &methodData = method.data();
		methods.push_back({
			.paymentMethod = qs(methodData.vpayment_method()),
			.available = methodData.is_available(),
		});
	}
	return {
		.allowed = data.is_allowed(),
		.buyAllowed = data.is_buy_allowed(),
		.countryCode = qs(data.vcountry_code()),
		.state = std::move(state),
		.methods = std::move(methods),
	};
}

[[nodiscard]] OnrampHostedSession ParseHostedSession(
		const MTPOnrampSession &result) {
	const auto &data = result.data();
	return {
		.provider = qs(data.vprovider()),
		.sessionId = qs(data.vsession_id()),
		.url = qs(data.vurl()),
		.expiresDate = data.vexpires_date().v,
	};
}

} // namespace

Onramp::Onramp(not_null<Main::Session*> session)
: _api(&session->mtp()) {
}

Onramp::~Onramp() {
	_destroying = true;
	cancel(_providers, Method::Providers);
	cancel(_baseCurrencies, Method::BaseCurrencies);
	cancel(_availability, Method::Availability);
	cancel(_hostedSession, Method::CreateSession);
}

Onramp::ProvidersState Onramp::providersCurrent() const {
	return _providers.state.current();
}

rpl::producer<Onramp::ProvidersState> Onramp::providersValue() const {
	return _providers.state.value();
}

Onramp::BaseCurrenciesState Onramp::baseCurrenciesCurrent() const {
	return _baseCurrencies.state.current();
}

auto Onramp::baseCurrenciesValue() const
-> rpl::producer<Onramp::BaseCurrenciesState> {
	return _baseCurrencies.state.value();
}

Onramp::AvailabilityState Onramp::availabilityCurrent() const {
	return _availability.state.current();
}

rpl::producer<Onramp::AvailabilityState> Onramp::availabilityValue() const {
	return _availability.state.value();
}

Onramp::HostedSessionState Onramp::hostedSessionCurrent() const {
	return _hostedSession.state.current();
}

auto Onramp::hostedSessionValue() const
-> rpl::producer<Onramp::HostedSessionState> {
	return _hostedSession.state.value();
}

std::optional<OnrampRouteSelection> Onramp::lastRouteSelection() const {
	return _lastRouteSelection;
}

rpl::producer<OnrampRouteSelection> Onramp::routeSelections() const {
	return _routeSelections.events();
}

void Onramp::selectRoute(OnrampRouteSelection selection) {
	_lastRouteSelection = selection;
	_routeSelections.fire(std::move(selection));
}

void Onramp::requestProvidersForGram() {
	using Flag = MTPpayments_GetOnrampProviders::Flag;
	using Flags = MTPpayments_GetOnrampProviders::Flags;
	const auto flags = Flags(Flag::f_crypto_currency);
	const auto generation = begin(_providers, Method::Providers);
	if (!generation) {
		return;
	}
#ifdef _DEBUG
	if (debugIntercept(
			_providers,
			generation,
			{
				.event = DebugOnrampEvent::Send,
				.method = DebugOnrampMethod::Providers,
				.generation = generation,
				.flags = flags.value(),
				.cryptoCurrency = kCryptoCurrency,
			})) {
		return;
	}
#endif // _DEBUG
	const auto id = _api.request(MTPpayments_GetOnrampProviders(
		MTP_flags(flags),
		MTP_string(kCryptoCurrency)
	)).done([=](const MTPVector<MTPOnrampProviderInfo> &result) {
		apply(_providers, generation, ParseProviders(result));
	}).fail([=](const MTP::Error &error) {
		fail(_providers, generation, { error.code(), error.type() });
	}).send();
	finishSend(_providers, generation, id);
}

void Onramp::requestAllProviders() {
	using Flags = MTPpayments_GetOnrampProviders::Flags;
	const auto flags = Flags();
	const auto generation = begin(_providers, Method::Providers);
	if (!generation) {
		return;
	}
#ifdef _DEBUG
	if (debugIntercept(
			_providers,
			generation,
			{
				.event = DebugOnrampEvent::Send,
				.method = DebugOnrampMethod::Providers,
				.generation = generation,
				.flags = flags.value(),
			})) {
		return;
	}
#endif // _DEBUG
	const auto id = _api.request(MTPpayments_GetOnrampProviders(
		MTP_flags(flags),
		MTP_string(QString())
	)).done([=](const MTPVector<MTPOnrampProviderInfo> &result) {
		apply(_providers, generation, ParseProviders(result));
	}).fail([=](const MTP::Error &error) {
		fail(_providers, generation, { error.code(), error.type() });
	}).send();
	finishSend(_providers, generation, id);
}

void Onramp::requestBaseCurrencies(const QString &provider) {
	const auto providerSnapshot = provider;
	const auto generation = begin(
		_baseCurrencies,
		Method::BaseCurrencies);
	if (!generation) {
		return;
	}
#ifdef _DEBUG
	if (debugIntercept(
			_baseCurrencies,
			generation,
			{
				.event = DebugOnrampEvent::Send,
				.method = DebugOnrampMethod::BaseCurrencies,
				.generation = generation,
				.provider = providerSnapshot,
				.cryptoCurrency = kCryptoCurrency,
			})) {
		return;
	}
#endif // _DEBUG
	const auto id = _api.request(MTPpayments_GetOnrampBaseCurrencies(
		MTP_string(providerSnapshot),
		MTP_string(kCryptoCurrency)
	)).done([=](const MTPVector<MTPstring> &result) {
		apply(_baseCurrencies, generation, ParseBaseCurrencies(result));
	}).fail([=](const MTP::Error &error) {
		fail(_baseCurrencies, generation, { error.code(), error.type() });
	}).send();
	finishSend(_baseCurrencies, generation, id);
}

void Onramp::requestAvailability(
		const QString &provider,
		std::optional<QString> baseCurrency) {
	const auto providerSnapshot = provider;
	const auto baseCurrencySnapshot = std::move(baseCurrency);
	using Flag = MTPpayments_GetOnrampAvailability::Flag;
	using Flags = MTPpayments_GetOnrampAvailability::Flags;
	const auto flags = Flags(
		baseCurrencySnapshot ? Flag::f_base_currency : Flag());
	const auto generation = begin(_availability, Method::Availability);
	if (!generation) {
		return;
	}
#ifdef _DEBUG
	if (debugIntercept(
			_availability,
			generation,
			{
				.event = DebugOnrampEvent::Send,
				.method = DebugOnrampMethod::Availability,
				.generation = generation,
				.flags = flags.value(),
				.provider = providerSnapshot,
				.cryptoCurrency = kCryptoCurrency,
				.baseCurrency = baseCurrencySnapshot,
			})) {
		return;
	}
#endif // _DEBUG
	const auto id = _api.request(MTPpayments_GetOnrampAvailability(
		MTP_flags(flags),
		MTP_string(providerSnapshot),
		MTP_string(kCryptoCurrency),
		MTP_string(baseCurrencySnapshot ? *baseCurrencySnapshot : QString())
	)).done([=](const MTPOnrampAvailability &result) {
		apply(_availability, generation, ParseAvailability(result));
	}).fail([=](const MTP::Error &error) {
		fail(_availability, generation, { error.code(), error.type() });
	}).send();
	finishSend(_availability, generation, id);
}

void Onramp::createSession(const OnrampSessionArgs &args) {
	const auto argsSnapshot = args;
	using Flag = MTPpayments_CreateOnrampSession::Flag;
	const auto flags = (argsSnapshot.paymentMethod
		? Flag::f_payment_method
		: Flag())
		| (argsSnapshot.baseCurrency ? Flag::f_base_currency : Flag())
		| (argsSnapshot.baseAmount ? Flag::f_base_amount : Flag())
		| (argsSnapshot.memo ? Flag::f_memo : Flag())
		| (argsSnapshot.theme ? Flag::f_theme : Flag())
		| (argsSnapshot.successReturnUrl
			? Flag::f_success_return_url
			: Flag())
		| (argsSnapshot.failReturnUrl ? Flag::f_fail_return_url : Flag());
	const auto generation = begin(_hostedSession, Method::CreateSession);
	if (!generation) {
		return;
	}
#ifdef _DEBUG
	if (debugIntercept(
			_hostedSession,
			generation,
			{
				.event = DebugOnrampEvent::Send,
				.method = DebugOnrampMethod::CreateSession,
				.generation = generation,
				.flags = flags.value(),
				.provider = argsSnapshot.provider,
				.cryptoCurrency = kCryptoCurrency,
				.address = argsSnapshot.address,
				.paymentMethod = argsSnapshot.paymentMethod,
				.baseCurrency = argsSnapshot.baseCurrency,
				.baseAmount = argsSnapshot.baseAmount,
				.memo = argsSnapshot.memo,
				.theme = argsSnapshot.theme,
				.successReturnUrl = argsSnapshot.successReturnUrl,
				.failReturnUrl = argsSnapshot.failReturnUrl,
			})) {
		return;
	}
#endif // _DEBUG
	const auto id = _api.request(MTPpayments_CreateOnrampSession(
		MTP_flags(flags),
		MTP_string(argsSnapshot.provider),
		MTP_string(kCryptoCurrency),
		MTP_string(argsSnapshot.address),
		MTP_string(argsSnapshot.paymentMethod
			? *argsSnapshot.paymentMethod
			: QString()),
		MTP_string(argsSnapshot.baseCurrency
			? *argsSnapshot.baseCurrency
			: QString()),
		MTP_string(argsSnapshot.baseAmount
			? *argsSnapshot.baseAmount
			: QString()),
		MTP_string(argsSnapshot.memo ? *argsSnapshot.memo : QString()),
		MTP_string(argsSnapshot.theme ? *argsSnapshot.theme : QString()),
		MTP_string(argsSnapshot.successReturnUrl
			? *argsSnapshot.successReturnUrl
			: QString()),
		MTP_string(argsSnapshot.failReturnUrl
			? *argsSnapshot.failReturnUrl
			: QString()),
		MTP_string() // crypto_amount
	)).done([=](const MTPOnrampSession &result) {
		apply(_hostedSession, generation, ParseHostedSession(result));
	}).fail([=](const MTP::Error &error) {
		fail(_hostedSession, generation, { error.code(), error.type() });
	}).send();
	finishSend(_hostedSession, generation, id);
}

#ifdef _DEBUG
void Onramp::setDebugRequestInterceptor(
		Fn<bool(const DebugOnrampRequest &)> interceptor) {
	_debugRequestInterceptor = std::move(interceptor);
}

void Onramp::debugResolve(
		uint64 generation,
		const MTPVector<MTPOnrampProviderInfo> &result) {
	apply(_providers, generation, ParseProviders(result));
}

void Onramp::debugResolve(
		uint64 generation,
		const MTPVector<MTPstring> &result) {
	apply(_baseCurrencies, generation, ParseBaseCurrencies(result));
}

void Onramp::debugResolve(
		uint64 generation,
		const MTPOnrampAvailability &result) {
	apply(_availability, generation, ParseAvailability(result));
}

void Onramp::debugResolve(
		uint64 generation,
		const MTPOnrampSession &result) {
	apply(_hostedSession, generation, ParseHostedSession(result));
}

void Onramp::debugFail(
		DebugOnrampMethod method,
		uint64 generation,
		int code,
		QString type) {
	switch (method) {
	case DebugOnrampMethod::Providers:
		fail(_providers, generation, { code, std::move(type) });
		return;
	case DebugOnrampMethod::BaseCurrencies:
		fail(_baseCurrencies, generation, { code, std::move(type) });
		return;
	case DebugOnrampMethod::Availability:
		fail(_availability, generation, { code, std::move(type) });
		return;
	case DebugOnrampMethod::CreateSession:
		fail(_hostedSession, generation, { code, std::move(type) });
		return;
	}
}

DebugOnrampMethod Onramp::DebugMethod(Method method) {
	switch (method) {
	case Method::Providers:
		return DebugOnrampMethod::Providers;
	case Method::BaseCurrencies:
		return DebugOnrampMethod::BaseCurrencies;
	case Method::Availability:
		return DebugOnrampMethod::Availability;
	case Method::CreateSession:
		return DebugOnrampMethod::CreateSession;
	}
	Unexpected("Onramp method.");
}

template <typename Value>
bool Onramp::debugIntercept(
		Lane<Value> &lane,
		uint64 generation,
		const DebugOnrampRequest &request) {
	const auto interceptor = _debugRequestInterceptor;
	const auto weak = base::make_weak(this);
	const auto intercepted = interceptor && interceptor(request);
	return !weak || intercepted || !active(lane, generation);
}

void Onramp::debugObserveCancel(Method method, uint64 generation) const {
	if (!generation) {
		return;
	}
	const auto interceptor = _debugRequestInterceptor;
	if (!interceptor) {
		return;
	}
	interceptor({
		.event = DebugOnrampEvent::Cancel,
		.method = DebugMethod(method),
		.generation = generation,
	});
}
#endif // _DEBUG

template <typename Value>
uint64 Onramp::begin(Lane<Value> &lane, Method method) {
	if (_destroying) {
		return 0;
	}
	const auto previousGeneration = lane.generation;
	const auto cancelWeak = base::make_weak(this);
	cancel(lane, method);
	if (!cancelWeak) {
		return 0;
	}
	if (lane.generation != previousGeneration || lane.activeGeneration) {
		return 0;
	}
	if (!++lane.generation) {
		++lane.generation;
	}
	const auto generation = lane.generation;
	lane.activeGeneration = generation;
	const auto publishWeak = base::make_weak(this);
	lane.state = OnrampLoadState<Value>{ .pending = true };
	if (!publishWeak) {
		return 0;
	}
	return active(lane, generation) ? generation : 0;
}

template <typename Value>
bool Onramp::active(
		const Lane<Value> &lane,
		uint64 generation) const {
	return generation
		&& (lane.generation == generation)
		&& (lane.activeGeneration == generation)
		&& lane.state.current().pending;
}

template <typename Value>
void Onramp::apply(Lane<Value> &lane, uint64 generation, Value value) {
	if (!active(lane, generation)) {
		return;
	}
	const auto id = base::take(lane.id);
	lane.activeGeneration = 0;
	if (id) {
		_api.request(id).cancel();
	}
	lane.state = OnrampLoadState<Value>{ .value = std::move(value) };
}

template <typename Value>
void Onramp::fail(
		Lane<Value> &lane,
		uint64 generation,
		OnrampError error) {
	if (!active(lane, generation)) {
		return;
	}
	const auto id = base::take(lane.id);
	lane.activeGeneration = 0;
	if (id) {
		_api.request(id).cancel();
	}
	lane.state = OnrampLoadState<Value>{ .error = std::move(error) };
}

template <typename Value>
void Onramp::finishSend(
		Lane<Value> &lane,
		uint64 generation,
		mtpRequestId id) {
	if (active(lane, generation)) {
		lane.id = id;
	} else if (id) {
		_api.request(id).cancel();
	}
}

template <typename Value>
void Onramp::cancel(Lane<Value> &lane, Method method) {
	const auto generation = base::take(lane.activeGeneration);
	const auto id = base::take(lane.id);
	if (id) {
		_api.request(id).cancel();
	}
#ifdef _DEBUG
	debugObserveCancel(method, generation);
#endif // _DEBUG
}

} // namespace Wallet
