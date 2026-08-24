/*
This file is part of Telegram Desktop,
the official desktop application for the Telegram messaging service.

For license and copyright information please follow this link:
https://github.com/telegramdesktop/tdesktop/blob/master/LEGAL
*/
#pragma once

#include "base/weak_ptr.h"

namespace Core {

// A reason may be held by an object that outlives this one, so the callbacks
// that mutate the registry are guarded rather than capturing a bare this.
class ScreenshotProtection final : public base::has_weak_ptr {
public:
	ScreenshotProtection();
	~ScreenshotProtection();

	void addReason(rpl::producer<bool> active);
	void addReason(rpl::producer<bool> active, rpl::lifetime &lifetime);

	[[nodiscard]] bool active() const;
	[[nodiscard]] rpl::producer<bool> activeValue() const;

private:
	void refresh();
	void apply(bool enabled);

	base::flat_map<uint64, bool> _reasons;
	uint64 _lastReasonId = 0;
	rpl::variable<bool> _active = false;
	std::unique_ptr<QObject> _filter;
	rpl::lifetime _lifetime;

};

} // namespace Core
