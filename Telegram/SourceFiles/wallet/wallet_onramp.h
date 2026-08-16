/*
This file is part of Telegram Desktop,
the official desktop application for the Telegram messaging service.

For license and copyright information please follow this link:
https://github.com/telegramdesktop/tdesktop/blob/master/LEGAL
*/
#pragma once

#include "base/weak_ptr.h"
#include "mtproto/sender.h"

namespace Main {
class Session;
} // namespace Main

namespace Wallet {

struct OnrampProvider {
	QString id;
	QString name;
	std::vector<QString> cryptoCurrencies;
	bool supportsBaseCurrencies = false;
	bool supportsLimits = false;
	bool supportsQuote = false;

	friend bool operator==(
		const OnrampProvider &,
		const OnrampProvider &) = default;
};

struct OnrampMethod {
	QString paymentMethod;
	bool available = false;

	friend bool operator==(
		const OnrampMethod &,
		const OnrampMethod &) = default;
};

struct OnrampAvailability {
	bool allowed = false;
	bool buyAllowed = false;
	QString countryCode;
	std::optional<QString> state;
	std::vector<OnrampMethod> methods;

	friend bool operator==(
		const OnrampAvailability &,
		const OnrampAvailability &) = default;
};

struct OnrampRouteSelection {
	QString provider;
	std::optional<QString> paymentMethod;
	QString baseCurrency;

	friend bool operator==(
		const OnrampRouteSelection &,
		const OnrampRouteSelection &) = default;
};

struct OnrampHostedSession {
	QString provider;
	QString sessionId;
	QString url;
	TimeId expiresDate = 0;

	friend bool operator==(
		const OnrampHostedSession &,
		const OnrampHostedSession &) = default;
};

struct OnrampError {
	int code = 0;
	QString type;

	friend bool operator==(
		const OnrampError &,
		const OnrampError &) = default;
};

template <typename Value>
struct OnrampLoadState {
	std::optional<Value> value;
	std::optional<OnrampError> error;
	bool pending = false;

	friend bool operator==(
		const OnrampLoadState &,
		const OnrampLoadState &) = default;
};

struct OnrampSessionArgs {
	QString provider;
	QString address;
	std::optional<QString> paymentMethod;
	std::optional<QString> baseCurrency;
	std::optional<QString> baseAmount;
	std::optional<QString> memo;
	std::optional<QString> theme;
	std::optional<QString> successReturnUrl;
	std::optional<QString> failReturnUrl;

	friend bool operator==(
		const OnrampSessionArgs &,
		const OnrampSessionArgs &) = default;
};

#ifdef _DEBUG
enum class DebugOnrampMethod {
	Providers,
	BaseCurrencies,
	Availability,
	CreateSession,
};

enum class DebugOnrampEvent {
	Send,
	Cancel,
};

struct DebugOnrampRequest {
	DebugOnrampEvent event = DebugOnrampEvent::Send;
	DebugOnrampMethod method = DebugOnrampMethod::Providers;
	uint64 generation = 0;
	uint32 flags = 0;
	std::optional<QString> provider;
	std::optional<QString> cryptoCurrency;
	std::optional<QString> address;
	std::optional<QString> paymentMethod;
	std::optional<QString> baseCurrency;
	std::optional<QString> baseAmount;
	std::optional<QString> memo;
	std::optional<QString> theme;
	std::optional<QString> successReturnUrl;
	std::optional<QString> failReturnUrl;
};
#endif // _DEBUG

class Onramp final : public base::has_weak_ptr {
public:
	using ProvidersState = OnrampLoadState<std::vector<OnrampProvider>>;
	using BaseCurrenciesState = OnrampLoadState<std::vector<QString>>;
	using AvailabilityState = OnrampLoadState<OnrampAvailability>;
	using HostedSessionState = OnrampLoadState<OnrampHostedSession>;

	explicit Onramp(not_null<Main::Session*> session);
	~Onramp();

