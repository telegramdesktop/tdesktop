/*
This file is part of Telegram Desktop,
the official desktop application for the Telegram messaging service.

For license and copyright information please follow this link:
https://github.com/telegramdesktop/tdesktop/blob/master/LEGAL
*/
#include "gram/ton/gram_transfer_link.h"

#include "base/qthelp_url.h"
#include "gram/ton/gram_address.h"

#include <QtCore/QUrl>
#include <QtCore/QUrlQuery>

namespace Gram {
namespace {

const auto kPrefix = u"ton://transfer/"_q;

[[nodiscard]] bool IsDecimalDigits(const QString &text) {
	if (text.isEmpty()) {
		return false;
	}
	for (const auto ch : text) {
		const auto code = ch.unicode();
		if (code < '0' || code > '9') {
			return false;
		}
	}
	return true;
}

} // namespace

std::optional<TransferLink> ParseTransferLink(const QString &url) {
	if (!url.startsWith(kPrefix, Qt::CaseInsensitive)) {
		return std::nullopt;
	}
	const auto tail = url.mid(kPrefix.size());
	const auto question = tail.indexOf(QChar('?'));
	const auto address = (question < 0) ? tail : tail.left(question);
	if (!ParseAddress(address)) {
		return std::nullopt;
	}
	auto result = TransferLink();
	result.address = address;
	if (question < 0) {
		return result;
	}
	const auto query = QUrlQuery(tail.mid(question + 1));
	const auto amount = query.queryItemValue(
		u"amount"_q,
		QUrl::FullyDecoded);
	if (!amount.isEmpty()) {
		auto ok = false;
		const auto parsed = amount.toLongLong(&ok);
		if (!IsDecimalDigits(amount) || !ok) {
			return std::nullopt;
		}
		result.amountNano = parsed;
	}
	result.comment = query.queryItemValue(u"text"_q, QUrl::FullyDecoded);
	return result;
}

QString FormatTransferLink(const TransferLink &link) {
	auto result = kPrefix + link.address;
	auto separator = u"?"_q;
	if (link.amountNano > 0) {
		result += separator
			+ u"amount="_q
			+ QString::number(link.amountNano);
		separator = u"&"_q;
	}
	if (!link.comment.isEmpty()) {
		result += separator + u"text="_q + qthelp::url_encode(link.comment);
	}
	return result;
}

} // namespace Gram
