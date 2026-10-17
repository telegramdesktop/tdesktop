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

namespace Gram {

// Replaces the request signature of a Wallet rev00 key-rotation message with
// random bytes and keeps every other bit of it. The emulator is asked to skip
// signature checks, so the fee it reports stays the fee of the real rotation,
// while the message that leaves the device is one the contract can never
// execute: a fee quote must never be a rotation anybody could broadcast.
//
// Both the argument and the result are standard padded base64, the form the
// engine serializes a BOC in. An empty result means the message was not the
// single signed rotation request this expects, and the caller must then send
// nothing at all.
[[nodiscard]] QString BreakRotationSignature(const QString &bocBase64);

// A zero-opcode text comment, for display only.
[[nodiscard]] std::optional<QString> TextCommentFromBoc(
	const QString &bocBase64);

} // namespace Gram
