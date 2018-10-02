/*
This file is part of Telegram Desktop,
the official desktop application for the Telegram messaging service.

For license and copyright information please follow this link:
https://github.com/telegramdesktop/tdesktop/blob/master/LEGAL
*/
#include "support/support_templates.h"

namespace Support {
namespace details {
namespace {

constexpr auto kQueryLimit = 10;
constexpr auto kWeightStep = 1000;

bool IsTemplatesFile(const QString &file) {
	return file.startsWith(qstr("tl_"), Qt::CaseInsensitive)
		&& file.endsWith(qstr(".txt"), Qt::CaseInsensitive);
}

QString NormalizeQuestion(const QString &question) {
	auto result = QString();
	result.reserve(question.size());
	for (const auto ch : question) {
		if (ch.isLetterOrNumber()) {
			result.append(ch.toLower());
		}
	}
	return result;
}

struct FileResult {
	TemplatesFile result;
	QStringList errors;
};

FileResult ReadFromBlob(const QByteArray &blob) {
	auto result = FileResult();
	const auto lines = blob.split('\n');

	enum class State {
		None,
		Question,
		Keys,
		Value,
		MoreValue,
		Url,
	};
	auto state = State::None;
	QStringList keys;
	QString question, value;
	const auto pushQuestion = [&] {
		const auto normalized = NormalizeQuestion(question);
		if (!normalized.isEmpty()) {
			result.result.questions.emplace(
				normalized,
				TemplatesQuestion{ question, keys, value });
		}
		question = value = QString();
		keys = QStringList();
	};
	for (const auto &utf : lines) {
		const auto line = QString::fromUtf8(utf).trimmed();
		const auto match = QRegularExpression(
			qsl("^\\{([A-Z_]+)\\}$")
		).match(line);
		if (match.hasMatch()) {
			const auto token = match.captured(1);
			if (state == State::Value || state == State::MoreValue) {
				pushQuestion();
			}
			if (token == qstr("VALUE")) {
				state = value.isEmpty() ? State::Value : State::None;
			} else if (token == qstr("KEYS")) {
				state = keys.isEmpty() ? State::Keys : State::None;
			} else if (token == qstr("QUESTION")) {
				state = State::Question;
			} else if (token == qstr("URL")) {
				state = State::Url;
			} else {
				state = State::None;
			}
			continue;
		}

		switch (state) {
		case State::Keys:
			if (!line.isEmpty()) {
				keys.push_back(line);
			}
			break;
		case State::MoreValue:
			value += '\n';
			[[fallthrough]];
		case State::Value:
			value += line;
			state = State::MoreValue;
			break;
		case State::Question:
			if (question.isEmpty()) question = line;
			break;
		case State::Url:
			if (result.result.url.isEmpty()) {
				result.result.url = line;
			}
			break;
		}
	}
	pushQuestion();
	return result;
}

FileResult ReadFile(const QString &path) {
	QFile f(path);
	if (!f.open(QIODevice::ReadOnly)) {
		auto result = FileResult();
		result.errors.push_back(
			qsl("Couldn't open '%1' for reading!").arg(path));
		return result;
	}

	const auto blob = f.readAll();
	f.close();

	return ReadFromBlob(blob);
}

struct FilesResult {
	TemplatesData result;
	TemplatesIndex index;
	QStringList errors;
};

FilesResult ReadFiles(const QString &folder) {
	auto result = FilesResult();
	const auto files = QDir(folder).entryList(QDir::Files);
	for (const auto &path : files) {
		if (!IsTemplatesFile(path)) {
			continue;
		}
		auto file = ReadFile(folder + '/' + path);
		if (!file.result.url.isEmpty() || !file.result.questions.empty()) {
			result.result.files[path] = std::move(file.result);
		}
		result.errors.append(std::move(file.errors));
	}
	return result;
}

TemplatesIndex ComputeIndex(const TemplatesData &data) {
	using Id = TemplatesIndex::Id;
	using Term = TemplatesIndex::Term;

	auto uniqueFirst = std::map<QChar, base::flat_set<Id>>();
	auto uniqueFull = std::map<Id, base::flat_set<Term>>();
	const auto pushString = [&](
			const Id &id,
			const QString &string,
			int weight) {
		const auto list = TextUtilities::PrepareSearchWords(string);
		for (const auto &word : list) {
			uniqueFirst[word[0]].emplace(id);
			uniqueFull[id].emplace(std::make_pair(word, weight));
		}
	};
	for (const auto &[path, file] : data.files) {
		for (const auto &[normalized, question] : file.questions) {
			const auto id = std::make_pair(path, normalized);
			for (const auto &key : question.keys) {
				pushString(id, key, kWeightStep * kWeightStep);
			}
			pushString(id, question.question, kWeightStep);
			pushString(id, question.value, 1);
		}
	}

	const auto to_vector = [](auto &&range) {
		return range | ranges::to_vector;
	};
	auto result = TemplatesIndex();
	for (const auto &[ch, unique] : uniqueFirst) {
		result.first.emplace(ch, to_vector(unique));
	}
	for (const auto &[id, unique] : uniqueFull) {
		result.full.emplace(id, to_vector(unique));
	}
	return result;
}

} // namespace
} // namespace details

Templates::Templates(not_null<AuthSession*> session) : _session(session) {
	reload();
}

void Templates::reload() {
	if (_reloadAfterRead) {
		return;
	} else if (_reading.alive()) {
		_reloadAfterRead = true;
		return;
	}

	auto [left, right] = base::make_binary_guard();
	_reading = std::move(left);
	crl::async([=, guard = std::move(right)]() mutable {
		auto result = details::ReadFiles(cWorkingDir() + "TEMPLATES");
		result.index = details::ComputeIndex(result.result);
		crl::on_main([
			=,
			result = std::move(result),
			guard = std::move(guard)
		]() mutable {
			if (!guard.alive()) {
				return;
			}
			_data = std::move(result.result);
			_index = std::move(result.index);
			_errors.fire(std::move(result.errors));
			crl::on_main(this, [=] {
				if (base::take(_reloadAfterRead)) {
					reload();
				} else {
					update();
				}
			});
		});
	});
}

auto Templates::query(const QString &text) const -> std::vector<Question> {
	const auto words = TextUtilities::PrepareSearchWords(text);
	const auto questions = [&](const QString &word) {
		const auto i = _index.first.find(word[0]);
		return (i == end(_index.first)) ? 0 : i->second.size();
	};
	const auto best = ranges::min_element(words, std::less<>(), questions);
	if (best == std::end(words)) {
		return {};
	}
	const auto narrowed = _index.first.find((*best)[0]);
	if (narrowed == end(_index.first)) {
		return {};
	}
	using Id = details::TemplatesIndex::Id;
	using Term = details::TemplatesIndex::Term;
	const auto questionById = [&](const Id &id) {
		return _data.files.at(id.first).questions.at(id.second);
	};

	using Pair = std::pair<Question, int>;
	const auto computeWeight = [&](const Id &id) {
		auto result = 0;
		const auto full = _index.full.find(id);
		for (const auto &word : words) {
			const auto from = ranges::lower_bound(
				full->second,
				word,
				std::less<>(),
				[](const Term &term) { return term.first; });
			const auto till = std::find_if(
				from,
				end(full->second),
				[&](const Term &term) {
					return !term.first.startsWith(word);
				});
			const auto weight = std::max_element(
				from,
				till,
				[](const Term &a, const Term &b) {
					return a.second < b.second;
				});
			if (weight == till) {
				return 0;
			}
			result += weight->second * (weight->first == word ? 2 : 1);
		}
		return result;
	};
	const auto pairById = [&](const Id &id) {
		return std::make_pair(questionById(id), computeWeight(id));
	};
	const auto good = narrowed->second | ranges::view::transform(
		pairById
	) | ranges::view::filter([](const Pair &pair) {
		return pair.second > 0;
	}) | ranges::to_vector | ranges::action::sort(
		std::greater<>(),
		[](const Pair &pair) { return pair.second; }
	);
	return good | ranges::view::transform([](const Pair &pair) {
		return pair.first;
	}) | ranges::view::take(details::kQueryLimit) | ranges::to_vector;
}

void Templates::update() {

}

} // namespace Support
