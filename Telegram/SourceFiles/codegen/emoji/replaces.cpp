/*
This file is part of Telegram Desktop,
the official desktop application for the Telegram messaging service.

For license and copyright information please follow this link:
https://github.com/telegramdesktop/tdesktop/blob/master/LEGAL
*/
#include "codegen/emoji/replaces.h"

#include "codegen/emoji/data.h"
#include <QtCore/QFile>
#include <QtCore/QJsonDocument>
#include <QtCore/QJsonObject>
#include <QtCore/QRegularExpression>

namespace codegen {
namespace emoji {
namespace {

constexpr auto kErrorBadReplaces = 402;

common::LogStream logReplacesError(const QString &filename) {
	return common::logError(kErrorBadReplaces, filename) << "Bad data: ";
}

auto RegExpCode = QRegularExpression("^:[\\+\\-a-z0-9_]+:$");
auto RegExpTone = QRegularExpression("_tone[0-9]");
auto RegExpHex = QRegularExpression("^[0-9a-f]+$");

class ReplacementWords {
public:
	ReplacementWords(const QString &string);
	QVector<QString> result() const;

private:
	friend ReplacementWords operator+(const ReplacementWords &a, const ReplacementWords &b);

	QMap<QString, int> wordsWithCounts_;

};

ReplacementWords::ReplacementWords(const QString &string) {
	auto feedWord = [this](QString &word) {
		if (!word.isEmpty()) {
			++wordsWithCounts_[word];
			word.clear();
		}
	};
	// Split by all non-letters-or-numbers.
	// Leave '-' and '+' inside a word only if they're followed by a number.
	auto word = QString();
	for (auto i = string.cbegin(), e = string.cend(); i != e; ++i) {
		if (i->isLetterOrNumber()) {
			word.append(*i);
			continue;
		} else if (*i == '-' || *i == '+') {
			if (i + 1 != e && (i + 1)->isNumber()) {
				word.append(*i);
				continue;
			}
		}
		feedWord(word);
	}
	feedWord(word);
}

QVector<QString> ReplacementWords::result() const {
	auto result = QVector<QString>();
	for (auto i = wordsWithCounts_.cbegin(), e = wordsWithCounts_.cend(); i != e; ++i) {
		for (auto j = 0, count = i.value(); j != count; ++j) {
			result.push_back(i.key());
		}
	}
	return result;
}

ReplacementWords operator+(const ReplacementWords &a, const ReplacementWords &b) {
	ReplacementWords result = a;
	for (auto i = b.wordsWithCounts_.cbegin(), e = b.wordsWithCounts_.cend(); i != e; ++i) {
		auto j = result.wordsWithCounts_.constFind(i.key());
		if (j == result.wordsWithCounts_.cend() || j.value() < i.value()) {
			result.wordsWithCounts_[i.key()] = i.value();
		}
	}
	return result;
}

bool AddReplacement(Replaces &result, const Id &id, const QString &replacement, const QString &name) {
	auto replace = Replace();
	replace.id = id;
	replace.replacement = replacement;
	replace.words = (ReplacementWords(replacement)).result();// + ReplacementWords(name)).result();
	if (replace.words.isEmpty()) {
		logReplacesError(result.filename) << "Child '" << replacement.toStdString() << "' has no words.";
		return false;
	}
	result.list.push_back(replace);
	return true;
}

QString ComposeString(const std::initializer_list<QChar> &chars) {
	auto result = QString();
	result.reserve(chars.size());
	for (auto ch : chars) {
		result.append(ch);
	}
	return result;
}

const auto NotSupported = ([] {
	auto result = QSet<QString>();
	auto insert = [&result](auto... args) {
		result.insert(ComposeString({ args... }));
	};
	insert(0x0023, 0xFE0F); // :pound_symbol:
	insert(0x002A, 0xFE0F); // :asterisk_symbol:
	for (auto i = 0; i != 10; ++i) {
		insert(0x0030 + i, 0xFE0F); // :digit_zero: ... :digit_nine:
	}
	for (auto i = 0; i != 5; ++i) {
		insert(0xD83C, 0xDFFB + i); // :tone1: ... :tone5:
	}
	for (auto i = 0; i != 26; ++i) {
		insert(0xD83C, 0xDDE6 + i); // :regional_indicator_a: ... :regional_indicator_z:
	}
	insert(0xD83C, 0xDDFA, 0xD83C, 0xDDF3); // :united_nations:

	insert(0xD83C, 0xDFF4, 0xDB40, 0xDC67, 0xDB40, 0xDC62, 0xDB40, 0xDC65, 0xDB40, 0xDC6E, 0xDB40, 0xDC67, 0xDB40, 0xDC7F); // :england:
	insert(0xD83C, 0xDFF4, 0xDB40, 0xDC67, 0xDB40, 0xDC62, 0xDB40, 0xDC73, 0xDB40, 0xDC63, 0xDB40, 0xDC74, 0xDB40, 0xDC7F); // :scotland:
	insert(0xD83C, 0xDFF4, 0xDB40, 0xDC67, 0xDB40, 0xDC62, 0xDB40, 0xDC77, 0xDB40, 0xDC6C, 0xDB40, 0xDC73, 0xDB40, 0xDC7F); // :wales:

	insert(0xD83D, 0xDEF7); // :sled:
	insert(0xD83D, 0xDEF8); // :flying_saucer:
	insert(0xD83E, 0xDD1F); // :love_you_gesture:
	insert(0xD83E, 0xDD28); // :face_with_raised_eyebrow:
	insert(0xD83E, 0xDD29); // :star_struck:
	insert(0xD83E, 0xDD2A); // :crazy_face:
	insert(0xD83E, 0xDD2B); // :shushing_face:
	insert(0xD83E, 0xDD2C); // :face_with_symbols_over_mouth:
	insert(0xD83E, 0xDD2D); // :face_with_hand_over_mouth:
	insert(0xD83E, 0xDD2E); // :face_vomiting:
	insert(0xD83E, 0xDD2F); // :exploding_head:
	insert(0xD83E, 0xDD31); // :breast_feeding:
	insert(0xD83E, 0xDD32); // :palms_up_together:
	insert(0xD83E, 0xDD4C); // :curling_stone:
	insert(0xD83E, 0xDD5F); // :dumpling:
	insert(0xD83E, 0xDD60); // :fortune_cookie:
	insert(0xD83E, 0xDD61); // :takeout_box:
	insert(0xD83E, 0xDD62); // :chopsticks:
	insert(0xD83E, 0xDD63); // :bowl_with_spoon:
	insert(0xD83E, 0xDD64); // :cup_with_straw:
	insert(0xD83E, 0xDD65); // :coconut:
	insert(0xD83E, 0xDD66); // :broccoli:
	insert(0xD83E, 0xDD67); // :pie:
	insert(0xD83E, 0xDD68); // :pretzel:
	insert(0xD83E, 0xDD69); // :cut_of_meat:
	insert(0xD83E, 0xDD6A); // :sandwich:
	insert(0xD83E, 0xDD6B); // :canned_food:
	insert(0xD83E, 0xDD92); // :giraffe:
	insert(0xD83E, 0xDD93); // :zebra:
	insert(0xD83E, 0xDD94); // :hedgehog:
	insert(0xD83E, 0xDD95); // :sauropod:
	insert(0xD83E, 0xDD96); // :t_rex:
	insert(0xD83E, 0xDD97); // :cricket:
	insert(0xD83E, 0xDDD0); // :face_with_monocle:
	insert(0xD83E, 0xDDD1); // :adult:
	insert(0xD83E, 0xDDD2); // :child:
	insert(0xD83E, 0xDDD3); // :older_adult:
	insert(0xD83E, 0xDDD4); // :bearded_person:
	insert(0xD83E, 0xDDD5); // :woman_with_headscarf:
	insert(0xD83E, 0xDDD6); // :person_in_steamy_room:
	insert(0xD83E, 0xDDD6, 0x200D, 0x2640, 0xFE0F); // :woman_in_steamy_room:
	insert(0xD83E, 0xDDD6, 0x200D, 0x2642, 0xFE0F); // :man_in_steamy_room:
	insert(0xD83E, 0xDDD7); // :person_climbing:
	insert(0xD83E, 0xDDD7, 0x200D, 0x2640, 0xFE0F); // :woman_climbing:
	insert(0xD83E, 0xDDD7, 0x200D, 0x2642, 0xFE0F); // :man_climbing:
	insert(0xD83E, 0xDDD8); // :person_in_lotus_position:
	insert(0xD83E, 0xDDD8, 0x200D, 0x2640, 0xFE0F); // :woman_in_lotus_position:
	insert(0xD83E, 0xDDD8, 0x200D, 0x2642, 0xFE0F); // :man_in_lotus_position:
	insert(0xD83E, 0xDDD9); // :mage:
	insert(0xD83E, 0xDDD9, 0x200D, 0x2640, 0xFE0F); // :woman_mage:
	insert(0xD83E, 0xDDD9, 0x200D, 0x2642, 0xFE0F); // :man_mage:
	insert(0xD83E, 0xDDDA); // :fairy:
	insert(0xD83E, 0xDDDA, 0x200D, 0x2640, 0xFE0F); // :woman_fairy:
	insert(0xD83E, 0xDDDA, 0x200D, 0x2642, 0xFE0F); // :man_fairy:
	insert(0xD83E, 0xDDDB); // :vampire:
	insert(0xD83E, 0xDDDB, 0x200D, 0x2640, 0xFE0F); // :woman_vampire:
	insert(0xD83E, 0xDDDB, 0x200D, 0x2642, 0xFE0F); // :man_vampire:
	insert(0xD83E, 0xDDDC); // :merperson:
	insert(0xD83E, 0xDDDC, 0x200D, 0x2640, 0xFE0F); // :mermaid:
	insert(0xD83E, 0xDDDC, 0x200D, 0x2642, 0xFE0F); // :merman:
	insert(0xD83E, 0xDDDD); // :elf:
	insert(0xD83E, 0xDDDD, 0x200D, 0x2640, 0xFE0F); // :woman_elf:
	insert(0xD83E, 0xDDDD, 0x200D, 0x2642, 0xFE0F); // :man_elf:
	insert(0xD83E, 0xDDDE); // :genie:
	insert(0xD83E, 0xDDDE, 0x200D, 0x2640, 0xFE0F); // :woman_genie:
	insert(0xD83E, 0xDDDE, 0x200D, 0x2642, 0xFE0F); // :man_genie:
	insert(0xD83E, 0xDDDF); // :zombie:
	insert(0xD83E, 0xDDDF, 0x200D, 0x2640, 0xFE0F); // :woman_zombie:
	insert(0xD83E, 0xDDDF, 0x200D, 0x2642, 0xFE0F); // :man_zombie:
	insert(0xD83E, 0xDDE0); // :brain:
	insert(0xD83E, 0xDDE1); // :orange_heart:
	insert(0xD83E, 0xDDE2); // :billed_cap:
	insert(0xD83E, 0xDDE3); // :scarf:
	insert(0xD83E, 0xDDE4); // :gloves:
	insert(0xD83E, 0xDDE5); // :coat:
	insert(0xD83E, 0xDDE6); // :socks:

	insert(0x23CF, 0xFE0F); // :eject:

	insert(0x2640, 0xFE0F); // :female_sign:
	insert(0x2642, 0xFE0F); // :male_sign:
	insert(0x2695, 0xFE0F); // :medical_symbol:

	return result;
})();

const auto ConvertMap = ([] {
	auto result = QMap<QString, QString>();
	auto insert = [&result](const std::initializer_list<QChar> &from, const std::initializer_list<QChar> &to) {
		result.insert(ComposeString(from), ComposeString(to));
	};
	auto insertWithAdd = [&result](const std::initializer_list<QChar> &from, const QString &added) {
		auto code = ComposeString(from);
		result.insert(code, code + added);
	};
	auto maleModifier = ComposeString({ 0x200D, 0x2642, 0xFE0F });
	auto femaleModifier = ComposeString({ 0x200D, 0x2640, 0xFE0F });
	insertWithAdd({ 0xD83E, 0xDD26 }, maleModifier);
	insertWithAdd({ 0xD83E, 0xDD37 }, femaleModifier);
	insertWithAdd({ 0xD83E, 0xDD38 }, maleModifier);
	insertWithAdd({ 0xD83E, 0xDD39 }, maleModifier);
	insertWithAdd({ 0xD83E, 0xDD3C }, maleModifier);
	insertWithAdd({ 0xD83E, 0xDD3D }, maleModifier);
	insertWithAdd({ 0xD83E, 0xDD3E }, femaleModifier);

	// :kiss_woman_man:
	insert({ 0xD83D, 0xDC69, 0x200D, 0x2764, 0xFE0F, 0x200D, 0xD83D, 0xDC8B, 0x200D, 0xD83D, 0xDC68 }, { 0xD83D, 0xDC8F });

	// :family_man_woman_boy:
	insert({ 0xD83D, 0xDC68, 0x200D, 0xD83D, 0xDC69, 0x200D, 0xD83D, 0xDC66 }, { 0xD83D, 0xDC6A });

	// :couple_with_heart_woman_man:
	insert({ 0xD83D, 0xDC69, 0x200D, 0x2764, 0xFE0F, 0x200D, 0xD83D, 0xDC68 }, { 0xD83D, 0xDC91 });

	auto insertFlag = [insert](char ch1, char ch2, char ch3, char ch4) {
		insert({ 0xD83C, 0xDDE6 + (ch1 - 'a'), 0xD83C, 0xDDe6 + (ch2 - 'a') }, { 0xD83C, 0xDDE6 + (ch3 - 'a'), 0xD83C, 0xDDe6 + (ch4 - 'a') });
	};
	insertFlag('a', 'c', 's', 'h');
	insertFlag('b', 'v', 'n', 'o');
	insertFlag('c', 'p', 'f', 'r');
	insertFlag('d', 'g', 'i', 'o');
	insertFlag('e', 'a', 'e', 's');
	insertFlag('h', 'm', 'a', 'u');
	insertFlag('m', 'f', 'f', 'r');
	insertFlag('s', 'j', 'n', 'o');
	insertFlag('t', 'a', 's', 'h');
	insertFlag('u', 'm', 'u', 's');

	return result;
})();

// Empty string result means we should skip this one.
QString ConvertEmojiId(const Id &id, const QString &replacement) {
	if (RegExpTone.match(replacement).hasMatch()) {
		return QString();
	}
	if (NotSupported.contains(id)) {
		return QString();
	}
	return ConvertMap.value(id, id);
}

} // namespace

Replaces PrepareReplaces(const QString &filename) {
	auto result = Replaces(filename);
	auto content = ([filename] {
		QFile f(filename);
		return f.open(QIODevice::ReadOnly) ? f.readAll() : QByteArray();
	})();
	if (content.isEmpty()) {
		logReplacesError(filename) << "Could not read data.";
		return result;
	}
	auto error = QJsonParseError();
	auto document = QJsonDocument::fromJson(content, &error);
	if (error.error != QJsonParseError::NoError) {
		logReplacesError(filename) << "Could not parse data (" << int(error.error) << "): " << error.errorString().toStdString();
		return result;
	}
	if (!document.isObject()) {
		logReplacesError(filename) << "Root object not found.";
		return result;
	}
	auto list = document.object();
	for (auto i = list.constBegin(), e = list.constEnd(); i != e; ++i) {
		if (!(*i).isObject()) {
			logReplacesError(filename) << "Child object not found.";
			return Replaces(filename);
		}
		auto childKey = i.key();
		auto child = (*i).toObject();
		auto failed = false;
		auto getString = [filename, childKey, &child, &failed](const QString &key) {
			auto it = child.constFind(key);
			if (it == child.constEnd() || !(*it).isString()) {
				logReplacesError(filename) << "Child '" << childKey.toStdString() << "' field not found: " << key.toStdString();
				failed = true;
				return QString();
			}
			return (*it).toString();
		};
		auto idParts = getString("output").split('-');
		auto name = getString("name");
		auto replacement = getString("alpha_code");
		auto aliases = getString("aliases").split('|');
		const auto Exceptions = { ":shrug:" };
		for (const auto &exception : Exceptions) {
			const auto index = aliases.indexOf(exception);
			if (index >= 0) {
				aliases.removeAt(index);
			}
		}
		if (aliases.size() == 1 && aliases[0].isEmpty()) {
			aliases.clear();
		}
		if (failed) {
			return Replaces(filename);
		}
		if (!RegExpCode.match(replacement).hasMatch()) {
			logReplacesError(filename) << "Child '" << childKey.toStdString() << "' alpha_code invalid: " << replacement.toStdString();
			return Replaces(filename);
		}
		for (auto &alias : aliases) {
			if (!RegExpCode.match(alias).hasMatch()) {
				logReplacesError(filename) << "Child '" << childKey.toStdString() << "' alias invalid: " << alias.toStdString();
				return Replaces(filename);
			}
		}
		auto id = Id();
		for (auto &idPart : idParts) {
			auto ok = true;
			auto utf32 = idPart.toInt(&ok, 0x10);
			if (!ok || !RegExpHex.match(idPart).hasMatch()) {
				logReplacesError(filename) << "Child '" << childKey.toStdString() << "' output part invalid: " << idPart.toStdString();
				return Replaces(filename);
			}
			if (utf32 >= 0 && utf32 < 0x10000) {
				auto ch = QChar(ushort(utf32));
				if (ch.isLowSurrogate() || ch.isHighSurrogate()) {
					logReplacesError(filename) << "Child '" << childKey.toStdString() << "' output part invalid: " << idPart.toStdString();
					return Replaces(filename);
				}
				id.append(ch);
			} else if (utf32 >= 0x10000 && utf32 <= 0x10FFFF) {
				auto hi = ((utf32 - 0x10000) / 0x400) + 0xD800;
				auto lo = ((utf32 - 0x10000) % 0x400) + 0xDC00;
				id.append(QChar(ushort(hi)));
				id.append(QChar(ushort(lo)));
			} else {
				logReplacesError(filename) << "Child '" << childKey.toStdString() << "' output part invalid: " << idPart.toStdString();
				return Replaces(filename);
			}
		}
		id = ConvertEmojiId(id, replacement);
		if (id.isEmpty()) {
			continue;
		}
		if (!AddReplacement(result, id, replacement, name)) {
			return Replaces(filename);
		}
		for (auto &alias : aliases) {
			if (!AddReplacement(result, id, alias, name)) {
				return Replaces(filename);
			}
		}
	}
	if (!AddReplacement(result, ComposeString({ 0xD83D, 0xDC4D }), ":like:", "thumbs up")) {
		return Replaces(filename);
	}
	if (!AddReplacement(result, ComposeString({ 0xD83D, 0xDC4E }), ":dislike:", "thumbs down")) {
		return Replaces(filename);
	}
	if (!AddReplacement(result, ComposeString({ 0xD83E, 0xDD14 }), ":hmm:", "thinking")) {
		return Replaces(filename);
	}
	return result;
}

bool CheckAndConvertReplaces(Replaces &replaces, const Data &data) {
	auto result = Replaces(replaces.filename);
	auto sorted = QMap<Id, Replace>();
	auto findId = [&data](const Id &id) {
		return data.map.find(id) != data.map.cend();
	};
	auto findAndSort = [findId, &data, &sorted](Id id, const Replace &replace) {
		if (!findId(id)) {
			id.replace(QChar(0xFE0F), QString());
			if (!findId(id)) {
				return false;
			}
		}
		auto it = data.map.find(id);
		id = data.list[it->second].id;
		if (data.list[it->second].postfixed) {
			id += QChar(kPostfix);
		}
		auto inserted = sorted.insertMulti(id, replace);
		inserted.value().id = id;
		return true;
	};

	// Find all replaces in data.map, adjust id if necessary.
	// Store all replaces in sorted map to find them fast afterwards.
	auto maleModifier = ComposeString({ 0x200D, 0x2642, 0xFE0F });
	auto femaleModifier = ComposeString({ 0x200D, 0x2640, 0xFE0F });
	for (auto &replace : replaces.list) {
		if (findAndSort(replace.id, replace)) {
			continue;
		}
		if (replace.id.endsWith(maleModifier)) {
			auto defaultId = replace.id.mid(0, replace.id.size() - maleModifier.size());
			if (findAndSort(defaultId, replace)) {
				continue;
			}
		} else if (replace.id.endsWith(femaleModifier)) {
			auto defaultId = replace.id.mid(0, replace.id.size() - femaleModifier.size());
			if (findAndSort(defaultId, replace)) {
				continue;
			}
		} else if (findId(replace.id + maleModifier)) {
			if (findId(replace.id + femaleModifier)) {
				logReplacesError(replaces.filename) << "Replace '" << replace.replacement.toStdString() << "' ambiguous.";
				return false;
			} else {
				findAndSort(replace.id + maleModifier, replace);
				continue;
			}
		} else if (findAndSort(replace.id + femaleModifier, replace)) {
			continue;
		}
		logReplacesError(replaces.filename) << "Replace '" << replace.replacement.toStdString() << "' not found.";
		return false;
	}

	// Go through all categories and put all replaces in order of emoji in categories.
	result.list.reserve(replaces.list.size());
	for (auto &category : data.categories) {
		for (auto index : category) {
			auto id = data.list[index].id;
			if (data.list[index].postfixed) {
				id += QChar(kPostfix);
			}
			for (auto it = sorted.find(id); it != sorted.cend(); sorted.erase(it), it = sorted.find(id)) {
				result.list.push_back(it.value());
			}
		}
	}
	if (result.list.size() != replaces.list.size()) {
		logReplacesError(replaces.filename) << "Some were not found.";
		return false;
	}
	if (!sorted.isEmpty()) {
		logReplacesError(replaces.filename) << "Weird.";
		return false;
	}
	replaces = std::move(result);
	return true;
}

} // namespace emoji
} // namespace codegen
