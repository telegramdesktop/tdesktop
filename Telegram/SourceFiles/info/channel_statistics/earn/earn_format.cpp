/*
This file is part of Telegram Desktop,
the official desktop application for the Telegram messaging service.

For license and copyright information please follow this link:
https://github.com/telegramdesktop/tdesktop/blob/master/LEGAL
*/
#include "info/channel_statistics/earn/earn_format.h"

namespace Info::ChannelEarn {

using EarnInt = Data::EarnInt;

constexpr auto kMinorPartLength = 9;
constexpr auto kMaxChoppedZero = kMinorPartLength - 2;
constexpr auto kZero = QChar('0');
constexpr auto kDot = QChar('.');

QString MajorPart(EarnInt value) {
	const auto string = QString::number(value);
	const auto diff = int(string.size()) - kMinorPartLength;
	return (diff <= 0) ? QString(kZero) : string.mid(0, diff);
}

QString MajorPart(CreditsAmount value) {
	return QString::number(int64(value.value()));
}

QString MinorPart(EarnInt value) {
	if (!value) {
		return QString(kDot) + kZero + kZero;
	}
	const auto string = QString::number(value);
	const auto diff = int(string.size()) - kMinorPartLength;
	const auto result = (diff < 0)
		? kDot + u"%1"_q.arg(0, std::abs(diff), 10, kZero) + string
		: kDot + string.mid(diff);
	const auto begin = (result.constData());
	const auto end = (begin + result.size());
	auto ch = end - 1;
	auto zeroCount = 0;
	while (ch != begin) {
		if (((*ch) == kZero) && (zeroCount < kMaxChoppedZero)) {
			zeroCount++;
		} else {
			break;
		}
		ch--;
	}
	return result.chopped(zeroCount);
}

QString MinorPart(CreditsAmount value) {
	return QString::number(value.value(), 'f', 2).right(3);
}

QString ToUsd(
		Data::EarnInt value,
		float64 rate,
		int afterFloat) {
	return ToUsd(CreditsAmount(value), rate, afterFloat);
}

QString ToUsd(
		CreditsAmount value,
		float64 rate,
		int afterFloat) {
	constexpr auto kApproximately = QChar(0x2248);

	return QString(kApproximately)
		+ QChar('$')
		+ QString::number(
			value.value() * rate,
			'f',
			afterFloat ? afterFloat : 2);
}

} // namespace Info::ChannelEarn
