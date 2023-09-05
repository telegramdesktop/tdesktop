/*
This file is part of rabbitGram Desktop,
the unofficial app based on Telegram Desktop.

For license and copyright information please follow this link:
https://github.com/rabbitGramDesktop/rabbitGramDesktop/blob/dev/LEGAL
*/
#pragma once

namespace ExteraLang {
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
} // namespace ExteraLang

// Shorthands

inline QString ktr(
	const QString &key,
	::ExteraLang::Lang::Var var1 = ::ExteraLang::Lang::Var(),
	::ExteraLang::Lang::Var var2 = ::ExteraLang::Lang::Var(),
	::ExteraLang::Lang::Var var3 = ::ExteraLang::Lang::Var(),
	::ExteraLang::Lang::Var var4 = ::ExteraLang::Lang::Var()) {
	return ::ExteraLang::Lang::Translate(key, var1, var2, var3, var4);
}

inline QString ktr(
	const QString &key,
	float64 value,
	::ExteraLang::Lang::Var var1 = ::ExteraLang::Lang::Var(),
	::ExteraLang::Lang::Var var2 = ::ExteraLang::Lang::Var(),
	::ExteraLang::Lang::Var var3 = ::ExteraLang::Lang::Var(),
	::ExteraLang::Lang::Var var4 = ::ExteraLang::Lang::Var()) {
	return ::ExteraLang::Lang::Translate(key, value, var1, var2, var3, var4);
}

inline TextWithEntities ktre(
	const QString &key,
	::ExteraLang::Lang::EntVar var1 = ::ExteraLang::Lang::EntVar(),
	::ExteraLang::Lang::EntVar var2 = ::ExteraLang::Lang::EntVar(),
	::ExteraLang::Lang::EntVar var3 = ::ExteraLang::Lang::EntVar(),
	::ExteraLang::Lang::EntVar var4 = ::ExteraLang::Lang::EntVar()) {
	return ::ExteraLang::Lang::TranslateWithEntities(key, var1, var2, var3, var4);
}

inline TextWithEntities ktre(
	const QString &key,
	float64 value,
	::ExteraLang::Lang::EntVar var1 = ::ExteraLang::Lang::EntVar(),
	::ExteraLang::Lang::EntVar var2 = ::ExteraLang::Lang::EntVar(),
	::ExteraLang::Lang::EntVar var3 = ::ExteraLang::Lang::EntVar(),
	::ExteraLang::Lang::EntVar var4 = ::ExteraLang::Lang::EntVar()) {
	return ::ExteraLang::Lang::TranslateWithEntities(key, value, var1, var2, var3, var4);
}

inline rpl::producer<QString> rktr(
	const QString &key,
	::ExteraLang::Lang::Var var1 = ::ExteraLang::Lang::Var(),
	::ExteraLang::Lang::Var var2 = ::ExteraLang::Lang::Var(),
	::ExteraLang::Lang::Var var3 = ::ExteraLang::Lang::Var(),
	::ExteraLang::Lang::Var var4 = ::ExteraLang::Lang::Var()) {
	return rpl::single(
			::ExteraLang::Lang::Translate(key, var1, var2, var3, var4)
		) | rpl::then(
			::ExteraLang::Lang::Events() | rpl::map(
				[=]{ return ::ExteraLang::Lang::Translate(key, var1, var2, var3, var4); })
		);
}

inline rpl::producer<QString> rktr(
	const QString &key,
	float64 value,
	::ExteraLang::Lang::Var var1 = ::ExteraLang::Lang::Var(),
	::ExteraLang::Lang::Var var2 = ::ExteraLang::Lang::Var(),
	::ExteraLang::Lang::Var var3 = ::ExteraLang::Lang::Var(),
	::ExteraLang::Lang::Var var4 = ::ExteraLang::Lang::Var()) {
	return rpl::single(
			::ExteraLang::Lang::Translate(key, value, var1, var2, var3, var4)
		) | rpl::then(
			::ExteraLang::Lang::Events() | rpl::map(
				[=]{ return ::ExteraLang::Lang::Translate(key, value, var1, var2, var3, var4); })
		);
}

inline rpl::producer<TextWithEntities> rktre(
	const QString &key,
	::ExteraLang::Lang::EntVar var1 = ::ExteraLang::Lang::EntVar(),
	::ExteraLang::Lang::EntVar var2 = ::ExteraLang::Lang::EntVar(),
	::ExteraLang::Lang::EntVar var3 = ::ExteraLang::Lang::EntVar(),
	::ExteraLang::Lang::EntVar var4 = ::ExteraLang::Lang::EntVar()) {
	return rpl::single(
			::ExteraLang::Lang::TranslateWithEntities(key, var1, var2, var3, var4)
		) | rpl::then(
			::ExteraLang::Lang::Events() | rpl::map(
				[=]{ return ::ExteraLang::Lang::TranslateWithEntities(key, var1, var2, var3, var4); })
		);
}

inline rpl::producer<TextWithEntities> rktre(
	const QString &key,
	float64 value,
	::ExteraLang::Lang::EntVar var1 = ::ExteraLang::Lang::EntVar(),
	::ExteraLang::Lang::EntVar var2 = ::ExteraLang::Lang::EntVar(),
	::ExteraLang::Lang::EntVar var3 = ::ExteraLang::Lang::EntVar(),
	::ExteraLang::Lang::EntVar var4 = ::ExteraLang::Lang::EntVar()) {
	return rpl::single(
			::ExteraLang::Lang::TranslateWithEntities(key, value, var1, var2, var3, var4)
		) | rpl::then(
			::ExteraLang::Lang::Events() | rpl::map(
				[=]{ return ::ExteraLang::Lang::TranslateWithEntities(key, value, var1, var2, var3, var4); })
		);
}