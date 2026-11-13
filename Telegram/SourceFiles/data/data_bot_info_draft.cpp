/*
This file is part of Telegram Desktop,
the official desktop application for the Telegram messaging service.

For license and copyright information please follow this link:
https://github.com/telegramdesktop/tdesktop/blob/master/LEGAL
*/
#include "data/data_bot_info_draft.h"

namespace Data {
namespace {

constexpr auto kNameLimit = 64;
constexpr auto kAboutLimit = 120;
constexpr auto kDescriptionLimit = 512;
constexpr auto kLangCodeLength = 2;

} // namespace

QString &BotInfoTexts::field(BotInfoField field) {
	switch (field) {
	case BotInfoField::Name: return name;
	case BotInfoField::About: return about;
	case BotInfoField::Description: return description;
	}
	Unexpected("Field in BotInfoTexts::field.");
}

const QString &BotInfoTexts::field(BotInfoField field) const {
	return const_cast<BotInfoTexts*>(this)->field(field);
}

bool BotInfoTexts::empty() const {
	return name.isEmpty() && about.isEmpty() && description.isEmpty();
}

std::optional<QString> &BotInfoChange::field(BotInfoField field) {
	switch (field) {
	case BotInfoField::Name: return name;
	case BotInfoField::About: return about;
	case BotInfoField::Description: return description;
	}
	Unexpected("Field in BotInfoChange::field.");
}

const std::optional<QString> &BotInfoChange::field(
		BotInfoField field) const {
	return const_cast<BotInfoChange*>(this)->field(field);
}

bool BotInfoChange::empty() const {
	return !count();
}

int BotInfoChange::count() const {
	return (name ? 1 : 0) + (about ? 1 : 0) + (description ? 1 : 0);
}

bool BotInfoLanguage::fallback() const {
	return code.isEmpty();
}

QString BotInfoLanguage::value(BotInfoField field) const {
	const auto &change = staged.field(field);
	return change ? *change : server.field(field);
}

BotInfoProblem BotInfoLanguage::problem(BotInfoField field) const {
	const auto text = value(field);
	if (BotInfoTextLength(text) > BotInfoFieldLimit(field)) {
		return BotInfoProblem::TooLong;
	} else if (fallback()
		&& (field == BotInfoField::Name)
		&& text.isEmpty()) {
		return BotInfoProblem::EmptyName;
	}
	return BotInfoProblem::None;
}

bool BotInfoLanguage::hasProblem() const {
	return ranges::any_of(kBotInfoFields, [&](BotInfoField field) {
		return problem(field) != BotInfoProblem::None;
	});
}

bool BotInfoLanguage::hasTranslated() const {
	return ranges::contains(translated, true);
}

int BotInfoLanguage::filledCount() const {
	return int(ranges::count_if(kBotInfoFields, [&](BotInfoField field) {
		return !value(field).isEmpty();
	}));
}

int BotInfoFieldLimit(BotInfoField field) {
	switch (field) {
	case BotInfoField::Name: return kNameLimit;
	case BotInfoField::About: return kAboutLimit;
	case BotInfoField::Description: return kDescriptionLimit;
	}
	Unexpected("Field in BotInfoFieldLimit.");
}

int BotInfoTextLength(const QString &text) {
	return int(ranges::count_if(text, [](QChar ch) {
		return !ch.isLowSurrogate();
	}));
}

QString NormalizeBotInfoText(BotInfoField field, QString text) {
	if (field == BotInfoField::Name) {
		text.replace(u'\n', u' ');
	}
	return text.trimmed();
}

bool ValidBotInfoLangCode(const QString &code) {
	return (code.size() == kLangCodeLength)
		&& ranges::all_of(code, [](QChar ch) {
			return (ch >= u'a') && (ch <= u'z');
		});
}

BotInfoDraft::BotInfoDraft()
: _languages(1) {
}

const std::vector<BotInfoLanguage> &BotInfoDraft::languages() const {
	return _languages;
}

const BotInfoLanguage *BotInfoDraft::find(const QString &code) const {
	return const_cast<BotInfoDraft*>(this)->lookup(code);
}

const BotInfoLanguage &BotInfoDraft::fallback() const {
	return _languages.front();
}

BotInfoLanguage *BotInfoDraft::lookup(const QString &code) {
	const auto i = ranges::find(_languages, code, &BotInfoLanguage::code);
	return (i != end(_languages)) ? &*i : nullptr;
}

BotInfoLanguage &BotInfoDraft::ensure(const QString &code) {
	if (const auto found = lookup(code)) {
		return *found;
	}
	_languages.push_back({ .code = code });
	return _languages.back();
}

void BotInfoDraft::add(const QString &code) {
	if (!lookup(code)) {
		_languages.push_back({ .code = code });
		_updates.fire({});
	}
}

void BotInfoDraft::setServer(const QString &code, BotInfoTexts texts) {
	const auto found = lookup(code);
	if (!found && texts.empty()) {
		return;
	}
	auto &language = found ? *found : ensure(code);
	if (found && (language.server == texts)) {
		return;
	}
	language.server = std::move(texts);
	for (const auto field : kBotInfoFields) {
		auto &change = language.staged.field(field);
		if (change && (*change == language.server.field(field))) {
			change = std::nullopt;
			language.translated[int(field)] = false;
		}
	}
	_updates.fire({});
}

void BotInfoDraft::stage(
		const QString &code,
		BotInfoField field,
		QString text,
		bool translated) {
	auto &language = ensure(code);
	auto &change = language.staged.field(field);
	auto normalized = NormalizeBotInfoText(field, std::move(text));
	const auto same = (normalized == language.server.field(field));
	auto updated = same
		? std::optional<QString>()
		: std::make_optional(std::move(normalized));
	const auto mark = translated && !same;
	if ((change == updated) && (language.translated[int(field)] == mark)) {
		return;
	}
	change = std::move(updated);
	language.translated[int(field)] = mark;
	language.error = QString();
	_updates.fire({});
}

void BotInfoDraft::setError(const QString &code, const QString &error) {
	if (const auto language = lookup(code)) {
		if (language->error != error) {
			language->error = error;
			_updates.fire({});
		}
	}
}

void BotInfoDraft::applied(
		const QString &code,
		const BotInfoChange &change) {
	const auto language = lookup(code);
	if (!language) {
		return;
	}
	for (const auto field : kBotInfoFields) {
		const auto &sent = change.field(field);
		if (!sent) {
			continue;
		}
		language->server.field(field) = *sent;
		auto &staged = language->staged.field(field);
		if (staged == sent) {
			staged = std::nullopt;
			language->translated[int(field)] = false;
		}
	}
	language->error = QString();
	_updates.fire({});
}

void BotInfoDraft::discard() {
	if (!changesCount()) {
		return;
	}
	for (auto &language : _languages) {
		language.staged = {};
		language.translated = {};
		language.error = QString();
	}
	_updates.fire({});
}

int BotInfoDraft::changesCount() const {
	return ranges::accumulate(
		_languages,
		0,
		ranges::plus(),
		[](const BotInfoLanguage &language) {
			return language.staged.count();
		});
}

int BotInfoDraft::changedLanguagesCount() const {
	return int(ranges::count_if(
		_languages,
		[](const BotInfoLanguage &language) {
			return !language.staged.empty();
		}));
}

int BotInfoDraft::problemsCount() const {
	return int(ranges::count_if(
		_languages,
		[](const BotInfoLanguage &language) {
			return language.hasProblem() || !language.error.isEmpty();
		}));
}

rpl::producer<> BotInfoDraft::updates() const {
	return _updates.events();
}

} // namespace Data
