/*
This file is part of Telegram Desktop,
the official desktop application for the Telegram messaging service.

For license and copyright information please follow this link:
https://github.com/telegramdesktop/tdesktop/blob/master/LEGAL
*/
#pragma once

#include "mtproto/sender.h"

namespace Main {

class Account;

class AppConfig final {
public:
	explicit AppConfig(not_null<Account*> account);

	template <typename Type>
	[[nodiscard]] Type get(const QString &key, Type fallback) const {
		if constexpr (std::is_same_v<Type, double>) {
			return getDouble(key, fallback);
		} else if constexpr (std::is_same_v<Type, QString>) {
			return getString(key, fallback);
		} else if constexpr (std::is_same_v<Type, std::vector<QString>>) {
			return getStringArray(key, std::move(fallback));
		} else if constexpr (std::is_same_v<Type, bool>) {
			return getBool(key, fallback);
		}
	}

	[[nodiscard]] rpl::producer<> refreshed() const;
	[[nodiscard]] rpl::producer<> value() const;

	[[nodiscard]] bool suggestionCurrent(const QString &key) const;
	[[nodiscard]] rpl::producer<> suggestionRequested(
		const QString &key) const;
	void dismissSuggestion(const QString &key);

	void refresh();

private:
	void refreshDelayed();

	template <typename Extractor>
	[[nodiscard]] auto getValue(
		const QString &key,
		Extractor &&extractor) const;

	[[nodiscard]] bool getBool(
		const QString &key,
		bool fallback) const;
	[[nodiscard]] double getDouble(
		const QString &key,
		double fallback) const;
	[[nodiscard]] QString getString(
		const QString &key,
		const QString &fallback) const;
	[[nodiscard]] std::vector<QString> getStringArray(
		const QString &key,
		std::vector<QString> &&fallback) const;

	const not_null<Account*> _account;
	std::optional<MTP::Sender> _api;
	mtpRequestId _requestId = 0;
	base::flat_map<QString, MTPJSONValue> _data;
	rpl::event_stream<> _refreshed;
	base::flat_set<QString> _dismissedSuggestions;
	rpl::lifetime _lifetime;

};

} // namespace Main
