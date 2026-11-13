/*
This file is part of Telegram Desktop,
the official desktop application for the Telegram messaging service.

For license and copyright information please follow this link:
https://github.com/telegramdesktop/tdesktop/blob/master/LEGAL
*/
#include "data/data_bot_info_localization.h"

namespace Data {
namespace {

constexpr auto kMaxFloodWait = 60;

[[nodiscard]] int FloodWaitSeconds(const QString &error) {
	if (!error.startsWith(u"FLOOD_"_q)) {
		return 0;
	}
	return error.mid(error.lastIndexOf(u'_') + 1).toInt();
}

} // namespace

BotInfoLocalization::BotInfoLocalization(BotInfoTransport transport)
: _transport(std::move(transport))
, _resume([=] { next(); }) {
}

BotInfoDraft &BotInfoLocalization::draft() {
	return _draft;
}

BotInfoLocalization::State BotInfoLocalization::state() const {
	return _state.current();
}

auto BotInfoLocalization::stateValue() const -> rpl::producer<State> {
	return _state.value();
}

bool BotInfoLocalization::known(const QString &code) const {
	return _known.contains(code);
}

void BotInfoLocalization::load(std::vector<QString> codes) {
	start(Job::Load, std::move(codes));
}

void BotInfoLocalization::apply() {
	auto codes = std::vector<QString>();
	for (const auto &language : _draft.languages()) {
		if (!language.staged.empty() && !language.hasProblem()) {
			codes.push_back(language.code);
		}
	}
	start(Job::Apply, std::move(codes));
}

void BotInfoLocalization::applyOne(const QString &code) {
	start(Job::Apply, { code });
}

void BotInfoLocalization::translate(TranslateRequest request) {
	_translateFields = std::move(request.fields);
	start(Job::Translate, std::move(request.codes));
}

void BotInfoLocalization::cancel() {
	finish(State());
}

void BotInfoLocalization::finish(State state) {
	++_generation;
	_resume.cancel();
	_queue.clear();
	if (base::take(_written) && _transport.refresh) {
		_transport.refresh();
	}
	_state = std::move(state);
}

void BotInfoLocalization::start(Job job, std::vector<QString> codes) {
	cancel();
	if (codes.empty()) {
		return;
	}
	_queue = std::move(codes);
	_state = State{ .job = job, .total = int(_queue.size()) };
	next();
}

void BotInfoLocalization::next() {
	auto state = _state.current();
	if (state.done >= state.total) {
		cancel();
		return;
	}
	if (state.pausedTill) {
		state.pausedTill = 0;
		_state = state;
	}
	const auto &code = _queue[state.done];
	switch (state.job) {
	case Job::Load: performLoad(code); break;
	case Job::Apply: performApply(code); break;
	case Job::Translate: performTranslate(code); break;
	case Job::None: Unexpected("Job in BotInfoLocalization::next.");
	}
}

void BotInfoLocalization::advance() {
	auto state = _state.current();
	++state.done;
	_state = state;
	next();
}

void BotInfoLocalization::failed(const QString &error, Fn<void()> skip) {
	const auto wait = FloodWaitSeconds(error);
	if (!wait) {
		skip();
		advance();
	} else if (wait > kMaxFloodWait) {
		finish({ .stopped = error });
	} else {
		const auto delay = (wait + 1) * crl::time(1000);
		auto state = _state.current();
		state.pausedTill = crl::now() + delay;
		_state = state;
		_resume.callOnce(delay);
	}
}

void BotInfoLocalization::performLoad(const QString &code) {
	const auto generation = _generation;
	const auto fresh = [=] { return generation == _generation; };
	_transport.read(code, crl::guard(this, [=](BotInfoTexts texts) {
		if (fresh()) {
			_known.emplace(code);
			_draft.setServer(code, std::move(texts));
			advance();
		}
	}), crl::guard(this, [=](QString error) {
		if (fresh()) {
			failed(error, [] {});
		}
	}));
}

void BotInfoLocalization::performApply(const QString &code) {
	const auto language = _draft.find(code);
	if (!language || language->staged.empty() || language->hasProblem()) {
		advance();
		return;
	}
	const auto change = language->staged;
	const auto generation = _generation;
	const auto fresh = [=] { return generation == _generation; };
	_transport.write(code, change, crl::guard(this, [=] {
		if (fresh()) {
			_written = true;
			_draft.applied(code, change);
			advance();
		}
	}), crl::guard(this, [=](QString error) {
		if (fresh()) {
			failed(error, [=] { _draft.setError(code, error); });
		}
	}));
}

void BotInfoLocalization::performTranslate(const QString &code) {
	const auto &source = _draft.fallback();
	auto fields = std::vector<BotInfoField>();
	auto texts = std::vector<QString>();
	for (const auto field : _translateFields) {
		const auto text = source.value(field);
		if (!text.isEmpty()) {
			fields.push_back(field);
			texts.push_back(text);
		}
	}
	if (fields.empty()) {
		advance();
		return;
	}
	const auto generation = _generation;
	const auto fresh = [=] { return generation == _generation; };
	_transport.translate(texts, code, crl::guard(this, [=](
			std::vector<QString> result) {
		if (!fresh()) {
			return;
		}
		const auto count = std::min(result.size(), fields.size());
		for (auto i = 0; i != int(count); ++i) {
			_draft.stage(code, fields[i], result[i], true);
		}
		advance();
	}), crl::guard(this, [=](QString error) {
		if (fresh()) {
			failed(error, [] {});
		}
	}));
}

} // namespace Data
