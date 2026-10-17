/*
This file is part of Telegram Desktop,
the official desktop application for the Telegram messaging service.

For license and copyright information please follow this link:
https://github.com/telegramdesktop/tdesktop/blob/master/LEGAL
*/
#pragma once

#include "base/basic_types.h"

#include <QtCore/QString>

#include <optional>

namespace Wallet {

enum class TonConnectLinkKind : uchar {
	Bare,
	OpenPending,
	Connect,
};

struct TonConnectLink {
	TonConnectLinkKind kind = TonConnectLinkKind::Bare;
	QString clientId;
	QString manifestUrl;
	std::optional<QString> proofPayload;
	QString traceId;
	QString ret;
};

[[nodiscard]] std::optional<QString> TonConnectStartParamQuery(
	const QString &startapp);
[[nodiscard]] QString TonConnectStartParam(QString query);

[[nodiscard]] std::optional<TonConnectLink> ParseTonConnectLink(
	const QString &query);

[[nodiscard]] bool ValidHttpsUrl(const QString &url);

} // namespace Wallet
