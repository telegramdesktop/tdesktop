/*
This file is part of Telegram Desktop,
the official desktop application for the Telegram messaging service.

For license and copyright information please follow this link:
https://github.com/telegramdesktop/tdesktop/blob/master/LEGAL
*/
#pragma once

#include "base/unique_qptr.h"

namespace Ui {
class RpWidget;
} // namespace Ui

namespace Calls {

class Call;

[[nodiscard]] std::vector<EmojiPtr> ComputeEmojiFingerprint(
	not_null<Call*> call);
[[nodiscard]] std::vector<EmojiPtr> ComputeEmojiFingerprint(
	bytes::const_span fingerprint);

[[nodiscard]] base::unique_qptr<Ui::RpWidget> CreateFingerprintAndSignalBars(
	not_null<QWidget*> parent,
	not_null<Call*> call);

struct FingerprintBadgeState {
	struct Entry {
		EmojiPtr emoji = nullptr;
		std::vector<EmojiPtr> sliding;
		std::vector<EmojiPtr> carousel;
		crl::time time = 0;
		float64 speed = 0.;
		float64 position = 0.;
		int added = 0;
	};
	std::vector<Entry> entries;
	float64 speed = 1.;
};
struct FingerprintBadge {
	not_null<const FingerprintBadgeState*> state;
	rpl::producer<> repaints;
};
FingerprintBadge SetupFingerprintBadge(
	rpl::lifetime &on,
	rpl::producer<QByteArray> fingerprint);

void SetupFingerprintBadgeWidget(
	not_null<Ui::RpWidget*> widget,
	not_null<const FingerprintBadgeState*> state,
	rpl::producer<> repaints);

struct FingerprintBadgeCache {
	struct Emoji {
		EmojiPtr ptr = nullptr;
		QImage image;
	};
	struct Entry {
		std::vector<Emoji> emoji;
	};
	std::vector<Entry> entries;
	QImage shadow;
};
void PaintFingerprintEntry(
	QPainter &p,
	const FingerprintBadgeState::Entry &entry,
	FingerprintBadgeCache::Entry &cache,
	int esize);

} // namespace Calls
