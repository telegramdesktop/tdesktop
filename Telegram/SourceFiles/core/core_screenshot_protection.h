/*
This file is part of Telegram Desktop,
the official desktop application for the Telegram messaging service.

For license and copyright information please follow this link:
https://github.com/telegramdesktop/tdesktop/blob/master/LEGAL
*/
#pragma once

#include "base/weak_ptr.h"

#include <QtWidgets/QWidget>

namespace Core {

// A reason may be held by an object that outlives this one, so the callbacks
// that mutate the registry are guarded rather than capturing a bare this.
class ScreenshotProtection final : public base::has_weak_ptr {
public:
	ScreenshotProtection();
	~ScreenshotProtection();

	// The window shows protected content itself.
	void addContentReason(
		not_null<QWidget*> window,
		rpl::producer<bool> active,
		rpl::lifetime &lifetime);

	// Only context around the window is protected, not the window content.
	void addAmbientReason(
		not_null<QWidget*> window,
		rpl::producer<bool> active,
		rpl::lifetime &lifetime);

private:
	void addReason(
		not_null<QWidget*> window,
		rpl::producer<bool> active,
		rpl::lifetime &lifetime);
	void refresh();
	void apply(not_null<QWidget*> window, bool force = false);
	[[nodiscard]] bool activeFor(not_null<QWidget*> window) const;

	struct Reason {
		QPointer<QWidget> window;
		bool active = false;
	};
	struct Applied {
		QPointer<QWidget> window;
		bool enabled = false;
	};
	base::flat_map<uint64, Reason> _reasons;
	uint64 _lastReasonId = 0;
	base::flat_map<not_null<QWidget*>, Applied> _applied;

};

} // namespace Core
