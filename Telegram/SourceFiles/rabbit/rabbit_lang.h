/*
This file is part of rabbitGram Desktop,
the unofficial app based on Telegram Desktop.

For license and copyright information please follow this link:
https://github.com/rabbitGramDesktop/rabbitGramDesktop/blob/dev/LEGAL
*/
#pragma once

namespace RabbitLang {
namespace Lang {

struct Var {
	Var() {};
	Var(const QString &k, const QString &v) {
		key = k;
		value = v;
	}

	QString key;
	QString value;
};

struct EntVar {
	EntVar() {};
	EntVar(const QString &k, TextWithEntities v) {
		key = k;
		value = v;
	}

	QString key;
	TextWithEntities value;
};

void Load(const QString &baseLangCode, const QString &langCode);

QString Translate(
	const QString &key,
	Var var1 = Var(),
	Var var2 = Var(),
	Var var3 = Var(),
	Var var4 = Var());
QString Translate(
	const QString &key,
	float64 value,
	Var var1 = Var(),
	Var var2 = Var(),
	Var var3 = Var(),
	Var var4 = Var());

TextWithEntities TranslateWithEntities(
	const QString &key,
	EntVar var1 = EntVar(),
	EntVar var2 = EntVar(),
	EntVar var3 = EntVar(),
	EntVar var4 = EntVar());
TextWithEntities TranslateWithEntities(
	const QString &key,
	float64 value,
	EntVar var1 = EntVar(),
	EntVar var2 = EntVar(),
	EntVar var3 = EntVar(),
	EntVar var4 = EntVar());

rpl::producer<> Events();

} // namespace Lang
} // namespace RabbitLang

// Shorthands

inline QString ktr(
	const QString &key,
	::RabbitLang::Lang::Var var1 = ::RabbitLang::Lang::Var(),
	::RabbitLang::Lang::Var var2 = ::RabbitLang::Lang::Var(),
	::RabbitLang::Lang::Var var3 = ::RabbitLang::Lang::Var(),
	::RabbitLang::Lang::Var var4 = ::RabbitLang::Lang::Var()) {
	return ::RabbitLang::Lang::Translate(key, var1, var2, var3, var4);
}

inline QString ktr(
	const QString &key,
	float64 value,
	::RabbitLang::Lang::Var var1 = ::RabbitLang::Lang::Var(),
	::RabbitLang::Lang::Var var2 = ::RabbitLang::Lang::Var(),
	::RabbitLang::Lang::Var var3 = ::RabbitLang::Lang::Var(),
	::RabbitLang::Lang::Var var4 = ::RabbitLang::Lang::Var()) {
	return ::RabbitLang::Lang::Translate(key, value, var1, var2, var3, var4);
}

inline TextWithEntities ktre(
	const QString &key,
	::RabbitLang::Lang::EntVar var1 = ::RabbitLang::Lang::EntVar(),
	::RabbitLang::Lang::EntVar var2 = ::RabbitLang::Lang::EntVar(),
	::RabbitLang::Lang::EntVar var3 = ::RabbitLang::Lang::EntVar(),
	::RabbitLang::Lang::EntVar var4 = ::RabbitLang::Lang::EntVar()) {
	return ::RabbitLang::Lang::TranslateWithEntities(key, var1, var2, var3, var4);
}

inline TextWithEntities ktre(
	const QString &key,
	float64 value,
	::RabbitLang::Lang::EntVar var1 = ::RabbitLang::Lang::EntVar(),
	::RabbitLang::Lang::EntVar var2 = ::RabbitLang::Lang::EntVar(),
	::RabbitLang::Lang::EntVar var3 = ::RabbitLang::Lang::EntVar(),
	::RabbitLang::Lang::EntVar var4 = ::RabbitLang::Lang::EntVar()) {
	return ::RabbitLang::Lang::TranslateWithEntities(key, value, var1, var2, var3, var4);
}

inline rpl::producer<QString> rktr(
	const QString &key,
	::RabbitLang::Lang::Var var1 = ::RabbitLang::Lang::Var(),
	::RabbitLang::Lang::Var var2 = ::RabbitLang::Lang::Var(),
	::RabbitLang::Lang::Var var3 = ::RabbitLang::Lang::Var(),
	::RabbitLang::Lang::Var var4 = ::RabbitLang::Lang::Var()) {
	return rpl::single(
			::RabbitLang::Lang::Translate(key, var1, var2, var3, var4)
		) | rpl::then(
			::RabbitLang::Lang::Events() | rpl::map(
				[=]{ return ::RabbitLang::Lang::Translate(key, var1, var2, var3, var4); })
		);
}

inline rpl::producer<QString> rktr(
	const QString &key,
	float64 value,
	::RabbitLang::Lang::Var var1 = ::RabbitLang::Lang::Var(),
	::RabbitLang::Lang::Var var2 = ::RabbitLang::Lang::Var(),
	::RabbitLang::Lang::Var var3 = ::RabbitLang::Lang::Var(),
	::RabbitLang::Lang::Var var4 = ::RabbitLang::Lang::Var()) {
	return rpl::single(
			::RabbitLang::Lang::Translate(key, value, var1, var2, var3, var4)
		) | rpl::then(
			::RabbitLang::Lang::Events() | rpl::map(
				[=]{ return ::RabbitLang::Lang::Translate(key, value, var1, var2, var3, var4); })
		);
}

inline rpl::producer<TextWithEntities> rktre(
	const QString &key,
	::RabbitLang::Lang::EntVar var1 = ::RabbitLang::Lang::EntVar(),
	::RabbitLang::Lang::EntVar var2 = ::RabbitLang::Lang::EntVar(),
	::RabbitLang::Lang::EntVar var3 = ::RabbitLang::Lang::EntVar(),
	::RabbitLang::Lang::EntVar var4 = ::RabbitLang::Lang::EntVar()) {
	return rpl::single(
			::RabbitLang::Lang::TranslateWithEntities(key, var1, var2, var3, var4)
		) | rpl::then(
			::RabbitLang::Lang::Events() | rpl::map(
				[=]{ return ::RabbitLang::Lang::TranslateWithEntities(key, var1, var2, var3, var4); })
		);
}

inline rpl::producer<TextWithEntities> rktre(
	const QString &key,
	float64 value,
	::RabbitLang::Lang::EntVar var1 = ::RabbitLang::Lang::EntVar(),
	::RabbitLang::Lang::EntVar var2 = ::RabbitLang::Lang::EntVar(),
	::RabbitLang::Lang::EntVar var3 = ::RabbitLang::Lang::EntVar(),
	::RabbitLang::Lang::EntVar var4 = ::RabbitLang::Lang::EntVar()) {
	return rpl::single(
			::RabbitLang::Lang::TranslateWithEntities(key, value, var1, var2, var3, var4)
		) | rpl::then(
			::RabbitLang::Lang::Events() | rpl::map(
				[=]{ return ::RabbitLang::Lang::TranslateWithEntities(key, value, var1, var2, var3, var4); })
		);
}