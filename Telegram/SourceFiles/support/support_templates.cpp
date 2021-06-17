/*
This file is part of Telegram Desktop,
the official desktop application for the Telegram messaging service.

For license and copyright information please follow this link:
https://github.com/telegramdesktop/tdesktop/blob/master/LEGAL
*/
#include "support/support_templates.h"

#include "ui/toast/toast.h"
#include "data/data_session.h"
#include "core/shortcuts.h"
#include "main/main_session.h"

#include <QtNetwork/QNetworkAccessManager>

namespace Support {
namespace details {
namespace {

constexpr auto kQueryLimit = 10;
constexpr auto kWeightStep = 1000;

struct Delta {
	std::vector<const TemplatesQuestion*> added;
	std::vector<const TemplatesQuestion*> changed;
	std::vector<const TemplatesQuestion*> removed;

	std::map<QString, QStringList> keys;

	explicit operator bool() const {
		return !added.empty() || !changed.empty() || !removed.empty();
	}
};

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

QString NormalizeKey(const QString &query) {
	return TextUtilities::RemoveAccents(query.trimmed().toLower());
}

struct FileResult {
	TemplatesFile result;
	QStringList errors;
};

enum class ReadState {
	None,
	Question,
	Keys,
	Value,
	Url,
};

template <typename StateChange, typename LineCallback>
void ReadByLine(
	const QByteArray &blob,
	StateChange &&stateChange,
	LineCallback &&lineCallback) {
	using State = ReadState;
	auto state = State::None;
	auto hadKeys = false;
	auto hadValue = false;
	for (const auto &utf : blob.split('\n')) {
		const auto line = QString::fromUtf8(utf).trimmed();
		const auto match = QRegularExpression(
			qsl("^\\{([A-Z_]+)\\}$")
		).match(line);
		if (match.hasMatch()) {
			const auto token = match.captured(1);
			if (state == State::Value) {
				hadKeys = hadValue = false;
			}
			const auto newState = [&] {
				if (token == qstr("VALUE")) {
					return hadValue ? State::None : State::Value;
				} else if (token == qstr("KEYS")) {
					return hadKeys ? State::None : State::Keys;
				} else if (token == qstr("QUESTION")) {
					return State::Question;
				} else if (token == qstr("URL")) {
					return State::Url;
				} else {
					return State::None;
				}
			}();
			stateChange(state, newState);
			state = newState;
			lineCallback(state, line, true);
		} else {
			if (!line.isEmpty()) {
				if (state == State::Value) {
					hadValue = true;
				} else if (state == State::Keys) {
					hadKeys = true;
				}
			}
			lineCallback(state, line, false);
		}
	}
}

template <typename Callback>
QString ReadByLineGetUrl(const QByteArray &blob, Callback &&callback) {
	using State = ReadState;
	auto url = QString();
	auto question = TemplatesQuestion();
	const auto call = [&] {
		while (question.value.endsWith('\n')) {
			question.value.chop(1);
		}
		return callback(base::take(question));
	};
	ReadByLine(blob, [&](State was, State now) {
		if (was == State::Value) {
			call();
		}
	}, [&](State state, const QString &line, bool stateChangeLine) {
		if (stateChangeLine) {
			return;
		}
		switch (state) {
		case State::Keys:
			if (!line.isEmpty()) {
				question.originalKeys.push_back(line);
				if (const auto norm = NormalizeKey(line); !norm.isEmpty()) {
					question.normalizedKeys.push_back(norm);
				}
			}
			break;
		case State::Value:
			if (!question.value.isEmpty()) {
				question.value += '\n';
			}
			question.value += line;
			break;
		case State::Question:
			if (question.question.isEmpty()) {
				question.question = line;
			}
			break;
		case State::Url:
			if (url.isEmpty()) {
				url = line;
			}
			break;
		}
	});
	call();
	return url;
}

FileResult ReadFromBlob(const QByteArray &blob) {
	auto result = FileResult();
	result.result.url = ReadByLineGetUrl(blob, [&](TemplatesQuestion &&q) {
		const auto normalized = NormalizeQuestion(q.question);
		if (!normalized.isEmpty()) {
			result.result.questions.emplace(normalized, std::move(q));
		}
	});
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

void WriteWithOwnUrlAndKeys(
		QIODevice &device,
		const QByteArray &blob,
		const QString &url,
		const Delta &delta) {
	device.write("{URL}\n");
	device.write(url.toUtf8());
	device.write("\n\n");

	using State = ReadState;
	auto question = QString();
	auto normalized = QString();
	auto ownKeysWritten = false;
	ReadByLine(blob, [&](State was, State now) {
		if (was == State::Value) {
			question = normalized = QString();
		}
	}, [&](State state, const QString &line, bool stateChangeLine) {
		const auto writeLine = [&] {
			device.write(line.toUtf8());
			device.write("\n", 1);
		};
		switch (state) {
		case State::Keys:
			if (stateChangeLine) {
				writeLine();
				ownKeysWritten = [&] {
					if (normalized.isEmpty()) {
						return false;
					}
					const auto i = delta.keys.find(normalized);
					if (i == end(delta.keys)) {
						return false;
					}
					device.write(i->second.join('\n').toUtf8());
					device.write("\n", 1);
					return true;
				}();
			} else if (!ownKeysWritten) {
				writeLine();
			}
			break;
		case State::Value:
			writeLine();
			break;
		case State::Question:
			writeLine();
			if (!stateChangeLine && question.isEmpty()) {
				question = line;
				normalized = NormalizeQuestion(line);
			}
			break;
		case State::Url:
			break;
		}
	});
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
			for (const auto &key : question.normalizedKeys) {
				pushString(id, key, kWeightStep * kWeightStep);
			}
			pushString(id, question.question, kWeightStep);
			pushString(id, question.value, 1);
		}
	}

