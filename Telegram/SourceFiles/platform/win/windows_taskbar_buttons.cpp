/*
This file is part of Telegram Desktop,
the official desktop application for the Telegram messaging service.

For license and copyright information please follow this link:
https://github.com/telegramdesktop/tdesktop/blob/master/LEGAL
*/
#include "platform/win/windows_taskbar_buttons.h"

#include "base/platform/win/base_windows_safe_library.h"
#include "lang/lang_keys.h"
#include "media/audio/media_audio.h"
#include "media/player/media_player_instance.h"
#include "platform/win/tray_win.h"
#include "ui/ui_utility.h"
#include "styles/style_window.h"

// Defined in qtbase, converts a QPixmap to a native icon handle.
HICON qt_pixmapToWinHICON(const QPixmap &);

namespace Platform {
namespace {

enum class ButtonId : UINT {
	Previous = 1,
	PlayPause,
	Next,
};

constexpr auto kButtonsCount = 3;

// While Windows is switching between light and dark themes the explorer
// process churns through lots of section handles and the in-process
// TaskbarList::_ThumbBarUpdateButtons (explorerframe.dll) may re-lock
// its shared memory block by an already recycled handle and crash
// reading foreign memory, so postpone theme-driven updates till the
// system settles down.
constexpr auto kThemeApplyDelay = crl::time(5000);

UINT(__stdcall *GetDpiForWindow)(_In_ HWND hwnd);

int(__stdcall *GetSystemMetricsForDpi)(
	_In_ int nIndex,
	_In_ UINT dpi);

[[nodiscard]] bool DpiMetricsSupported() {
	static const auto Result = [&] {
#define LOAD_SYMBOL(lib, name) base::Platform::LoadMethod(lib, #name, name)
		const auto user32 = base::Platform::SafeLoadLibrary(L"User32.dll");
		return LOAD_SYMBOL(user32, GetDpiForWindow)
			&& LOAD_SYMBOL(user32, GetSystemMetricsForDpi);
#undef LOAD_SYMBOL
	}();
	return Result;
}

[[nodiscard]] int ThumbIconSize(HWND window) {
	if (DpiMetricsSupported()) {
		if (const auto dpi = GetDpiForWindow(window)) {
			return GetSystemMetricsForDpi(SM_CXSMICON, dpi);
		}
	}
	return GetSystemMetrics(SM_CXSMICON);
}

[[nodiscard]] HICON CreateThumbIcon(
		const style::icon &icon,
		QColor color,
		int size) {
	auto source = icon.instance(color, 100, true);
	const auto full = std::max(source.width(), source.height());
	if (full > 0 && full != size) {
		source = icon.instance(color, size * 100 / full, true);
	}
	auto scaled = (source.width() == size && source.height() == size)
		? std::move(source)
		: source.scaled(
			size,
			size,
			Qt::KeepAspectRatio,
			Qt::SmoothTransformation);
	auto result = QImage(size, size, QImage::Format_ARGB32_Premultiplied);
	result.fill(Qt::transparent);
	{
		auto p = QPainter(&result);
		p.drawImage(
			(size - scaled.width()) / 2,
			(size - scaled.height()) / 2,
			scaled);
	}
	return qt_pixmapToWinHICON(Ui::PixmapFromImage(std::move(result)));
}

void FillButton(
		THUMBBUTTON &button,
		ButtonId id,
		HICON icon,
		const QString &tooltip,
		bool active,
		bool enabled) {
	button.dwMask = THUMBBUTTONMASK(THB_ICON | THB_TOOLTIP | THB_FLAGS);
	button.iId = static_cast<UINT>(id);
	button.hIcon = icon;
	button.dwFlags = !active
		? THBF_HIDDEN
		: (enabled ? THBF_ENABLED : THBF_DISABLED);
	const auto tip = tooltip.toStdWString();
	const auto count = std::min(tip.size(), std::size(button.szTip) - 1);
	std::copy(tip.begin(), tip.begin() + count, button.szTip);
	button.szTip[count] = wchar_t(0);
}

} // namespace

TaskbarButtons::TaskbarButtons(not_null<ITaskbarList3*> taskbar, HWND window)
: _taskbar(taskbar)
, _window(window)
, _themeApplyTimer([=] { scheduleApply(); }) {
	using namespace Media::Player;

	const auto instance = Media::Player::instance();

	instance->updatedNotifier(
	) | rpl::filter([](const TrackState &state) {
		const auto type = state.id.type();
		return (type == AudioMsgId::Type::Song)
			|| (type == AudioMsgId::Type::Voice);
	}) | rpl::on_next([=](const TrackState &) {
		updateFromPlayer();
	}, _lifetime);

	rpl::merge(
		instance->stops(AudioMsgId::Type::Song),
		instance->stops(AudioMsgId::Type::Voice),
		instance->startsPlay(AudioMsgId::Type::Song),
		instance->startsPlay(AudioMsgId::Type::Voice),
		instance->playlistChanges(AudioMsgId::Type::Song),
		instance->playlistChanges(AudioMsgId::Type::Voice)
	) | rpl::on_next([=] {
		updateFromPlayer();
	}, _lifetime);
}

TaskbarButtons::~TaskbarButtons() {
	destroyIcons();
}

void TaskbarButtons::buttonsCreated() {
	_taskbarReady = true;
	_added = false;
	_applied = State();
	if (currentState().active) {
		scheduleApply();
	}
}

void TaskbarButtons::buttonClicked(int id) {
	const auto instance = Media::Player::instance();
	const auto type = instance->getActiveType();
	switch (static_cast<ButtonId>(id)) {
	case ButtonId::Previous: instance->previous(type); break;
	case ButtonId::PlayPause: instance->playPause(type); break;
	case ButtonId::Next: instance->next(type); break;
	}
}

void TaskbarButtons::refreshTheme() {
	_themeApplyTimer.callOnce(kThemeApplyDelay);
}

void TaskbarButtons::scheduleApply() {
	if (_applyScheduled) {
		return;
	}
	_applyScheduled = true;
	crl::on_main(this, [=] {
		_applyScheduled = false;
		if (!_taskbarReady) {
			return;
		}
		const auto state = currentState();
		const auto wasActive = _added && _applied.active;
		if (!state.active && !wasActive) {
			return;
		}
		const auto iconsChanged = refreshIcons();
		if (!_added) {
			apply(state, true);
		} else if (iconsChanged || state != _applied) {
			apply(state, false);
		}
	});
}

TaskbarButtons::State TaskbarButtons::currentState() const {
	using namespace Media::Player;

	const auto instance = Media::Player::instance();
	const auto type = instance->getActiveType();
	const auto state = instance->getState(type);
	auto result = State();
	result.active = bool(state.id);
	if (result.active) {
		result.playing = ShowPauseIcon(state.state);
		result.nextAvailable = instance->nextAvailable(type);
		result.previousAvailable = instance->previousAvailable(type);
	}
	return result;
}

bool TaskbarButtons::refreshIcons() {
	const auto dark = IsDarkTaskbar();
	const auto size = ThumbIconSize(_window);
	if (_iconsDark == dark && _iconsSize == size && _previousIcon) {
		return false;
	}
	destroyIcons();
	_iconsDark = dark;
	_iconsSize = size;
	const auto color = dark.value_or(true)
		? QColor(255, 255, 255)
		: QColor(0, 0, 0);
	_previousIcon = CreateThumbIcon(
		st::windowTaskbarThumbPrevious,
		color,
		size);
	_playIcon = CreateThumbIcon(st::windowTaskbarThumbPlay, color, size);
	_pauseIcon = CreateThumbIcon(st::windowTaskbarThumbPause, color, size);
	_nextIcon = CreateThumbIcon(st::windowTaskbarThumbNext, color, size);
	return true;
}

void TaskbarButtons::destroyIcons() {
	const auto icons = { _previousIcon, _playIcon, _pauseIcon, _nextIcon };
	for (const auto icon : icons) {
		if (icon) {
			DestroyIcon(icon);
		}
	}
	_previousIcon = _playIcon = _pauseIcon = _nextIcon = nullptr;
}

void TaskbarButtons::updateFromPlayer() {
	if (!_taskbarReady) {
		return;
	}
	scheduleApply();
}

void TaskbarButtons::apply(State state, bool create) {
	if (_applying) {
		scheduleApply();
		return;
	}
	_applying = true;
	const auto guard = gsl::finally([&] { _applying = false; });

	auto buttons = std::array<THUMBBUTTON, kButtonsCount>();

	FillButton(
		buttons[0],
		ButtonId::Previous,
		_previousIcon,
		tr::lng_mac_menu_player_previous(tr::now),
		state.active,
		state.previousAvailable);
	FillButton(
		buttons[1],
		ButtonId::PlayPause,
		state.playing ? _pauseIcon : _playIcon,
		(state.playing
			? tr::lng_mac_menu_player_pause(tr::now)
			: tr::lng_mac_menu_player_resume(tr::now)),
		state.active,
		true);
	FillButton(
		buttons[2],
		ButtonId::Next,
		_nextIcon,
		tr::lng_mac_menu_player_next(tr::now),
		state.active,
		state.nextAvailable);

	const auto result = create
		? _taskbar->ThumbBarAddButtons(
			_window,
			UINT(kButtonsCount),
			buttons.data())
		: _taskbar->ThumbBarUpdateButtons(
			_window,
			UINT(kButtonsCount),
			buttons.data());
	if (create) {
		_added = SUCCEEDED(result);
	}
	_applied = state;
}

} // namespace Platform
