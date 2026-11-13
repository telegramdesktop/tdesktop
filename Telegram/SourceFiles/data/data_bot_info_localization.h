/*
This file is part of Telegram Desktop,
the official desktop application for the Telegram messaging service.

For license and copyright information please follow this link:
https://github.com/telegramdesktop/tdesktop/blob/master/LEGAL
*/
#pragma once

#include "base/timer.h"
#include "base/weak_ptr.h"
#include "data/data_bot_info_draft.h"

namespace Data {

struct BotInfoTransport {
	Fn<void(
		QString langCode,
		Fn<void(BotInfoTexts)> done,
		Fn<void(QString)> fail)> read;
	Fn<void(
		QString langCode,
		BotInfoChange change,
		Fn<void()> done,
		Fn<void(QString)> fail)> write;
	Fn<void(
		std::vector<QString> texts,
		QString langCode,
		Fn<void(std::vector<QString>)> done,
		Fn<void(QString)> fail)> translate;
	Fn<void()> refresh;
};

class BotInfoLocalization final : public base::has_weak_ptr {
public:
	enum class Job : uchar {
		None,
		Load,
		Apply,
		Translate,
	};
	struct State {
		Job job = Job::None;
		int done = 0;
		int total = 0;
		crl::time pausedTill = 0;
		QString stopped;

		friend inline bool operator==(
			const State &,
			const State &) = default;
	};
	struct TranslateRequest {
		std::vector<QString> codes;
		std::vector<BotInfoField> fields;
	};

	explicit BotInfoLocalization(BotInfoTransport transport);

	[[nodiscard]] BotInfoDraft &draft();
	[[nodiscard]] State state() const;
	[[nodiscard]] rpl::producer<State> stateValue() const;
	[[nodiscard]] bool known(const QString &code) const;

	void load(std::vector<QString> codes);
	void apply();
	void applyOne(const QString &code);
	void translate(TranslateRequest request);
	void cancel();

private:
	void start(Job job, std::vector<QString> codes);
	void finish(State state);
	void next();
	void advance();
	void failed(const QString &error, Fn<void()> skip);
	void performLoad(const QString &code);
	void performApply(const QString &code);
	void performTranslate(const QString &code);

	const BotInfoTransport _transport;
	BotInfoDraft _draft;
	rpl::variable<State> _state;
	std::vector<QString> _queue;
	std::vector<BotInfoField> _translateFields;
	base::Timer _resume;
	int _generation = 0;
	base::flat_set<QString> _known;
	bool _written = false;

};

} // namespace Data
