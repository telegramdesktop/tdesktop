/*
This file is part of Telegram Desktop,
the official desktop application for the Telegram messaging service.

For license and copyright information please follow this link:
https://github.com/telegramdesktop/tdesktop/blob/master/LEGAL
*/
#pragma once

#include "gram/ton/gram_address.h"

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

struct StreamEvent {
	StreamEventKind kind = StreamEventKind::Unknown;
	std::vector<Address> accounts;
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
