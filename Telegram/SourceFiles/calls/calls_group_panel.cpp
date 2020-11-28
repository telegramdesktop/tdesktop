/*
This file is part of Telegram Desktop,
the official desktop application for the Telegram messaging service.

For license and copyright information please follow this link:
https://github.com/telegramdesktop/tdesktop/blob/master/LEGAL
*/
#include "calls/calls_group_panel.h"

#include "calls/calls_group_members.h"
#include "ui/widgets/buttons.h"
#include "ui/widgets/window.h"
#include "ui/widgets/call_button.h"
#include "ui/widgets/call_mute_button.h"
#include "ui/layers/layer_manager.h"
#include "boxes/confirm_box.h"
#include "core/application.h"
#include "lang/lang_keys.h"
#include "base/event_filter.h"
#include "app.h"
#include "styles/style_calls.h"

#ifdef Q_OS_WIN
#include "ui/platform/win/ui_window_title_win.h"
#endif // Q_OS_WIN

#include <QtWidgets/QDesktopWidget>
#include <QtWidgets/QApplication>
#include <QtGui/QWindow>

namespace Calls {

GroupPanel::GroupPanel(not_null<GroupCall*> call)
: _call(call)
, _channel(call->channel())
, _window(std::make_unique<Ui::Window>(Core::App().getModalParent()))
, _layerBg(std::make_unique<Ui::LayerManager>(_window->body()))
#ifdef Q_OS_WIN
, _controls(std::make_unique<Ui::Platform::TitleControls>(
	_window.get(),
	st::callTitle))
#endif // Q_OS_WIN
, _members(widget(), call)
, _settings(widget(), st::groupCallSettings)
, _mute(std::make_unique<Ui::CallMuteButton>(
	widget(),
	Ui::CallMuteButtonState{
		.text = tr::lng_group_call_connecting(tr::now),
		.type = Ui::CallMuteButtonType::Connecting,
	}))
, _hangup(widget(), st::callHangup) {
	initWindow();
	initWidget();
	initControls();
	initLayout();
	showAndActivate();
}

GroupPanel::~GroupPanel() = default;

void GroupPanel::showAndActivate() {
	if (_window->isHidden()) {
		_window->show();
	}
	_window->raise();
	_window->setWindowState(_window->windowState() | Qt::WindowActive);
	_window->activateWindow();
	_window->setFocus();
}

void GroupPanel::initWindow() {
	_window->setAttribute(Qt::WA_OpaquePaintEvent);
	_window->setAttribute(Qt::WA_NoSystemBackground);
	_window->setWindowIcon(
		QIcon(QPixmap::fromImage(Image::Empty()->original(), Qt::ColorOnly)));
	_window->setTitleStyle(st::callTitle);
	_window->setTitle(computeTitleRect()
		? u" "_q
		: tr::lng_group_call_title(tr::now));

	base::install_event_filter(_window.get(), [=](not_null<QEvent*> e) {
		if (e->type() == QEvent::Close && handleClose()) {
			e->ignore();
			return base::EventFilterResult::Cancel;
		}
		return base::EventFilterResult::Continue;
	});

	_window->setBodyTitleArea([=](QPoint widgetPoint) {
		using Flag = Ui::WindowTitleHitTestFlag;
		if (!widget()->rect().contains(widgetPoint)) {
			return Flag::None | Flag(0);
		}
#ifdef Q_OS_WIN
		if (_controls->geometry().contains(widgetPoint)) {
			return Flag::None | Flag(0);
		}
#endif // Q_OS_WIN
		const auto inControls = false;
		return inControls
			? Flag::None
			: (Flag::Move | Flag::Maximize);
	});
}

void GroupPanel::initWidget() {
	widget()->setMouseTracking(true);

	widget()->paintRequest(
	) | rpl::start_with_next([=](QRect clip) {
		paint(clip);
	}, widget()->lifetime());

	widget()->sizeValue(
	) | rpl::skip(1) | rpl::start_with_next([=] {
		updateControlsGeometry();
	}, widget()->lifetime());
}

void GroupPanel::initControls() {
	_mute->clicks(
	) | rpl::filter([=](Qt::MouseButton button) {
		return (button == Qt::LeftButton);
	}) | rpl::start_with_next([=] {
		if (_call && _call->muted() != MuteState::ForceMuted) {
			_call->setMuted((_call->muted() == MuteState::Active)
				? MuteState::Muted
				: MuteState::Active);
		}
	}, _mute->lifetime());

	_hangup->setClickedCallback([=] {
		_layerBg->showBox(Box<ConfirmBox>(
			tr::lng_group_call_leave_sure(tr::now),
			tr::lng_group_call_leave(tr::now),
			[=] { if (_call) _call->hangup(); }));
	});
	_settings->setClickedCallback([=] {
	});

	_settings->setText(tr::lng_menu_settings());
	_hangup->setText(tr::lng_box_leave());

	_members->desiredHeightValue(
	) | rpl::start_with_next([=] {
		updateControlsGeometry();
	}, _members->lifetime());

	initWithCall(_call);
}

void GroupPanel::initWithCall(GroupCall *call) {
	_callLifetime.destroy();
	_call = call;
	if (!_call) {
		return;
	}

	_channel = _call->channel();

	call->levelUpdates(
	) | rpl::filter([=](const LevelUpdate &update) {
		return update.self;
	}) | rpl::start_with_next([=](const LevelUpdate &update) {
		_mute->setLevel(update.value);
	}, _callLifetime);

	_members->toggleMuteRequests(
	) | rpl::start_with_next([=](GroupMembers::MuteRequest request) {
		if (_call) {
			_call->toggleMute(request.user, request.mute);
		}
	}, _callLifetime);

	using namespace rpl::mappers;
	rpl::combine(
		_call->mutedValue(),
		_call->stateValue() | rpl::map(
			_1 == State::Creating || _1 == State::Joining
		)
	) | rpl::start_with_next([=](MuteState mute, bool connecting) {
		_mute->setState(Ui::CallMuteButtonState{
			.text = (connecting
				? tr::lng_group_call_connecting(tr::now)
				: mute == MuteState::ForceMuted
				? tr::lng_group_call_force_muted(tr::now)
				: mute != MuteState::Active
				? tr::lng_call_unmute_audio(tr::now)
				: tr::lng_call_mute_audio(tr::now)),
			.type = (connecting
				? Ui::CallMuteButtonType::Connecting
				: mute == MuteState::ForceMuted
				? Ui::CallMuteButtonType::ForceMuted
				: mute == MuteState::Muted
				? Ui::CallMuteButtonType::Muted
				: Ui::CallMuteButtonType::Active),
		});
	}, _callLifetime);
}

void GroupPanel::initLayout() {
	initGeometry();

#ifdef Q_OS_WIN
	_controls->raise();
#endif // Q_OS_WIN
}

void GroupPanel::showControls() {
	Expects(_call != nullptr);

	widget()->showChildren();
}

void GroupPanel::closeBeforeDestroy() {
	_window->close();
	initWithCall(nullptr);
}

void GroupPanel::initGeometry() {
	const auto center = Core::App().getPointForCallPanelCenter();
	const auto rect = QRect(0, 0, st::groupCallWidth, st::groupCallHeight);
	_window->setGeometry(rect.translated(center - rect.center()));
	_window->setMinimumSize(rect.size());
	_window->show();
	updateControlsGeometry();
}

int GroupPanel::computeMembersListTop() const {
#ifdef Q_OS_WIN
	return st::callTitleButton.height + st::groupCallMembersMargin.top() / 2;
#elif defined Q_OS_MAC // Q_OS_WIN
	return st::groupCallMembersMargin.top() * 2;
#else // Q_OS_WIN || Q_OS_MAC
	return st::groupCallMembersMargin.top();
#endif // Q_OS_WIN || Q_OS_MAC
}

std::optional<QRect> GroupPanel::computeTitleRect() const {
#ifdef Q_OS_WIN
	const auto controls = _controls->geometry();
	return QRect(0, 0, controls.x(), controls.height());
#else // Q_OS_WIN
	return std::nullopt;
#endif // Q_OS_WIN
}

void GroupPanel::updateControlsGeometry() {
	if (widget()->size().isEmpty()) {
		return;
	}
	const auto desiredHeight = _members->desiredHeight();
	const auto membersWidthAvailable = widget()->width()
		- st::groupCallMembersMargin.left()
		- st::groupCallMembersMargin.right();
	const auto membersWidthMin = st::groupCallWidth
		- st::groupCallMembersMargin.left()
		- st::groupCallMembersMargin.right();
	const auto membersWidth = std::clamp(
		membersWidthAvailable,
		membersWidthMin,
		st::groupCallMembersWidthMax);
	const auto muteTop = widget()->height() - st::groupCallMuteBottomSkip;
	const auto buttonsTop = widget()->height() - st::groupCallButtonBottomSkip;
	const auto membersTop = computeMembersListTop();
	const auto availableHeight = muteTop
		- membersTop
		- st::groupCallMembersMargin.bottom();
	_members->setGeometry(
		(widget()->width() - membersWidth) / 2,
		membersTop,
		membersWidth,
		std::min(desiredHeight, availableHeight));
	const auto muteSize = _mute->innerSize().width();
	const auto fullWidth = muteSize
		+ 2 * _settings->width()
		+ 2 * st::groupCallButtonSkip;
	_mute->moveInner({ (widget()->width() - muteSize) / 2, muteTop });
	_settings->moveToLeft((widget()->width() - fullWidth) / 2, buttonsTop);
	_hangup->moveToRight((widget()->width() - fullWidth) / 2, buttonsTop);
	refreshTitle();
}

void GroupPanel::refreshTitle() {
	if (const auto titleRect = computeTitleRect()) {
		if (!_title) {
			_title.create(
				widget(),
				tr::lng_group_call_title(),
				st::groupCallHeaderLabel);
			_title->setAttribute(Qt::WA_TransparentForMouseEvents);
			_window->setTitle(u" "_q);
		}
		const auto best = _title->naturalWidth();
		const auto from = (widget()->width() - best) / 2;
		const auto top = (computeMembersListTop() - _title->height()) / 2;
		const auto left = titleRect->x();
		if (from >= left && from + best <= left + titleRect->width()) {
			_title->resizeToWidth(best);
			_title->moveToLeft(from, top);
		} else if (titleRect->width() < best) {
			_title->resizeToWidth(titleRect->width());
			_title->moveToLeft(left, top);
		} else if (from < left) {
			_title->resizeToWidth(best);
			_title->moveToLeft(left, top);
		} else {
			_title->resizeToWidth(best);
			_title->moveToLeft(left + titleRect->width() - best, top);
		}
	} else if (_title) {
		_title.destroy();
		_window->setTitle(tr::lng_group_call_title(tr::now));
	}
}

void GroupPanel::paint(QRect clip) {
	Painter p(widget());

	auto region = QRegion(clip);
	for (const auto rect : region) {
		p.fillRect(rect, st::groupCallBg);
	}
}

bool GroupPanel::handleClose() {
	if (_call) {
		_window->hide();
		return true;
	}
	return false;
}

not_null<Ui::RpWidget*> GroupPanel::widget() const {
	return _window->body();
}

} // namespace Calls
