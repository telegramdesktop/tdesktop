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

} // namespace Calls
