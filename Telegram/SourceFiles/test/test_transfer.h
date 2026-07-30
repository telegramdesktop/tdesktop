/*
This file is part of Telegram Desktop,
the official desktop application for the Telegram messaging service.

For license and copyright information please follow this link:
https://github.com/telegramdesktop/tdesktop/blob/master/LEGAL
*/
#pragma once

#include "base/basic_types.h"

class DocumentData;

namespace Data {
struct FileOrigin;
} // namespace Data

namespace rpl {
class lifetime;
} // namespace rpl

namespace Test {

struct TransferSample {
	bool loading = false;
	int64 offset = 0;
	QString text;
};

struct TransferSaveCall {
	int sequence = 0;
	QString targetPath;
	bool autoLoading = false;
	QString source;
};

struct TransferFailure {
	int status = 0;
	bool loading = false;
	int64 offset = 0;
	QString targetPath;
	bool started = false;
};

class DocumentTransfer final {
public:
	// Subscribes immediately; the caller lifetime owns both observations.
	DocumentTransfer(
		not_null<DocumentData*> document,
		QString diagnosticTag,
		Fn<void(const TransferFailure &)> failureCallback,
		rpl::lifetime &lifetime);

	// Starts one explicit-path save; a duplicate fails without saving again.
	void start(Data::FileOrigin origin, QString targetPath);

	// Reads current transfer state without mutating the document.
	[[nodiscard]] TransferSample sample() const;

	// Returns a copy of every matching save-entry observation.
	[[nodiscard]] std::vector<TransferSaveCall> saveCalls() const;

	// Returns a copy of the first exact failed-state observation.
	[[nodiscard]] std::optional<TransferFailure> failure() const;

private:
	struct State;
	const std::shared_ptr<State> _state;

};

// Publishes one save entry synchronously without changing product state.
void NotifyDocumentSave(
	not_null<DocumentData*> document,
	const QString &targetPath,
	bool autoLoading);

// Publishes the exact failed assignment synchronously without product changes.
void NotifyDocumentLoadFailed(
	not_null<DocumentData*> document,
	bool started);

} // namespace Test