	Onramp(const Onramp &) = delete;
	Onramp &operator=(const Onramp &) = delete;

	[[nodiscard]] ProvidersState providersCurrent() const;
	[[nodiscard]] rpl::producer<ProvidersState> providersValue() const;
	[[nodiscard]] BaseCurrenciesState baseCurrenciesCurrent() const;
	[[nodiscard]] auto baseCurrenciesValue() const
		-> rpl::producer<BaseCurrenciesState>;
	[[nodiscard]] AvailabilityState availabilityCurrent() const;
	[[nodiscard]] rpl::producer<AvailabilityState> availabilityValue() const;
	[[nodiscard]] HostedSessionState hostedSessionCurrent() const;
	[[nodiscard]] rpl::producer<HostedSessionState> hostedSessionValue() const;
	[[nodiscard]] std::optional<OnrampRouteSelection> lastRouteSelection() const;
	[[nodiscard]] rpl::producer<OnrampRouteSelection> routeSelections() const;

	void selectRoute(OnrampRouteSelection selection);

	void requestProvidersForGram();
	void requestAllProviders();
	void requestBaseCurrencies(const QString &provider);
	void requestAvailability(
		const QString &provider,
		std::optional<QString> baseCurrency = std::nullopt);
	void createSession(const OnrampSessionArgs &args);

#ifdef _DEBUG
	void setDebugRequestInterceptor(
		Fn<bool(const DebugOnrampRequest &)> interceptor);
	void debugResolve(
		uint64 generation,
		const MTPVector<MTPOnrampProviderInfo> &result);
	void debugResolve(
		uint64 generation,
		const MTPVector<MTPstring> &result);
	void debugResolve(
		uint64 generation,
		const MTPOnrampAvailability &result);
	void debugResolve(
		uint64 generation,
		const MTPOnrampSession &result);
	void debugFail(
		DebugOnrampMethod method,
		uint64 generation,
		int code,
		QString type);
#endif // _DEBUG

private:
	enum class Method {
		Providers,
		BaseCurrencies,
		Availability,
		CreateSession,
	};

	template <typename Value>
	struct Lane {
		mtpRequestId id = 0;
		uint64 generation = 0;
		uint64 activeGeneration = 0;
		rpl::variable<OnrampLoadState<Value>> state;
	};

	template <typename Value>
	[[nodiscard]] uint64 begin(Lane<Value> &lane, Method method);
	template <typename Value>
	[[nodiscard]] bool active(
		const Lane<Value> &lane,
		uint64 generation) const;
	template <typename Value>
	void apply(Lane<Value> &lane, uint64 generation, Value value);
	template <typename Value>
	void fail(Lane<Value> &lane, uint64 generation, OnrampError error);
	template <typename Value>
	void finishSend(
		Lane<Value> &lane,
		uint64 generation,
		mtpRequestId id);
	template <typename Value>
	void cancel(Lane<Value> &lane, Method method);

#ifdef _DEBUG
	[[nodiscard]] static DebugOnrampMethod DebugMethod(Method method);
	template <typename Value>
	[[nodiscard]] bool debugIntercept(
		Lane<Value> &lane,
		uint64 generation,
		const DebugOnrampRequest &request);
	void debugObserveCancel(
		Method method,
		uint64 generation) const;
#endif // _DEBUG

	Lane<std::vector<OnrampProvider>> _providers;
	Lane<std::vector<QString>> _baseCurrencies;
	Lane<OnrampAvailability> _availability;
	Lane<OnrampHostedSession> _hostedSession;
	std::optional<OnrampRouteSelection> _lastRouteSelection;
	rpl::event_stream<OnrampRouteSelection> _routeSelections;
#ifdef _DEBUG
	Fn<bool(const DebugOnrampRequest &)> _debugRequestInterceptor;
#endif // _DEBUG
	bool _destroying = false;
	MTP::Sender _api;

};

} // namespace Wallet
