/*
This file is part of Telegram Desktop,
the official desktop application for the Telegram messaging service.

For license and copyright information please follow this link:
https://github.com/telegramdesktop/tdesktop/blob/master/LEGAL
*/
#pragma once

#include "base/platform/win/base_windows_shlobj_h.h"
#include "base/timer.h"

namespace Platform {

class TaskbarButtons final : public base::has_weak_ptr {
public:
	TaskbarButtons(not_null<ITaskbarList3*> taskbar, HWND window);
	~TaskbarButtons();

	[[nodiscard]] HWND window() const {
		return _window;
	}

	void buttonsCreated();
	void buttonClicked(int id);
	void refreshTheme();

private:
	struct State {
		bool active = false;
		bool playing = false;
		bool nextAvailable = false;
		bool previousAvailable = false;

		friend bool operator==(const State &, const State &) = default;
	};

	[[nodiscard]] State currentState() const;
	bool refreshIcons();
	void destroyIcons();
	void scheduleApply();
	void apply(State state, bool create);
	void updateFromPlayer();

	const not_null<ITaskbarList3*> _taskbar;
	const HWND _window;

	HICON _previousIcon = nullptr;
	HICON _playIcon = nullptr;
	HICON _pauseIcon = nullptr;
	HICON _nextIcon = nullptr;
	std::optional<bool> _iconsDark;
	int _iconsSize = 0;

	State _applied;
	bool _taskbarReady = false;
	bool _added = false;
	bool _applying = false;
	bool _applyScheduled = false;

	base::Timer _themeApplyTimer;

	rpl::lifetime _lifetime;

};

} // namespace Platform
