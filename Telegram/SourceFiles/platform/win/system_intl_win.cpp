/*
This file is part of Telegram Desktop,
the official desktop application for the Telegram messaging service.

For license and copyright information please follow this link:
https://github.com/telegramdesktop/tdesktop/blob/master/LEGAL
*/
#include "platform/platform_system_intl.h"

#include "base/platform/win/base_windows_safe_library.h"
#include "tgw/tgw_intl.h"
#include "twidget/icu_intl.h"
#include "twidget/json.h"

#include <icu.h>

namespace Platform {
namespace {

// Far above tags converting within 157 bytes, far below 48 KB (116 ms).
constexpr auto kMaxLocaleTagSize = std::size_t(1024);

// WHY: until widget-sandbox fixes them, IcuIntl reads unterminated ICU locale
// strings past its buffers, stalls on huge tags, and on a locale keyword's ICU
// error hands BasicIntl dates that its unchecked gmtime_s cannot convert.
class SystemIntl final : public twidget::IcuIntl {
public:
	explicit SystemIntl(const std::string &fallback);

	std::string format(const twidget::Json &request) override;

private:
	SystemIntl(
		const std::string &fallback,
		const std::optional<std::string> &id);

	std::string _dateFallback;

};

[[nodiscard]] bool Overflows(int32_t length, UErrorCode error) {
	return (length >= ULOC_FULLNAME_CAPACITY)
		|| (error == U_STRING_NOT_TERMINATED_WARNING)
		|| (error == U_BUFFER_OVERFLOW_ERROR);
}

[[nodiscard]] std::optional<std::string> LocaleId(const std::string &tag) {
	if (tag.size() > kMaxLocaleTagSize) {
		return std::nullopt;
	} else if (tag.empty()) {
		return std::string();
	}
	auto id = std::array<char, ULOC_FULLNAME_CAPACITY>();
	auto parsed = int32_t(0);
	auto error = U_ZERO_ERROR;
	const auto length = uloc_forLanguageTag(
		tag.c_str(),
		id.data(),
		int32_t(id.size()),
		&parsed,
		&error);
	if (Overflows(length, error)) {
		return std::nullopt;
	} else if (U_FAILURE(error) || parsed <= 0 || !id[0]) {
		return std::string();
	}
	auto converted = std::array<char, ULOC_FULLNAME_CAPACITY>();
	error = U_ZERO_ERROR;
	const auto written = uloc_toLanguageTag(
		id.data(),
		converted.data(),
		int32_t(converted.size()),
		false,
		&error);
	if (Overflows(written, error)) {
		return std::nullopt;
	}
	return std::string(id.data());
}

[[nodiscard]] std::optional<std::string> BaseName(const std::string &id) {
	auto base = std::array<char, ULOC_FULLNAME_CAPACITY>();
	auto error = U_ZERO_ERROR;
	const auto length = uloc_getBaseName(
		id.c_str(),
		base.data(),
		int32_t(base.size()),
		&error);
	if (Overflows(length, error) || U_FAILURE(error)) {
		return std::nullopt;
	}
	return std::string(base.data());
}

[[nodiscard]] std::optional<std::string> BaseTag(const std::string &id) {
	const auto base = BaseName(id);
	if (!base || base->empty()) {
		return base;
	}
	auto tag = std::array<char, ULOC_FULLNAME_CAPACITY>();
	auto error = U_ZERO_ERROR;
	const auto written = uloc_toLanguageTag(
		base->c_str(),
		tag.data(),
		int32_t(tag.size()),
		false,
		&error);
	if (Overflows(written, error) || U_FAILURE(error)) {
		return std::nullopt;
	}
	auto result = std::string(tag.data());
	const auto reparsed = LocaleId(result);
	if (!reparsed || reparsed->empty() || BaseName(*reparsed) != reparsed) {
		return std::nullopt;
	}
	return result;
}

[[nodiscard]] std::optional<std::string> DateLocale(
		const std::optional<std::string> &id,
		const std::string &fallback) {
	if (!id || id->empty()) {
		return fallback;
	}
	auto base = BaseTag(*id);
	if (base && base->empty()) {
		return fallback;
	}
	return base;
}

[[nodiscard]] std::string InvalidDate(const std::string &kind) {
	if (kind != "dateParts") {
		return "Invalid Date";
	}
	auto part = twidget::Json::object();
	part.set("type", "literal").set("value", "Invalid Date");
	auto parts = twidget::Json::array();
	parts.push(std::move(part));
	return parts.dump();
}

SystemIntl::SystemIntl(const std::string &fallback)
: SystemIntl(fallback, LocaleId(fallback)) {
}

SystemIntl::SystemIntl(
	const std::string &fallback,
	const std::optional<std::string> &id)
: IcuIntl(id ? fallback : "en-US")
, _dateFallback(DateLocale(id, "en-US").value_or("en-US")) {
}

std::string SystemIntl::format(const twidget::Json &request) {
	const auto &locale = request["locale"];
	const auto id = LocaleId(locale.isArray()
		? locale[std::size_t(0)].asString()
		: locale.asString());
	const auto &kind = request["kind"].asString();
	const auto dates = std::array{ "date", "dateMethod", "dateParts" };
	if (ranges::contains(dates, kind)
		&& !Tgw::BasicCanConvert(request["value"])) {
		auto tag = DateLocale(id, _dateFallback);
		if (!tag) {
			return InvalidDate(kind);
		}
		auto withBase = request;
		withBase.set("locale", std::move(*tag));
		return IcuIntl::format(withBase);
	} else if (id) {
		return IcuIntl::format(request);
	}
	auto withoutLocale = request;
	withoutLocale.set("locale", twidget::Json());
	return IcuIntl::format(withoutLocale);
}

} // namespace

std::shared_ptr<twidget::IntlProvider> CreateSystemIntl(
		const QString &fallbackLocale) {
	// Never freed: delay-loaded ICU imports and IcuIntl's newer() use it.
	static const auto icu = base::Platform::SafeLoadLibrary(L"icu.dll");
	if (!icu) {
		return nullptr;
	}
	return std::make_shared<SystemIntl>(fallbackLocale.toStdString());
}

} // namespace Platform