	auto result = TemplatesIndex();
	for (const auto &[ch, unique] : uniqueFirst) {
		result.first.emplace(ch, unique | ranges::to_vector);
	}
	for (const auto &[id, unique] : uniqueFull) {
		result.full.emplace(id, unique | ranges::to_vector);
	}
	return result;
}

void ReplaceFileIndex(
		TemplatesIndex &result,
		TemplatesIndex &&source,
		const QString &path) {
	for (auto i = begin(result.full); i != end(result.full);) {
		if (i->first.first == path) {
			i = result.full.erase(i);
		} else {
			++i;
		}
	}
	for (auto &[id, list] : source.full) {
		result.full.emplace(id, std::move(list));
	}

	using Id = TemplatesIndex::Id;
	for (auto &[ch, list] : result.first) {
		auto i = ranges::lower_bound(
			list,
			std::make_pair(path, QString()));
		auto j = std::find_if(i, end(list), [&](const Id &id) {
			return id.first != path;
		});
		list.erase(i, j);
	}
	for (auto &[ch, list] : source.first) {
		auto &to = result.first[ch];
		to.insert(
			end(to),
			std::make_move_iterator(begin(list)),
			std::make_move_iterator(end(list)));
		ranges::sort(to);
	}
}

void MoveKeys(TemplatesFile &to, const TemplatesFile &from) {
	const auto &existing = from.questions;
	for (auto &[normalized, question] : to.questions) {
		if (const auto i = existing.find(normalized); i != end(existing)) {
			question.originalKeys = i->second.originalKeys;
			question.normalizedKeys = i->second.normalizedKeys;
		}
	}
}

Delta ComputeDelta(const TemplatesFile &was, const TemplatesFile &now) {
	auto result = Delta();
	for (const auto &[normalized, question] : now.questions) {
		const auto i = was.questions.find(normalized);
		if (i == end(was.questions)) {
			result.added.push_back(&question);
		} else {
			result.keys.emplace(normalized, i->second.originalKeys);
			if (i->second.value != question.value) {
				result.changed.push_back(&question);
			}
		}
	}
	for (const auto &[normalized, question] : was.questions) {
		if (result.keys.find(normalized) == end(result.keys)) {
			result.removed.push_back(&question);
		}
	}
	return result;
}

QString FormatUpdateNotification(const QString &path, const Delta &delta) {
	auto result = qsl("Template file '%1' updated!\n\n").arg(path);
	if (!delta.added.empty()) {
		result += qstr("-------- Added --------\n\n");
		for (const auto question : delta.added) {
			result += qsl("Q: %1\nK: %2\nA: %3\n\n").arg(
				question->question,
				question->originalKeys.join(qsl(", ")),
				question->value.trimmed());
		}
	}
	if (!delta.changed.empty()) {
		result += qstr("-------- Modified --------\n\n");
		for (const auto question : delta.changed) {
			result += qsl("Q: %1\nA: %2\n\n").arg(
				question->question,
				question->value.trimmed());
		}
	}
	if (!delta.removed.empty()) {
		result += qstr("-------- Removed --------\n\n");
		for (const auto question : delta.removed) {
			result += qsl("Q: %1\n\n").arg(question->question);
		}
	}
	return result;
}

QString UpdateFile(
	const QString &path,
	const QByteArray &content,
	const QString &url,
	const Delta &delta) {
	auto result = QString();
	const auto full = cWorkingDir() + "TEMPLATES/" + path;
	const auto old = full + qstr(".old");
	QFile(old).remove();
	if (QFile(full).copy(old)) {
		result += qsl("(old file saved at '%1')"
		).arg(path + qstr(".old"));

		QFile f(full);
		if (f.open(QIODevice::WriteOnly)) {
			WriteWithOwnUrlAndKeys(f, content, url, delta);
		} else {
			result += qsl("\n\nError: could not open new file '%1'!"
			).arg(full);
		}
	} else {
		result += qsl("Error: could not save old file '%1'!"
		).arg(old);
	}
	return result;
}

int CountMaxKeyLength(const TemplatesData &data) {
	auto result = 0;
	for (const auto &[path, file] : data.files) {
		for (const auto &[normalized, question] : file.questions) {
			for (const auto &key : question.normalizedKeys) {
				accumulate_max(result, key.size());
			}
		}
	}
	return result;
}

} // namespace
} // namespace details

