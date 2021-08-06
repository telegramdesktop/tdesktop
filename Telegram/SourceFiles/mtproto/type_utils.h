/*
This file is part of Telegram Desktop,
the official desktop application for the Telegram messaging service.

For license and copyright information please follow this link:
https://github.com/telegramdesktop/tdesktop/blob/master/LEGAL
*/
#pragma once

inline MTPbool MTP_bool(bool v) {
	return v ? MTP_boolTrue() : MTP_boolFalse();
}

inline bool mtpIsTrue(const MTPBool &v) {
	return v.type() == mtpc_boolTrue;
}
inline bool mtpIsFalse(const MTPBool &v) {
	return !mtpIsTrue(v);
}
