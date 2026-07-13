/*
This file is part of Telegram Desktop,
the official desktop application for the Telegram messaging service.

For license and copyright information please follow this link:
https://github.com/telegramdesktop/tdesktop/blob/master/LEGAL
*/
#include "platform/win/windows_taskbar_buttons.h"

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

[[nodiscard]] HICON CreateThumbIcon(const style::icon &icon, QColor color) {
	const auto size = GetSystemMetrics(SM_CXSMICON);
	auto source = icon.instance(color, 100);
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
, _window(window) {
	using namespace Media::Player;

	refreshIcons();

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
	refreshIcons();
	_created = false;
	apply(currentState(), true);
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
	refreshIcons();
	if (_created) {
		apply(currentState(), false);
	}
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

void TaskbarButtons::refreshIcons() {
	const auto dark = IsDarkTaskbar();
	if (_iconsDark == dark && _previousIcon) {
		return;
	}
	destroyIcons();
	_iconsDark = dark;
	const auto color = dark.value_or(true)
		? QColor(255, 255, 255)
		: QColor(0, 0, 0);
	_previousIcon = CreateThumbIcon(st::windowTaskbarThumbPrevious, color);
	_playIcon = CreateThumbIcon(st::windowTaskbarThumbPlay, color);
	_pauseIcon = CreateThumbIcon(st::windowTaskbarThumbPause, color);
	_nextIcon = CreateThumbIcon(st::windowTaskbarThumbNext, color);
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
	if (!_created) {
		return;
	}
	const auto state = currentState();
	if (state == _applied) {
		return;
	}
	apply(state, false);
}

void TaskbarButtons::apply(State state, bool create) {
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
		_created = SUCCEEDED(result);
	}
	_applied = state;
}

} // namespace Platform