using namespace details;

struct Templates::Updates {
	QNetworkAccessManager manager;
	std::map<QString, QNetworkReply*> requests;
};

Templates::Templates(not_null<Main::Session*> session) : _session(session) {
	load();
	Shortcuts::Requests(
	) | rpl::start_with_next([=](not_null<Shortcuts::Request*> request) {
		using Command = Shortcuts::Command;
		request->check(
			Command::SupportReloadTemplates
		) && request->handle([=] {
			reload();
			return true;
		});
	}, _lifetime);
}

void Templates::reload() {
	_reloadToastSubscription = errors(
	) | rpl::start_with_next([=](QStringList errors) {
		Ui::Toast::Show(errors.isEmpty()
			? "Templates reloaded!"
			: ("Errors:\n\n" + errors.join("\n\n")));
	});

	load();
}

void Templates::load() {
	if (_reloadAfterRead) {
		return;
	} else if (_reading || _updates) {
		_reloadAfterRead = true;
		return;
	}

	crl::async([=, guard = _reading.make_guard()]() mutable {
		auto result = ReadFiles(cWorkingDir() + "TEMPLATES");
		result.index = ComputeIndex(result.result);
		crl::on_main(std::move(guard), [
			=,
			result = std::move(result)
		]() mutable {
			setData(std::move(result.result));
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

void Templates::setData(TemplatesData &&data) {
	_data = std::move(data);
	_maxKeyLength = CountMaxKeyLength(_data);
}

void Templates::ensureUpdatesCreated() {
	if (_updates) {
		return;
	}
	_updates = std::make_unique<Updates>();
	QObject::connect(
		&_updates->manager,
		&QNetworkAccessManager::finished,
		[=](QNetworkReply *reply) { updateRequestFinished(reply); });
}

void Templates::update() {
	auto errors = QStringList();
	const auto sendRequest = [&](const QString &path, const QString &url) {
		ensureUpdatesCreated();
		if (_updates->requests.find(path) != end(_updates->requests)) {
			return;
		}
		_updates->requests.emplace(
			path,
			_updates->manager.get(QNetworkRequest(url)));
	};

	for (const auto &[path, file] : _data.files) {
		if (!file.url.isEmpty()) {
			sendRequest(path, file.url);
		}
	}
}

void Templates::updateRequestFinished(QNetworkReply *reply) {
	reply->deleteLater();

	const auto path = [&] {
		for (const auto &[file, sent] : _updates->requests) {
			if (sent == reply) {
				return file;
			}
		}
		return QString();
	}();
	if (path.isEmpty()) {
		return;
	}
	_updates->requests[path] = nullptr;
	if (reply->error() != QNetworkReply::NoError) {
		const auto message = qsl(
			"Error: template update failed, url '%1', error %2, %3"
		).arg(reply->url().toDisplayString()
		).arg(reply->error()
		).arg(reply->errorString());
		_session->data().serviceNotification({ message });
		return;
	}
	LOG(("Got template from url '%1'"
		).arg(reply->url().toDisplayString()));
	const auto content = reply->readAll();
	crl::async([=, weak = base::make_weak(this)]{
		auto result = ReadFromBlob(content);
		auto one = TemplatesData();
		one.files.emplace(path, std::move(result.result));
		auto index = ComputeIndex(one);
		crl::on_main(weak,[
			=,
			one = std::move(one),
			errors = std::move(result.errors),
			index = std::move(index)
		]() mutable {
			auto &existing = _data.files.at(path);
			auto &parsed = one.files.at(path);
			MoveKeys(parsed, existing);
			ReplaceFileIndex(_index, ComputeIndex(one), path);
			if (!errors.isEmpty()) {
				_errors.fire(std::move(errors));
			}
			if (const auto delta = ComputeDelta(existing, parsed)) {
				const auto text = FormatUpdateNotification(
					path,
					delta);
				const auto copy = UpdateFile(
					path,
					content,
					existing.url,
					delta);
				const auto full = text + copy;
				_session->data().serviceNotification({ full });
			}
			_data.files.at(path) = std::move(one.files.at(path));

			_updates->requests.erase(path);
			checkUpdateFinished();
		});
		});
}

void Templates::checkUpdateFinished() {
	if (!_updates || !_updates->requests.empty()) {
		return;
	}
	_updates = nullptr;
	if (base::take(_reloadAfterRead)) {
		reload();
	}
}

auto Templates::matchExact(QString query) const
-> std::optional<QuestionByKey> {
	if (query.isEmpty() || query.size() > _maxKeyLength) {
		return {};
	}

	query = NormalizeKey(query);

	for (const auto &[path, file] : _data.files) {
		for (const auto &[normalized, question] : file.questions) {
			for (const auto &key : question.normalizedKeys) {
				if (key == query) {
					return QuestionByKey{ question, key };
				}
			}
		}
	}
	return {};
}

auto Templates::matchFromEnd(QString query) const
-> std::optional<QuestionByKey> {
	if (query.size() > _maxKeyLength) {
		query = query.mid(query.size() - _maxKeyLength);
	}

	const auto size = query.size();
	auto queries = std::vector<QString>();
	queries.reserve(size);
	for (auto i = 0; i != size; ++i) {
		queries.push_back(NormalizeKey(query.mid(size - i - 1)));
	}

	auto result = std::optional<QuestionByKey>();
	for (const auto &[path, file] : _data.files) {
		for (const auto &[normalized, question] : file.questions) {
			for (const auto &key : question.normalizedKeys) {
				if (key.size() <= queries.size()
					&& queries[key.size() - 1] == key
					&& (!result || result->key.size() <= key.size())) {
					result = QuestionByKey{ question, key };
				}
			}
		}
	}
	return result;
}

Templates::~Templates() = default;

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
	using Id = TemplatesIndex::Id;
	using Term = TemplatesIndex::Term;
	const auto questionById = [&](const Id &id) {
		return _data.files.at(id.first).questions.at(id.second);
	};

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
	using Pair = std::pair<Id, int>;
	const auto pairById = [&](const Id &id) {
		return std::make_pair(id, computeWeight(id));
	};
	const auto sorter = [](const Pair &a, const Pair &b) {
		// weight DESC filename DESC question ASC
		if (a.second > b.second) {
			return true;
		} else if (a.second < b.second) {
			return false;
		} else if (a.first.first > b.first.first) {
			return true;
		} else if (a.first.first < b.first.first) {
			return false;
		} else {
			return (a.first.second < b.first.second);
		}
	};
	const auto good = narrowed->second | ranges::views::transform(
		pairById
	) | ranges::views::filter([](const Pair &pair) {
		return pair.second > 0;
	}) | ranges::to_vector | ranges::actions::stable_sort(sorter);
	return good | ranges::views::transform([&](const Pair &pair) {
		return questionById(pair.first);
	}) | ranges::views::take(kQueryLimit) | ranges::to_vector;
}

} // namespace Support
