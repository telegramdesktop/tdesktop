/*
This file is part of Telegram Desktop,
the official desktop application for the Telegram messaging service.

For license and copyright information please follow this link:
https://github.com/telegramdesktop/tdesktop/blob/master/LEGAL
*/
#include "api/api_bot_info.h"

#include "apiwrap.h"
#include "data/data_user.h"
#include "main/main_session.h"
#include "mtproto/sender.h"

namespace Api {
namespace {

constexpr auto kMaxFloodWait = 60;

[[nodiscard]] int FloodWaitSeconds(const QString &error) {
	if (!error.startsWith(u"FLOOD_"_q)) {
		return 0;
	}
	return error.mid(error.lastIndexOf(u'_') + 1).toInt();
}

} // namespace

BotInfoTransport MakeBotInfoTransport(not_null<UserData*> bot) {
	const auto api = std::make_shared<MTP::Sender>(&bot->session().mtp());
	const auto fail = [](Fn<void(QString)> callback) {
		return [=](const MTP::Error &error) {
			callback(error.type());
		};
	};
	return {
		.read = [=](
				QString langCode,
				Fn<void(Data::BotInfoTexts)> done,
				Fn<void(QString)> failed) {
			api->request(MTPbots_GetBotInfo(
				MTP_flags(MTPbots_GetBotInfo::Flag::f_bot),
				bot->inputUser(),
				MTP_string(langCode)
			)).done([=](const MTPbots_BotInfo &result) {
				const auto &data = result.data();
				done({
					.name = qs(data.vname()),
					.about = qs(data.vabout()),
					.description = qs(data.vdescription()),
				});
			}).fail(fail(failed)).handleFloodErrors().send();
		},
		.write = [=](
				QString langCode,
				Data::BotInfoChange change,
				Fn<void()> done,
				Fn<void(QString)> failed) {
			using Flag = MTPbots_SetBotInfo::Flag;
			api->request(MTPbots_SetBotInfo(
				MTP_flags(Flag::f_bot
					| (change.name ? Flag::f_name : Flag())
					| (change.about ? Flag::f_about : Flag())
					| (change.description ? Flag::f_description : Flag())),
				bot->inputUser(),
				MTP_string(langCode),
				MTP_string(change.name.value_or(QString())),
				MTP_string(change.about.value_or(QString())),
				MTP_string(change.description.value_or(QString()))
			)).done([=] {
				if (langCode.isEmpty() && change.about) {
					bot->setAbout(*change.about);
				}
				done();
			}).fail(fail(failed)).handleFloodErrors().send();
		},
		.translate = [=](
				std::vector<QString> texts,
				QString langCode,
				Fn<void(std::vector<QString>)> done,
				Fn<void(QString)> failed) {
			auto list = QVector<MTPTextWithEntities>();
			list.reserve(texts.size());
			for (const auto &text : texts) {
				list.push_back(MTP_textWithEntities(
					MTP_string(text),
					MTP_vector<MTPMessageEntity>()));
			}
			api->request(MTPmessages_TranslateText(
				MTP_flags(MTPmessages_TranslateText::Flag::f_text),
				MTP_inputPeerEmpty(),
				MTPVector<MTPint>(),
				MTP_vector<MTPTextWithEntities>(list),
				MTP_string(langCode),
				MTPstring() // tone
			)).done([=](const MTPmessages_TranslatedText &result) {
				done(ranges::views::all(
					result.data().vresult().v
				) | ranges::views::transform([](
						const MTPTextWithEntities &entry) {
					return qs(entry.data().vtext());
				}) | ranges::to_vector);
			}).fail(fail(failed)).handleFloodErrors().send();
		},
		.refresh = [=] {
			bot->session().api().requestFullPeer(bot);
		},
	};
}

BotInfoLocalization::BotInfoLocalization(BotInfoTransport transport)
: _transport(std::move(transport))
, _resume([=] { next(); }) {
}

Data::BotInfoDraft &BotInfoLocalization::draft() {
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

void BotInfoLocalization::translateOne(
		const QString &text,
		const QString &code,
		Fn<void(QString)> done,
		Fn<void(QString)> fail) {
	_transport.translate({ text }, code, [=](std::vector<QString> result) {
		if (result.empty()) {
			fail(QString());
		} else {
			done(result.front());
		}
	}, fail);
}

void BotInfoLocalization::cancel() {
	++_generation;
	_resume.cancel();
	_queue.clear();
	if (base::take(_written) && _transport.refresh) {
		_transport.refresh();
	}
	_state = State();
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
		cancel();
		_state = State{ .stopped = error };
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
	_transport.read(code, crl::guard(this, [=](Data::BotInfoTexts texts) {
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
	const auto language = _draft.find(code);
	const auto &source = _draft.fallback();
	auto fields = std::vector<Data::BotInfoField>();
	auto texts = std::vector<QString>();
	for (const auto field : _translateFields) {
		const auto text = source.value(field);
		const auto missing = !language || language->value(field).isEmpty();
		if (missing && !text.isEmpty()) {
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

not_null<BotInfoLocalization*> BotInfoLocalizations::get(
		not_null<UserData*> bot) {
	const auto i = _localizations.find(bot);
	if (i != end(_localizations)) {
		return i->second.get();
	}
	return _localizations.emplace(
		bot,
		std::make_unique<BotInfoLocalization>(MakeBotInfoTransport(bot))
	).first->second.get();
}

} // namespace Api
