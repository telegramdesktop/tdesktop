/*
This file is part of Telegram Desktop,
the official desktop application for the Telegram messaging service.

For license and copyright information please follow this link:
https://github.com/telegramdesktop/tdesktop/blob/master/LEGAL
*/
#pragma once

#include <QtCore/QByteArray>
#include <QtCore/QString>

#include <optional>
#include <vector>

namespace Gram {

struct StreamEndpoint {
	QString host;
	QString requestTarget;
	int port = 0;
};

enum class StreamEventKind {
	Unknown,
	AccountState,
	Transactions,
	TraceInvalidated,
};

// StreamEvent::accounts carries the provider's account strings
// verbatim, deduplicated by string. A frame may name the same account
// in raw or in user-friendly form, and both are kept as distinct
// entries; normalizing them to decide whether one of them is ours is
// Wallet::Stream::mine()'s job, not this parser's.
struct StreamEvent {
	StreamEventKind kind = StreamEventKind::Unknown;
	std::vector<QString> accounts;
};

[[nodiscard]] std::optional<StreamEndpoint> ParseStreamEndpoint(
	const QString &url);
[[nodiscard]] QString StreamEndpointLabel(const StreamEndpoint &endpoint);
[[nodiscard]] StreamEvent ParseStreamEvent(const QByteArray &frame);
[[nodiscard]] QByteArray StreamSubscribeMessage(
	const QString &account,
	quint32 id);
[[nodiscard]] QByteArray StreamPingMessage(quint32 id);

} // namespace Gram
