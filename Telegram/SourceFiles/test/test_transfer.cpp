/*
This file is part of Telegram Desktop,
the official desktop application for the Telegram messaging service.

For license and copyright information please follow this link:
https://github.com/telegramdesktop/tdesktop/blob/master/LEGAL
*/
#include "test/test_transfer.h"

#include "data/data_document.h"
#include "data/data_file_origin.h"
#include "test/test_agent.h"
#include "test/test_log.h"
#include "ui/text/format_values.h"

namespace Test {
namespace {

struct SaveEvent {
	DocumentData *document = nullptr;
	QString targetPath;
	bool autoLoading = false;
};

struct FailureEvent {
	DocumentData *document = nullptr;
	bool started = false;
};

[[nodiscard]] rpl::event_stream<SaveEvent> &SaveEvents() {
	static auto result = rpl::event_stream<SaveEvent>();
	return result;
}

[[nodiscard]] rpl::event_stream<FailureEvent> &FailureEvents() {
	static auto result = rpl::event_stream<FailureEvent>();
	return result;
}

} // namespace

struct DocumentTransfer::State {
	void observeSave(const SaveEvent &event);
	void observeFailure(const FailureEvent &event);

	not_null<DocumentData*> document;
	QString diagnosticTag;
	Fn<void(const TransferFailure &)> failureCallback;
	QString targetPath;
	std::vector<TransferSaveCall> saveCalls;
	std::optional<TransferFailure> failure;
	int nextSequence = 1;
	bool started = false;
	bool driving = false;
};

void DocumentTransfer::State::observeSave(const SaveEvent &event) {
	if (event.document != document) {
		return;
	}
	auto source = QString();
	if (driving) {
		source = u"driver:"_q + diagnosticTag;
	} else if (event.autoLoading) {
		source = u"auto-loading"_q;
	} else {
		source = u"external"_q;
	}
	saveCalls.push_back({
		.sequence = nextSequence++,
		.targetPath = event.targetPath,
		.autoLoading = event.autoLoading,
		.source = std::move(source),
	});
}

void DocumentTransfer::State::observeFailure(const FailureEvent &event) {
	if (event.document != document || failure.has_value()) {
		return;
	}
	failure = TransferFailure{
		.status = int(document->status),
		.loading = document->loading(),
		.offset = document->loadOffset(),
		.targetPath = targetPath,
		.started = event.started,
	};
	if (failureCallback) {
		failureCallback(*failure);
	}
}

DocumentTransfer::DocumentTransfer(
		not_null<DocumentData*> document,
		QString diagnosticTag,
		Fn<void(const TransferFailure &)> failureCallback,
		rpl::lifetime &lifetime)
: _state(std::make_shared<State>(State{
	.document = document,
	.diagnosticTag = std::move(diagnosticTag),
	.failureCallback = std::move(failureCallback),
})) {
	const auto state = _state;
	SaveEvents().events(
	) | rpl::on_next(
		[state](const SaveEvent &event) {
			const auto strong = state;
			strong->observeSave(event);
		},
		lifetime);
	FailureEvents().events(
	) | rpl::on_next(
		[state](const FailureEvent &event) {
			const auto strong = state;
			strong->observeFailure(event);
		},
		lifetime);
}

void DocumentTransfer::start(
		Data::FileOrigin origin,
		QString targetPath) {
	const auto state = _state;
	if (targetPath.isEmpty()) {
		Fail(
			u"empty document transfer target"_q,
			u"tag=%1"_q.arg(state->diagnosticTag));
		return;
	}
	if (state->started) {
		Fail(
			u"duplicate document transfer start"_q,
			u"tag=%1"_q.arg(state->diagnosticTag));
		return;
	}
	state->started = true;
	state->targetPath = std::move(targetPath);
	const auto wasDriving = std::exchange(state->driving, true);
	const auto guard = gsl::finally([state, wasDriving] {
		state->driving = wasDriving;
	});
	state->document->save(std::move(origin), state->targetPath);
}

TransferSample DocumentTransfer::sample() const {
	const auto state = _state;
	const auto loading = state->document->loading();
	const auto offset = state->document->loadOffset();
	return {
		.loading = loading,
		.offset = offset,
		.text = Ui::FormatDownloadText(offset, state->document->size),
	};
}

std::vector<TransferSaveCall> DocumentTransfer::saveCalls() const {
	return _state->saveCalls;
}

std::optional<TransferFailure> DocumentTransfer::failure() const {
	return _state->failure;
}

void NotifyDocumentSave(
		not_null<DocumentData*> document,
		const QString &targetPath,
		bool autoLoading) {
	if (!Active()) {
		return;
	}
	SaveEvents().fire_copy({
		.document = document,
		.targetPath = targetPath,
		.autoLoading = autoLoading,
	});
}

void NotifyDocumentLoadFailed(
		not_null<DocumentData*> document,
		bool started) {
	if (!Active()) {
		return;
	}
	FailureEvents().fire_copy({
		.document = document,
		.started = started,
	});
}

} // namespace Test
