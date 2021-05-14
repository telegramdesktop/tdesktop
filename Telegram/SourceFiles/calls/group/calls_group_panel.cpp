/*
This file is part of Telegram Desktop,
the official desktop application for the Telegram messaging service.

For license and copyright information please follow this link:
https://github.com/telegramdesktop/tdesktop/blob/master/LEGAL
*/
#include "calls/group/calls_group_panel.h"

#include "calls/group/calls_group_common.h"
#include "calls/group/calls_group_members.h"
#include "calls/group/calls_group_settings.h"
#include "calls/group/calls_group_menu.h"
#include "calls/group/calls_group_large_video.h"
#include "calls/group/ui/desktop_capture_choose_source.h"
#include "ui/platform/ui_platform_window_title.h"
#include "ui/platform/ui_platform_utility.h"
#include "ui/controls/call_mute_button.h"
#include "ui/widgets/buttons.h"
#include "ui/widgets/window.h"
#include "ui/widgets/call_button.h"
#include "ui/widgets/checkbox.h"
#include "ui/widgets/dropdown_menu.h"
#include "ui/widgets/input_fields.h"
#include "ui/chat/group_call_bar.h"
#include "ui/layers/layer_manager.h"
#include "ui/layers/generic_box.h"
#include "ui/text/text_utilities.h"
#include "ui/toasts/common_toasts.h"
#include "ui/round_rect.h"
#include "ui/special_buttons.h"
#include "info/profile/info_profile_values.h" // Info::Profile::Value.
#include "core/application.h"
#include "lang/lang_keys.h"
#include "data/data_channel.h"
#include "data/data_chat.h"
#include "data/data_user.h"
#include "data/data_group_call.h"
#include "data/data_session.h"
#include "data/data_changes.h"
#include "data/data_peer_values.h"
#include "main/main_session.h"
#include "base/event_filter.h"
#include "boxes/peers/edit_participants_box.h"
#include "boxes/peers/add_participants_box.h"
#include "boxes/peer_lists_box.h"
#include "boxes/confirm_box.h"
#include "base/unixtime.h"
#include "base/timer_rpl.h"
#include "app.h"
#include "apiwrap.h" // api().kickParticipant.
#include "webrtc/webrtc_video_track.h"
#include "styles/style_calls.h"
#include "styles/style_layers.h"

#include <QtWidgets/QDesktopWidget>
#include <QtWidgets/QApplication>
#include <QtGui/QWindow>
#include <QtGui/QScreen>

namespace Calls::Group {
namespace {

constexpr auto kSpacePushToTalkDelay = crl::time(250);
constexpr auto kRecordingAnimationDuration = crl::time(1200);
constexpr auto kRecordingOpacity = 0.6;
constexpr auto kStartNoConfirmation = TimeId(10);
constexpr auto kControlsBackgroundOpacity = 0.8;
constexpr auto kOverrideActiveColorBgAlpha = 172;

class InviteController final : public ParticipantsBoxController {
public:
	InviteController(
		not_null<PeerData*> peer,
		base::flat_set<not_null<UserData*>> alreadyIn);

	void prepare() override;

	void rowClicked(not_null<PeerListRow*> row) override;
	base::unique_qptr<Ui::PopupMenu> rowContextMenu(
		QWidget *parent,
		not_null<PeerListRow*> row) override;

	void itemDeselectedHook(not_null<PeerData*> peer) override;

	[[nodiscard]] auto peersWithRows() const
		-> not_null<const base::flat_set<not_null<UserData*>>*>;
	[[nodiscard]] rpl::producer<not_null<UserData*>> rowAdded() const;

	[[nodiscard]] bool hasRowFor(not_null<PeerData*> peer) const;

private:
	[[nodiscard]] bool isAlreadyIn(not_null<UserData*> user) const;

	std::unique_ptr<PeerListRow> createRow(
		not_null<PeerData*> participant) const override;

	not_null<PeerData*> _peer;
	const base::flat_set<not_null<UserData*>> _alreadyIn;
	mutable base::flat_set<not_null<UserData*>> _inGroup;
	rpl::event_stream<not_null<UserData*>> _rowAdded;

};

class InviteContactsController final : public AddParticipantsBoxController {
public:
	InviteContactsController(
		not_null<PeerData*> peer,
		base::flat_set<not_null<UserData*>> alreadyIn,
		not_null<const base::flat_set<not_null<UserData*>>*> inGroup,
		rpl::producer<not_null<UserData*>> discoveredInGroup);

private:
	void prepareViewHook() override;

	std::unique_ptr<PeerListRow> createRow(
		not_null<UserData*> user) override;

	bool needsInviteLinkButton() override {
		return false;
	}

	const not_null<const base::flat_set<not_null<UserData*>>*> _inGroup;
	rpl::producer<not_null<UserData*>> _discoveredInGroup;

	rpl::lifetime _lifetime;

};

[[nodiscard]] rpl::producer<QString> StartsWhenText(
		rpl::producer<TimeId> date) {
	return std::move(
		date
	) | rpl::map([](TimeId date) -> rpl::producer<QString> {
		const auto parsedDate = base::unixtime::parse(date);
		const auto dateDay = QDateTime(parsedDate.date(), QTime(0, 0));
		const auto previousDay = QDateTime(
			parsedDate.date().addDays(-1),
			QTime(0, 0));
		const auto now = QDateTime::currentDateTime();
		const auto kDay = int64(24 * 60 * 60);
		const auto tillTomorrow = int64(now.secsTo(previousDay));
		const auto tillToday = tillTomorrow + kDay;
		const auto tillAfter = tillToday + kDay;

		const auto time = parsedDate.time().toString(
			QLocale::system().timeFormat(QLocale::ShortFormat));
		auto exact = tr::lng_group_call_starts_short_date(
			lt_date,
			rpl::single(langDayOfMonthFull(dateDay.date())),
			lt_time,
			rpl::single(time)
		) | rpl::type_erased();
		auto tomorrow = tr::lng_group_call_starts_short_tomorrow(
			lt_time,
			rpl::single(time));
		auto today = tr::lng_group_call_starts_short_today(
			lt_time,
			rpl::single(time));

		auto todayAndAfter = rpl::single(
			std::move(today)
		) | rpl::then(base::timer_once(
			std::min(tillAfter, kDay) * crl::time(1000)
		) | rpl::map([=] {
			return rpl::duplicate(exact);
		})) | rpl::flatten_latest() | rpl::type_erased();

		auto tomorrowAndAfter = rpl::single(
			std::move(tomorrow)
		) | rpl::then(base::timer_once(
			std::min(tillToday, kDay) * crl::time(1000)
		) | rpl::map([=] {
			return rpl::duplicate(todayAndAfter);
		})) | rpl::flatten_latest() | rpl::type_erased();

		auto full = rpl::single(
			rpl::duplicate(exact)
		) | rpl::then(base::timer_once(
			tillTomorrow * crl::time(1000)
		) | rpl::map([=] {
			return rpl::duplicate(tomorrowAndAfter);
		})) | rpl::flatten_latest() | rpl::type_erased();

		if (tillTomorrow > 0) {
			return full;
		} else if (tillToday > 0) {
			return tomorrowAndAfter;
		} else if (tillAfter > 0) {
			return todayAndAfter;
		} else {
			return exact;
		}
	}) | rpl::flatten_latest();
}

[[nodiscard]] object_ptr<Ui::RpWidget> CreateGradientLabel(
		QWidget *parent,
		rpl::producer<QString> text) {
	struct State {
		QBrush brush;
		QPainterPath path;
	};
	auto result = object_ptr<Ui::RpWidget>(parent);
	const auto raw = result.data();
	const auto state = raw->lifetime().make_state<State>();

	std::move(
		text
	) | rpl::start_with_next([=](const QString &text) {
		state->path = QPainterPath();
		const auto &font = st::groupCallCountdownFont;
		state->path.addText(0, font->ascent, font->f, text);
		const auto width = font->width(text);
		raw->resize(width, font->height);
		auto gradient = QLinearGradient(QPoint(width, 0), QPoint());
		gradient.setStops(QGradientStops{
			{ 0.0, st::groupCallForceMutedBar1->c },
			{ .7, st::groupCallForceMutedBar2->c },
			{ 1.0, st::groupCallForceMutedBar3->c }
		});
		state->brush = QBrush(std::move(gradient));
		raw->update();
	}, raw->lifetime());

	raw->paintRequest(
	) | rpl::start_with_next([=] {
		auto p = QPainter(raw);
		auto hq = PainterHighQualityEnabler(p);
		const auto skip = st::groupCallWidth / 20;
		const auto available = parent->width() - 2 * skip;
		const auto full = raw->width();
		if (available > 0 && full > available) {
			const auto scale = available / float64(full);
			const auto shift = raw->rect().center();
			p.translate(shift);
			p.scale(scale, scale);
			p.translate(-shift);
		}
		p.setPen(Qt::NoPen);
		p.setBrush(state->brush);
		p.drawPath(state->path);
	}, raw->lifetime());
	return result;
}

[[nodiscard]] object_ptr<Ui::RpWidget> CreateSectionSubtitle(
		QWidget *parent,
		rpl::producer<QString> text) {
	auto result = object_ptr<Ui::FixedHeightWidget>(
		parent,
		st::searchedBarHeight);

	const auto raw = result.data();
	raw->paintRequest(
	) | rpl::start_with_next([=](QRect clip) {
		auto p = QPainter(raw);
		p.fillRect(clip, st::groupCallMembersBgOver);
	}, raw->lifetime());

	const auto label = Ui::CreateChild<Ui::FlatLabel>(
		raw,
		std::move(text),
		st::groupCallBoxLabel);
	raw->widthValue(
	) | rpl::start_with_next([=](int width) {
		const auto padding = st::groupCallInviteDividerPadding;
		const auto available = width - padding.left() - padding.right();
		label->resizeToNaturalWidth(available);
		label->moveToLeft(padding.left(), padding.top(), width);
	}, label->lifetime());

	return result;
}

InviteController::InviteController(
	not_null<PeerData*> peer,
	base::flat_set<not_null<UserData*>> alreadyIn)
: ParticipantsBoxController(CreateTag{}, nullptr, peer, Role::Members)
, _peer(peer)
, _alreadyIn(std::move(alreadyIn)) {
	SubscribeToMigration(
		_peer,
		lifetime(),
		[=](not_null<ChannelData*> channel) { _peer = channel; });
}

void InviteController::prepare() {
	delegate()->peerListSetHideEmpty(true);
	ParticipantsBoxController::prepare();
	delegate()->peerListSetAboveWidget(CreateSectionSubtitle(
		nullptr,
		tr::lng_group_call_invite_members()));
	delegate()->peerListSetAboveSearchWidget(CreateSectionSubtitle(
		nullptr,
		tr::lng_group_call_invite_members()));
}

void InviteController::rowClicked(not_null<PeerListRow*> row) {
	delegate()->peerListSetRowChecked(row, !row->checked());
}

base::unique_qptr<Ui::PopupMenu> InviteController::rowContextMenu(
		QWidget *parent,
		not_null<PeerListRow*> row) {
	return nullptr;
}

void InviteController::itemDeselectedHook(not_null<PeerData*> peer) {
}

bool InviteController::hasRowFor(not_null<PeerData*> peer) const {
	return (delegate()->peerListFindRow(peer->id.value) != nullptr);
}

bool InviteController::isAlreadyIn(not_null<UserData*> user) const {
	return _alreadyIn.contains(user);
}

std::unique_ptr<PeerListRow> InviteController::createRow(
		not_null<PeerData*> participant) const {
	const auto user = participant->asUser();
	if (!user || user->isSelf() || user->isBot()) {
		return nullptr;
	}
	auto result = std::make_unique<PeerListRow>(user);
	_rowAdded.fire_copy(user);
	_inGroup.emplace(user);
	if (isAlreadyIn(user)) {
		result->setDisabledState(PeerListRow::State::DisabledChecked);
	}
	return result;
}

auto InviteController::peersWithRows() const
-> not_null<const base::flat_set<not_null<UserData*>>*> {
	return &_inGroup;
}

rpl::producer<not_null<UserData*>> InviteController::rowAdded() const {
	return _rowAdded.events();
}

InviteContactsController::InviteContactsController(
	not_null<PeerData*> peer,
	base::flat_set<not_null<UserData*>> alreadyIn,
	not_null<const base::flat_set<not_null<UserData*>>*> inGroup,
	rpl::producer<not_null<UserData*>> discoveredInGroup)
: AddParticipantsBoxController(peer, std::move(alreadyIn))
, _inGroup(inGroup)
, _discoveredInGroup(std::move(discoveredInGroup)) {
}

void InviteContactsController::prepareViewHook() {
	AddParticipantsBoxController::prepareViewHook();

	delegate()->peerListSetAboveWidget(CreateSectionSubtitle(
		nullptr,
		tr::lng_contacts_header()));
	delegate()->peerListSetAboveSearchWidget(CreateSectionSubtitle(
		nullptr,
		tr::lng_group_call_invite_search_results()));

	std::move(
		_discoveredInGroup
	) | rpl::start_with_next([=](not_null<UserData*> user) {
		if (auto row = delegate()->peerListFindRow(user->id.value)) {
			delegate()->peerListRemoveRow(row);
		}
	}, _lifetime);
}

std::unique_ptr<PeerListRow> InviteContactsController::createRow(
		not_null<UserData*> user) {
	return _inGroup->contains(user)
		? nullptr
		: AddParticipantsBoxController::createRow(user);
}

} // namespace

struct Panel::VideoTile {
	std::unique_ptr<LargeVideo> video;
	VideoEndpoint endpoint;
};

Panel::Panel(not_null<GroupCall*> call)
: _call(call)
, _peer(call->peer())
, _window(std::make_unique<Ui::Window>())
, _layerBg(std::make_unique<Ui::LayerManager>(_window->body()))
#ifndef Q_OS_MAC
, _controls(std::make_unique<Ui::Platform::TitleControls>(
	_window->body(),
	st::groupCallTitle))
#endif // !Q_OS_MAC
, _videoMode(true) // #TODO calls
, _mute(std::make_unique<Ui::CallMuteButton>(
	widget(),
	st::callMuteButton,
	Core::App().appDeactivatedValue(),
	Ui::CallMuteButtonState{
		.text = (_call->scheduleDate()
			? tr::lng_group_call_start_now(tr::now)
			: tr::lng_group_call_connecting(tr::now)),
		.type = (!_call->scheduleDate()
			? Ui::CallMuteButtonType::Connecting
			: _peer->canManageGroupCall()
			? Ui::CallMuteButtonType::ScheduledCanStart
			: _call->scheduleStartSubscribed()
			? Ui::CallMuteButtonType::ScheduledNotify
			: Ui::CallMuteButtonType::ScheduledSilent),
	}))
, _hangup(widget(), st::groupCallHangup) {
	_layerBg->setStyleOverrides(&st::groupCallBox, &st::groupCallLayerBox);
	_layerBg->setHideByBackgroundClick(true);

	SubscribeToMigration(
		_peer,
		_window->lifetime(),
		[=](not_null<ChannelData*> channel) { migrate(channel); });
	setupRealCallViewers();

	initWindow();
	initWidget();
	initControls();
	initLayout();
	showAndActivate();
	setupJoinAsChangedToasts();
	setupTitleChangedToasts();
	setupAllowedToSpeakToasts();
}

Panel::~Panel() {
	if (_menu) {
		_menu.destroy();
	}
	_videoTiles.clear();
}

void Panel::setupRealCallViewers() {
	_call->real(
	) | rpl::start_with_next([=](not_null<Data::GroupCall*> real) {
		subscribeToChanges(real);
	}, _window->lifetime());
}

bool Panel::isActive() const {
	return _window->isActiveWindow()
		&& _window->isVisible()
		&& !(_window->windowState() & Qt::WindowMinimized);
}

void Panel::minimize() {
	_window->setWindowState(_window->windowState() | Qt::WindowMinimized);
}

void Panel::close() {
	_window->close();
}

void Panel::showAndActivate() {
	if (_window->isHidden()) {
		_window->show();
	}
	const auto state = _window->windowState();
	if (state & Qt::WindowMinimized) {
		_window->setWindowState(state & ~Qt::WindowMinimized);
	}
	_window->raise();
	_window->activateWindow();
	_window->setFocus();
}

void Panel::migrate(not_null<ChannelData*> channel) {
	_peer = channel;
	_peerLifetime.destroy();
	subscribeToPeerChanges();
	_title.destroy();
	refreshTitle();
}

void Panel::subscribeToPeerChanges() {
	Info::Profile::NameValue(
		_peer
	) | rpl::start_with_next([=](const TextWithEntities &name) {
		_window->setTitle(name.text);
	}, _peerLifetime);
}

QWidget *Panel::chooseSourceParent() {
	return _window.get();
}

QString Panel::chooseSourceActiveDeviceId() {
	return _call->screenSharingDeviceId();
}

rpl::lifetime &Panel::chooseSourceInstanceLifetime() {
	return _window->lifetime();
}

void Panel::chooseSourceAccepted(const QString &deviceId) {
	_call->toggleScreenSharing(deviceId);
}

void Panel::chooseSourceStop() {
	_call->toggleScreenSharing(std::nullopt);
}

void Panel::initWindow() {
	_window->setAttribute(Qt::WA_OpaquePaintEvent);
	_window->setAttribute(Qt::WA_NoSystemBackground);
	_window->setWindowIcon(
		QIcon(QPixmap::fromImage(Image::Empty()->original(), Qt::ColorOnly)));
	_window->setTitleStyle(st::groupCallTitle);

	subscribeToPeerChanges();

	base::install_event_filter(_window.get(), [=](not_null<QEvent*> e) {
		if (e->type() == QEvent::Close && handleClose()) {
			e->ignore();
			return base::EventFilterResult::Cancel;
		} else if (e->type() == QEvent::KeyPress
			|| e->type() == QEvent::KeyRelease) {
			if (static_cast<QKeyEvent*>(e.get())->key() == Qt::Key_Space) {
				_call->pushToTalk(
					e->type() == QEvent::KeyPress,
					kSpacePushToTalkDelay);
			}
		}
		return base::EventFilterResult::Continue;
	});

	_window->setBodyTitleArea([=](QPoint widgetPoint) {
		using Flag = Ui::WindowTitleHitTestFlag;
		const auto titleRect = QRect(
			0,
			0,
			widget()->width(),
			st::groupCallMembersTop);
		return (titleRect.contains(widgetPoint)
			&& (!_menuToggle || !_menuToggle->geometry().contains(widgetPoint))
			&& (!_menu || !_menu->geometry().contains(widgetPoint))
			&& (!_recordingMark || !_recordingMark->geometry().contains(widgetPoint))
			&& (!_joinAsToggle || !_joinAsToggle->geometry().contains(widgetPoint)))
			? (Flag::Move | Flag::Maximize)
			: Flag::None;
	});

	_call->videoCallValue(
	) | rpl::start_with_next([=] {
		updateMode();
	}, _window->lifetime());
}

void Panel::initWidget() {
	widget()->setMouseTracking(true);

	widget()->paintRequest(
	) | rpl::start_with_next([=](QRect clip) {
		paint(clip);
	}, widget()->lifetime());

	widget()->sizeValue(
	) | rpl::skip(1) | rpl::start_with_next([=](QSize size) {
		if (!updateMode()) {
			updateControlsGeometry();
		}

		// title geometry depends on _controls->geometry,
		// which is not updated here yet.
		crl::on_main(widget(), [=] { refreshTitle(); });
	}, widget()->lifetime());
}

void Panel::endCall() {
	if (!_call->peer()->canManageGroupCall()) {
		_call->hangup();
		return;
	}
	_layerBg->showBox(Box(
		LeaveBox,
		_call,
		false,
		BoxContext::GroupCallPanel));
}

void Panel::startScheduledNow() {
	const auto date = _call->scheduleDate();
	const auto now = base::unixtime::now();
	if (!date) {
		return;
	} else if (now + kStartNoConfirmation >= date) {
		_call->startScheduledNow();
	} else {
		const auto box = std::make_shared<QPointer<Ui::GenericBox>>();
		const auto done = [=] {
			if (*box) {
				(*box)->closeBox();
			}
			_call->startScheduledNow();
		};
		auto owned = ConfirmBox({
			.text = { tr::lng_group_call_start_now_sure(tr::now) },
			.button = tr::lng_group_call_start_now(),
			.callback = done,
		});
		*box = owned.data();
		_layerBg->showBox(std::move(owned));
	}
}

void Panel::initControls() {
	_mute->clicks(
	) | rpl::filter([=](Qt::MouseButton button) {
		return (button == Qt::LeftButton);
	}) | rpl::start_with_next([=] {
		if (_call->scheduleDate()) {
			if (_peer->canManageGroupCall()) {
				startScheduledNow();
			} else if (const auto real = _call->lookupReal()) {
				_call->toggleScheduleStartSubscribed(
					!real->scheduleStartSubscribed());
			}
			return;
		}
		const auto oldState = _call->muted();
		const auto newState = (oldState == MuteState::ForceMuted)
			? MuteState::RaisedHand
			: (oldState == MuteState::RaisedHand)
			? MuteState::RaisedHand
			: (oldState == MuteState::Muted)
			? MuteState::Active
			: MuteState::Muted;
		_call->setMutedAndUpdate(newState);
	}, _mute->lifetime());

	initShareAction();
	refreshLeftButton();

	_hangup->setClickedCallback([=] { endCall(); });

	const auto scheduleDate = _call->scheduleDate();
	_hangup->setText(scheduleDate
		? tr::lng_group_call_close()
		: tr::lng_group_call_leave());
	if (scheduleDate) {
		auto changes = _call->real(
		) | rpl::map([=](not_null<Data::GroupCall*> real) {
			return real->scheduleDateValue();
		}) | rpl::flatten_latest();

		setupScheduledLabels(rpl::single(
			scheduleDate
		) | rpl::then(rpl::duplicate(changes)));

		auto started = std::move(changes) | rpl::filter([](TimeId date) {
			return (date == 0);
		}) | rpl::take(1);

		rpl::merge(
			rpl::duplicate(started) | rpl::to_empty,
			_peer->session().changes().peerFlagsValue(
				_peer,
				Data::PeerUpdate::Flag::Username
			) | rpl::skip(1) | rpl::to_empty
		) | rpl::start_with_next([=] {
			refreshLeftButton();
			updateControlsGeometry();
		}, _callLifetime);

		std::move(started) | rpl::start_with_next([=] {
			_hangup->setText(tr::lng_group_call_leave());
			setupMembers();
		}, _callLifetime);
	}

	_call->stateValue(
	) | rpl::filter([](State state) {
		return (state == State::HangingUp)
			|| (state == State::Ended)
			|| (state == State::FailedHangingUp)
			|| (state == State::Failed);
	}) | rpl::start_with_next([=] {
		closeBeforeDestroy();
	}, _callLifetime);

	_call->levelUpdates(
	) | rpl::filter([=](const LevelUpdate &update) {
		return update.me;
	}) | rpl::start_with_next([=](const LevelUpdate &update) {
		_mute->setLevel(update.value);
	}, _callLifetime);

	_call->real(
	) | rpl::start_with_next([=](not_null<Data::GroupCall*> real) {
		setupRealMuteButtonState(real);
	}, _callLifetime);
}

void Panel::refreshLeftButton() {
	const auto share = _call->scheduleDate()
		&& _peer->isBroadcast()
		&& _peer->asChannel()->hasUsername();
	if ((share && _callShare) || (!share && _settings)) {
		return;
	}
	if (share) {
		_settings.destroy();
		_callShare.create(widget(), st::groupCallShare);
		_callShare->setClickedCallback(_callShareLinkCallback);
		_callShare->setText(tr::lng_group_call_share_button());
	} else {
		_callShare.destroy();
		_settings.create(widget(), st::groupCallSettings);
		_settings->setClickedCallback([=] {
			_layerBg->showBox(Box(SettingsBox, _call));
		});
		_settings->setText(tr::lng_group_call_settings());
	}
	const auto raw = _callShare ? _callShare.data() : _settings.data();
	raw->show();

	auto overrides = _mute->colorOverrides();
	raw->setColorOverrides(rpl::duplicate(overrides));

	auto toggleableOverrides = [&](rpl::producer<bool> active) {
		return rpl::combine(
			std::move(active),
			rpl::duplicate(overrides)
		) | rpl::map([](bool active, Ui::CallButtonColors colors) {
			if (active && colors.bg) {
				colors.bg->setAlpha(kOverrideActiveColorBgAlpha);
			}
			return colors;
		});
	};
	if (!_video) {
		_video.create(
			widget(),
			st::groupCallVideoSmall,
			&st::groupCallVideoActiveSmall);
		_video->show();
		_video->setClickedCallback([=] {
			_call->toggleVideo(!_call->isSharingCamera());
		});
		_video->setText(tr::lng_group_call_video());
		_video->setColorOverrides(
			toggleableOverrides(_call->isSharingCameraValue()));
		_call->isSharingCameraValue(
		) | rpl::start_with_next([=](bool sharing) {
			_video->setProgress(sharing ? 1. : 0.);
		}, _video->lifetime());
	}
	if (!_screenShare) {
		_screenShare.create(widget(), st::groupCallScreenShareSmall);
		_screenShare->show();
		_screenShare->setClickedCallback([=] {
			Ui::DesktopCapture::ChooseSource(this);
		});
		_screenShare->setText(tr::lng_group_call_screen_share());
		_screenShare->setColorOverrides(
			toggleableOverrides(_call->isSharingScreenValue()));
		_call->isSharingScreenValue(
		) | rpl::start_with_next([=](bool sharing) {
			_screenShare->setProgress(sharing ? 1. : 0.);
		}, _screenShare->lifetime());
	}
}

void Panel::initShareAction() {
	const auto showBox = [=](object_ptr<Ui::BoxContent> next) {
		_layerBg->showBox(std::move(next));
	};
	const auto showToast = [=](QString text) {
		Ui::ShowMultilineToast({
			.parentOverride = widget(),
			.text = { text },
		});
	};
	auto [shareLinkCallback, shareLinkLifetime] = ShareInviteLinkAction(
		_peer,
		showBox,
		showToast);
	_callShareLinkCallback = [=, callback = std::move(shareLinkCallback)] {
		if (_call->lookupReal()) {
			callback();
		}
	};
	widget()->lifetime().add(std::move(shareLinkLifetime));
}

void Panel::setupRealMuteButtonState(not_null<Data::GroupCall*> real) {
	using namespace rpl::mappers;
	rpl::combine(
		_call->mutedValue() | MapPushToTalkToActive(),
		_call->instanceStateValue(),
		real->scheduleDateValue(),
		real->scheduleStartSubscribedValue(),
		Data::CanManageGroupCallValue(_peer),
		_videoMode.value()
	) | rpl::distinct_until_changed(
	) | rpl::filter(
		_2 != GroupCall::InstanceState::TransitionToRtc
	) | rpl::start_with_next([=](
			MuteState mute,
			GroupCall::InstanceState state,
			TimeId scheduleDate,
			bool scheduleStartSubscribed,
			bool canManage,
			bool videoMode) {
		using Type = Ui::CallMuteButtonType;
		_mute->setState(Ui::CallMuteButtonState{
			.text = (scheduleDate
				? (canManage
					? tr::lng_group_call_start_now(tr::now)
					: scheduleStartSubscribed
					? tr::lng_group_call_cancel_reminder(tr::now)
					: tr::lng_group_call_set_reminder(tr::now))
				: state == GroupCall::InstanceState::Disconnected
				? tr::lng_group_call_connecting(tr::now)
				: mute == MuteState::ForceMuted
				? (videoMode
					? tr::lng_group_call_force_muted_small(tr::now)
					: tr::lng_group_call_force_muted(tr::now))
				: mute == MuteState::RaisedHand
				? (videoMode
					? tr::lng_group_call_raised_hand_small(tr::now)
					: tr::lng_group_call_raised_hand(tr::now))
				: mute == MuteState::Muted
				? tr::lng_group_call_unmute(tr::now)
				: (videoMode
					? tr::lng_group_call_you_are_live_small(tr::now)
					: tr::lng_group_call_you_are_live(tr::now))),
			.subtext = ((scheduleDate || videoMode)
				? QString()
				: state == GroupCall::InstanceState::Disconnected
				? QString()
				: mute == MuteState::ForceMuted
				? tr::lng_group_call_raise_hand_tip(tr::now)
				: mute == MuteState::RaisedHand
				? tr::lng_group_call_raised_hand_sub(tr::now)
				: mute == MuteState::Muted
				? tr::lng_group_call_unmute_sub(tr::now)
				: QString()),
			.type = (scheduleDate
				? (canManage
					? Type::ScheduledCanStart
					: scheduleStartSubscribed
					? Type::ScheduledNotify
					: Type::ScheduledSilent)
				: state == GroupCall::InstanceState::Disconnected
				? Type::Connecting
				: mute == MuteState::ForceMuted
				? Type::ForceMuted
				: mute == MuteState::RaisedHand
				? Type::RaisedHand
				: mute == MuteState::Muted
				? Type::Muted
				: Type::Active),
		});
	}, _callLifetime);
}

void Panel::setupScheduledLabels(rpl::producer<TimeId> date) {
	using namespace rpl::mappers;
	date = std::move(date) | rpl::take_while(_1 != 0);
	_startsWhen.create(
		widget(),
		StartsWhenText(rpl::duplicate(date)),
		st::groupCallStartsWhen);
	auto countdownCreated = std::move(
		date
	) | rpl::map([=](TimeId date) {
		_countdownData = std::make_shared<Ui::GroupCallScheduledLeft>(date);
		return rpl::empty_value();
	}) | rpl::start_spawning(widget()->lifetime());

	_countdown = CreateGradientLabel(widget(), rpl::duplicate(
		countdownCreated
	) | rpl::map([=] {
		return _countdownData->text(
			Ui::GroupCallScheduledLeft::Negative::Ignore);
	}) | rpl::flatten_latest());

	_startsIn.create(
		widget(),
		rpl::conditional(
			std::move(
				countdownCreated
			) | rpl::map(
				[=] { return _countdownData->late(); }
			) | rpl::flatten_latest(),
			tr::lng_group_call_late_by(),
			tr::lng_group_call_starts_in()),
		st::groupCallStartsIn);

	const auto top = [=] {
		const auto muteTop = widget()->height() - st::groupCallMuteBottomSkip;
		const auto membersTop = st::groupCallMembersTop;
		const auto height = st::groupCallScheduledBodyHeight;
		return (membersTop + (muteTop - membersTop - height) / 2);
	};
	rpl::combine(
		widget()->sizeValue(),
		_startsIn->widthValue()
	) | rpl::start_with_next([=](QSize size, int width) {
		_startsIn->move(
			(size.width() - width) / 2,
			top() + st::groupCallStartsInTop);
	}, _startsIn->lifetime());

	rpl::combine(
		widget()->sizeValue(),
		_startsWhen->widthValue()
	) | rpl::start_with_next([=](QSize size, int width) {
		_startsWhen->move(
			(size.width() - width) / 2,
			top() + st::groupCallStartsWhenTop);
	}, _startsWhen->lifetime());

	rpl::combine(
		widget()->sizeValue(),
		_countdown->widthValue()
	) | rpl::start_with_next([=](QSize size, int width) {
		_countdown->move(
			(size.width() - width) / 2,
			top() + st::groupCallCountdownTop);
	}, _startsWhen->lifetime());
}

void Panel::setupMembers() {
	if (_members) {
		return;
	}

	_startsIn.destroy();
	_countdown.destroy();
	_startsWhen.destroy();

	_members.create(widget(), _call);
	setupPinnedVideo();

	_members->setMode(_mode);
	_members->show();

	_members->desiredHeightValue(
	) | rpl::start_with_next([=] {
		updateMembersGeometry();
	}, _members->lifetime());

	_members->toggleMuteRequests(
	) | rpl::start_with_next([=](MuteRequest request) {
		if (_call) {
			_call->toggleMute(request);
		}
	}, _callLifetime);

	_members->changeVolumeRequests(
	) | rpl::start_with_next([=](VolumeRequest request) {
		if (_call) {
			_call->changeVolume(request);
		}
	}, _callLifetime);

	_members->kickParticipantRequests(
	) | rpl::start_with_next([=](not_null<PeerData*> participantPeer) {
		kickParticipant(participantPeer);
	}, _callLifetime);

	_members->addMembersRequests(
	) | rpl::start_with_next([=] {
		if (_peer->isBroadcast() && _peer->asChannel()->hasUsername()) {
			_callShareLinkCallback();
		} else {
			addMembers();
		}
	}, _callLifetime);

	_members->enlargeVideo(
	) | rpl::start_with_next([=] {
		enlargeVideo();
	}, _callLifetime);

	_call->videoEndpointPinnedValue(
	) | rpl::start_with_next([=](const VideoEndpoint &pinned) {
		if (_mode == PanelMode::Wide) {
			refreshTilesGeometry();
		} else if (pinned) {
			enlargeVideo();
		}
	}, _callLifetime);
}

void Panel::enlargeVideo() {
	_lastSmallGeometry = _window->geometry();

	const auto available = _window->screen()->availableGeometry();
	const auto width = std::max(
		_window->width(),
		std::max(
			std::min(available.width(), st::groupCallWideModeSize.width()),
			st::groupCallWideModeWidthMin));
	const auto height = std::max(
		_window->height(),
		std::min(available.height(), st::groupCallWideModeSize.height()));
	auto geometry = QRect(
		_window->x() - (width - _window->width()) / 2,
		_window->y() - (height - _window->height()) / 2,
		width,
		height);
	if (geometry.x() < available.x()) {
		geometry.setX(std::min(available.x(), _window->x()));
	}
	if (geometry.x() + geometry.width()
		> available.x() + available.width()) {
		geometry.setX(std::max(
			available.x() + available.width(),
			_window->x() + _window->width()) - geometry.width());
	}
	if (geometry.y() < available.y()) {
		geometry.setY(std::min(available.y(), _window->y()));
	}
	if (geometry.y() + geometry.height() > available.y() + available.height()) {
		geometry.setY(std::max(
			available.y() + available.height(),
			_window->y() + _window->height()) - geometry.height());
	}
	if (_lastLargeMaximized) {
		_window->setWindowState(
			_window->windowState() | Qt::WindowMaximized);
	} else {
		_window->setGeometry((_lastLargeGeometry
			&& available.intersects(*_lastLargeGeometry))
			? *_lastLargeGeometry
			: geometry);
	}
}

void Panel::minimizeVideo() {
	if (_window->windowState() & Qt::WindowMaximized) {
		_lastLargeMaximized = true;
		_window->setWindowState(
			_window->windowState() & ~Qt::WindowMaximized);
	} else {
		_lastLargeMaximized = false;
		_lastLargeGeometry = _window->geometry();
	}
	const auto available = _window->screen()->availableGeometry();
	const auto width = st::groupCallWidth;
	const auto height = st::groupCallHeight;
	auto geometry = QRect(
		_window->x() + (_window->width() - width) / 2,
		_window->y() + (_window->height() - height) / 2,
		width,
		height);
	_window->setGeometry((_lastSmallGeometry
		&& available.intersects(*_lastSmallGeometry))
		? *_lastSmallGeometry
		: geometry);
}

void Panel::raiseControls() {
	if (_controlsBackground) {
		_controlsBackground->raise();
	}
	const auto buttons = {
		&_settings,
		&_callShare,
		&_screenShare,
		&_video,
		&_hangup
	};
	for (const auto button : buttons) {
		if (const auto raw = button->data()) {
			raw->raise();
		}
	}
	_mute->raise();
}

void Panel::refreshTilesGeometry() {
	const auto outer = _pinnedVideoWrap->size();
	if (_videoTiles.empty()
		|| outer.isEmpty()
		|| _mode == PanelMode::Default) {
		trackControls(nullptr);
		return;
	}
	struct Geometry {
		QSize size;
		QRect columns;
		QRect rows;
	};
	const auto &pinned = _call->videoEndpointPinned();
	auto sizes = base::flat_map<not_null<LargeVideo*>, Geometry>();
	sizes.reserve(_videoTiles.size());
	for (const auto &tile : _videoTiles) {
		const auto video = tile.video.get();
		const auto size = (pinned && tile.endpoint != pinned)
			? QSize()
			: video->trackSize();
		if (size.isEmpty()) {
			video->toggleControlsHidingEnabled(false);
			video->setGeometry(0, 0, outer.width(), 0);
		} else {
			sizes.emplace(video, Geometry{ size });
		}
	}
	if (sizes.size() == 1) {
		trackControls(sizes.front().first);
		sizes.front().first->toggleControlsHidingEnabled(true);
		sizes.front().first->setGeometry(0, 0, outer.width(), outer.height());
		return;
	}
	if (sizes.empty()) {
		return;
	}

	auto columnsBlack = uint64();
	auto rowsBlack = uint64();
	const auto count = int(sizes.size());
	const auto skip = st::groupCallVideoLargeSkip;
	const auto slices = int(std::ceil(std::sqrt(float64(count))));
	{
		auto index = 0;
		const auto columns = slices;
		const auto sizew = (outer.width() + skip) / float64(columns);
		for (auto column = 0; column != columns; ++column) {
			const auto left = int(std::round(column * sizew));
			const auto width = int(std::round(column * sizew + sizew - skip))
				- left;
			const auto rows = int(std::round((count - index)
				/ float64(columns - column)));
			const auto sizeh = (outer.height() + skip) / float64(rows);
			for (auto row = 0; row != rows; ++row) {
				const auto top = int(std::round(row * sizeh));
				const auto height = int(std::round(
					row * sizeh + sizeh - skip)) - top;
				auto &geometry = (sizes.begin() + index)->second;
				geometry.columns = {
					left,
					top,
					width,
					height };
				const auto scaled = geometry.size.scaled(
					width,
					height,
					Qt::KeepAspectRatio);
				columnsBlack += (scaled.width() < width)
					? (width - scaled.width()) * height
					: (height - scaled.height()) * width;
				++index;
			}
		}
	}
	{
		auto index = 0;
		const auto rows = slices;
		const auto sizeh = (outer.height() + skip) / float64(rows);
		for (auto row = 0; row != rows; ++row) {
			const auto top = int(std::round(row * sizeh));
			const auto height = int(std::round(row * sizeh + sizeh - skip))
				- top;
			const auto columns = int(std::round((count - index)
				/ float64(rows - row)));
			const auto sizew = (outer.width() + skip) / float64(columns);
			for (auto column = 0; column != columns; ++column) {
				const auto left = int(std::round(column * sizew));
				const auto width = int(std::round(
					column * sizew + sizew - skip)) - left;
				auto &geometry = (sizes.begin() + index)->second;
				geometry.rows = {
					left,
					top,
					width,
					height };
				const auto scaled = geometry.size.scaled(
					width,
					height,
					Qt::KeepAspectRatio);
				rowsBlack += (scaled.width() < width)
					? (width - scaled.width()) * height
					: (height - scaled.height()) * width;
				++index;
			}
		}
	}
	for (const auto &[video, geometry] : sizes) {
		const auto &rect = (columnsBlack < rowsBlack)
			? geometry.columns
			: geometry.rows;
		video->toggleControlsHidingEnabled(false);
		video->setGeometry(rect.x(), rect.y(), rect.width(), rect.height());
	}
}

void Panel::setupPinnedVideo() {
	using namespace rpl::mappers;
	_pinnedVideoWrap = std::make_unique<Ui::RpWidget>(widget());

	const auto setupTile = [=](
			const VideoEndpoint &endpoint,
			const GroupCall::VideoTrack &track) {
		const auto row = _members->lookupRow(track.peer);
		Assert(row != nullptr);
		auto video = std::make_unique<LargeVideo>(
			_pinnedVideoWrap.get(),
			st::groupCallLargeVideoWide,
			(_mode == PanelMode::Wide),
			rpl::single(LargeVideoTrack{ track.track.get(), row }),
			_call->videoEndpointPinnedValue() | rpl::map(_1 == endpoint));

		video->pinToggled(
		) | rpl::start_with_next([=](bool pinned) {
			_call->pinVideoEndpoint(pinned ? endpoint : VideoEndpoint{});
		}, video->lifetime());

		video->requestedQuality(
		) | rpl::start_with_next([=](VideoQuality quality) {
			_call->requestVideoQuality(endpoint, quality);
		}, video->lifetime());

		video->trackSizeValue(
		) | rpl::start_with_next([=] {
			refreshTilesGeometry();
		}, video->lifetime());

		video->lifetime().add([=, video = video.get()] {
			if (_trackControlsTile == video) {
				trackControls(nullptr);
			}
		});

		return VideoTile{
			.video = std::move(video),
			.endpoint = endpoint,
		};
	};
	for (const auto &[endpoint, track] : _call->activeVideoTracks()) {
		_videoTiles.push_back(setupTile(endpoint, track));
	}
	_call->videoStreamActiveUpdates(
	) | rpl::start_with_next([=](const VideoEndpoint &endpoint) {
		if (_call->activeVideoTracks().contains(endpoint)) {
			// Add async (=> the participant row is definitely in Members).
			crl::on_main(_pinnedVideoWrap.get(), [=] {
				const auto &tracks = _call->activeVideoTracks();
				const auto i = tracks.find(endpoint);
				if (i != end(tracks)) {
					_videoTiles.push_back(setupTile(endpoint, i->second));
				}
			});
		} else {
			// Remove sync.
			const auto eraseTill = end(_videoTiles);
			const auto eraseFrom = ranges::remove(
				_videoTiles,
				endpoint,
				&VideoTile::endpoint);
			if (eraseFrom != eraseTill) {
				_videoTiles.erase(eraseFrom, eraseTill);
				refreshTilesGeometry();
			}
		}
	}, _pinnedVideoWrap->lifetime());

	_pinnedVideoWrap->sizeValue() | rpl::start_with_next([=] {
		refreshTilesGeometry();
	}, _pinnedVideoWrap->lifetime());

	raiseControls();
}

void Panel::setupJoinAsChangedToasts() {
	_call->rejoinEvents(
	) | rpl::filter([](RejoinEvent event) {
		return (event.wasJoinAs != event.nowJoinAs);
	}) | rpl::map([=] {
		return _call->stateValue() | rpl::filter([](State state) {
			return (state == State::Joined);
		}) | rpl::take(1);
	}) | rpl::flatten_latest() | rpl::start_with_next([=] {
		Ui::ShowMultilineToast({
			.parentOverride = widget(),
			.text = tr::lng_group_call_join_as_changed(
				tr::now,
				lt_name,
				Ui::Text::Bold(_call->joinAs()->name),
				Ui::Text::WithEntities),
		});
	}, widget()->lifetime());
}

void Panel::setupTitleChangedToasts() {
	_call->titleChanged(
	) | rpl::filter([=] {
		return (_call->lookupReal() != nullptr);
	}) | rpl::map([=] {
		return _peer->groupCall()->title().isEmpty()
			? _peer->name
			: _peer->groupCall()->title();
	}) | rpl::start_with_next([=](const QString &title) {
		Ui::ShowMultilineToast({
			.parentOverride = widget(),
			.text = tr::lng_group_call_title_changed(
				tr::now,
				lt_title,
				Ui::Text::Bold(title),
				Ui::Text::WithEntities),
		});
	}, widget()->lifetime());
}

void Panel::setupAllowedToSpeakToasts() {
	_call->allowedToSpeakNotifications(
	) | rpl::start_with_next([=] {
		if (isActive()) {
			Ui::ShowMultilineToast({
				.parentOverride = widget(),
				.text = { tr::lng_group_call_can_speak_here(tr::now) },
				});
		} else {
			const auto real = _call->lookupReal();
			const auto name = (real && !real->title().isEmpty())
				? real->title()
				: _peer->name;
			Ui::ShowMultilineToast({
				.text = tr::lng_group_call_can_speak(
					tr::now,
					lt_chat,
					Ui::Text::Bold(name),
					Ui::Text::WithEntities),
				});
		}
	}, widget()->lifetime());
}

void Panel::subscribeToChanges(not_null<Data::GroupCall*> real) {
	const auto validateRecordingMark = [=](bool recording) {
		if (!recording && _recordingMark) {
			_recordingMark.destroy();
		} else if (recording && !_recordingMark) {
			struct State {
				Ui::Animations::Simple animation;
				base::Timer timer;
				bool opaque = true;
			};
			_recordingMark.create(widget());
			_recordingMark->show();
			const auto state = _recordingMark->lifetime().make_state<State>();
			const auto size = st::groupCallRecordingMark;
			const auto skip = st::groupCallRecordingMarkSkip;
			_recordingMark->resize(size + 2 * skip, size + 2 * skip);
			_recordingMark->setClickedCallback([=] {
				Ui::ShowMultilineToast({
					.parentOverride = widget(),
					.text = { tr::lng_group_call_is_recorded(tr::now) },
				});
			});
			const auto animate = [=] {
				const auto opaque = state->opaque;
				state->opaque = !opaque;
				state->animation.start(
					[=] { _recordingMark->update(); },
					opaque ? 1. : kRecordingOpacity,
					opaque ? kRecordingOpacity : 1.,
					kRecordingAnimationDuration);
			};
			state->timer.setCallback(animate);
			state->timer.callEach(kRecordingAnimationDuration);
			animate();

			_recordingMark->paintRequest(
			) | rpl::start_with_next([=] {
				auto p = QPainter(_recordingMark.data());
				auto hq = PainterHighQualityEnabler(p);
				p.setPen(Qt::NoPen);
				p.setBrush(st::groupCallMemberMutedIcon);
				p.setOpacity(state->animation.value(
					state->opaque ? 1. : kRecordingOpacity));
				p.drawEllipse(skip, skip, size, size);
			}, _recordingMark->lifetime());
		}
		refreshTitleGeometry();
	};

	using namespace rpl::mappers;
	real->recordStartDateChanges(
	) | rpl::map(
		_1 != 0
	) | rpl::distinct_until_changed(
	) | rpl::start_with_next([=](bool recorded) {
		validateRecordingMark(recorded);
		Ui::ShowMultilineToast({
			.parentOverride = widget(),
			.text = (recorded
				? tr::lng_group_call_recording_started
				: _call->recordingStoppedByMe()
				? tr::lng_group_call_recording_saved
				: tr::lng_group_call_recording_stopped)(
					tr::now,
					Ui::Text::RichLangValue),
		});
	}, widget()->lifetime());
	validateRecordingMark(real->recordStartDate() != 0);

	const auto showMenu = _peer->canManageGroupCall();
	const auto showUserpic = !showMenu && _call->showChooseJoinAs();
	if (showMenu) {
		_joinAsToggle.destroy();
		if (!_menuToggle) {
			_menuToggle.create(widget(), st::groupCallMenuToggle);
			_menuToggle->show();
			_menuToggle->setClickedCallback([=] { showMainMenu(); });
		}
	} else if (showUserpic) {
		_menuToggle.destroy();
		rpl::single(
			_call->joinAs()
		) | rpl::then(_call->rejoinEvents(
		) | rpl::map([](const RejoinEvent &event) {
			return event.nowJoinAs;
		})) | rpl::start_with_next([=](not_null<PeerData*> joinAs) {
			auto joinAsToggle = object_ptr<Ui::UserpicButton>(
				widget(),
				joinAs,
				Ui::UserpicButton::Role::Custom,
				st::groupCallJoinAsToggle);
			_joinAsToggle.destroy();
			_joinAsToggle = std::move(joinAsToggle);
			_joinAsToggle->show();
			_joinAsToggle->setClickedCallback([=] {
				chooseJoinAs();
			});
			updateControlsGeometry();
		}, widget()->lifetime());
	} else {
		_menuToggle.destroy();
		_joinAsToggle.destroy();
	}
	updateControlsGeometry();
}

void Panel::chooseJoinAs() {
	const auto context = ChooseJoinAsProcess::Context::Switch;
	const auto callback = [=](JoinInfo info) {
		_call->rejoinAs(info);
	};
	const auto showBox = [=](object_ptr<Ui::BoxContent> next) {
		_layerBg->showBox(std::move(next));
	};
	const auto showToast = [=](QString text) {
		Ui::ShowMultilineToast({
			.parentOverride = widget(),
			.text = { text },
		});
	};
	_joinAsProcess.start(
		_peer,
		context,
		showBox,
		showToast,
		callback,
		_call->joinAs());
}

void Panel::showMainMenu() {
	if (_menu) {
		return;
	}
	_menu.create(widget(), st::groupCallDropdownMenu);
	FillMenu(
		_menu.data(),
		_peer,
		_call,
		[=] { chooseJoinAs(); },
		[=](auto box) { _layerBg->showBox(std::move(box)); });
	if (_menu->empty()) {
		_menu.destroy();
		return;
	}

	const auto raw = _menu.data();
	raw->setHiddenCallback([=] {
		raw->deleteLater();
		if (_menu == raw) {
			_menu = nullptr;
			_menuToggle->setForceRippled(false);
		}
	});
	raw->setShowStartCallback([=] {
		if (_menu == raw) {
			_menuToggle->setForceRippled(true);
		}
	});
	raw->setHideStartCallback([=] {
		if (_menu == raw) {
			_menuToggle->setForceRippled(false);
		}
	});
	_menuToggle->installEventFilter(_menu);

	const auto x = st::groupCallMenuPosition.x();
	const auto y = st::groupCallMenuPosition.y();
	if (_menuToggle->x() > widget()->width() / 2) {
		_menu->moveToRight(x, y);
		_menu->showAnimated(Ui::PanelAnimation::Origin::TopRight);
	} else {
		_menu->moveToLeft(x, y);
		_menu->showAnimated(Ui::PanelAnimation::Origin::TopLeft);
	}
}

void Panel::addMembers() {
	const auto real = _call->lookupReal();
	if (!real) {
		return;
	}
	auto alreadyIn = _peer->owner().invitedToCallUsers(real->id());
	for (const auto &participant : real->participants()) {
		if (const auto user = participant.peer->asUser()) {
			alreadyIn.emplace(user);
		}
	}
	alreadyIn.emplace(_peer->session().user());
	auto controller = std::make_unique<InviteController>(
		_peer,
		alreadyIn);
	controller->setStyleOverrides(
		&st::groupCallInviteMembersList,
		&st::groupCallMultiSelect);

	auto contactsController = std::make_unique<InviteContactsController>(
		_peer,
		std::move(alreadyIn),
		controller->peersWithRows(),
		controller->rowAdded());
	contactsController->setStyleOverrides(
		&st::groupCallInviteMembersList,
		&st::groupCallMultiSelect);

	const auto weak = base::make_weak(_call.get());
	const auto invite = [=](const std::vector<not_null<UserData*>> &users) {
		const auto call = weak.get();
		if (!call) {
			return;
		}
		const auto result = call->inviteUsers(users);
		if (const auto user = std::get_if<not_null<UserData*>>(&result)) {
			Ui::ShowMultilineToast({
				.parentOverride = widget(),
				.text = tr::lng_group_call_invite_done_user(
					tr::now,
					lt_user,
					Ui::Text::Bold((*user)->firstName),
					Ui::Text::WithEntities),
			});
		} else if (const auto count = std::get_if<int>(&result)) {
			if (*count > 0) {
				Ui::ShowMultilineToast({
					.parentOverride = widget(),
					.text = tr::lng_group_call_invite_done_many(
						tr::now,
						lt_count,
						*count,
						Ui::Text::RichLangValue),
				});
			}
		} else {
			Unexpected("Result in GroupCall::inviteUsers.");
		}
	};
	const auto inviteWithAdd = [=](
			const std::vector<not_null<UserData*>> &users,
			const std::vector<not_null<UserData*>> &nonMembers,
			Fn<void()> finish) {
		_peer->session().api().addChatParticipants(
			_peer,
			nonMembers,
			[=](bool) { invite(users); finish(); });
	};
	const auto inviteWithConfirmation = [=](
			const std::vector<not_null<UserData*>> &users,
			const std::vector<not_null<UserData*>> &nonMembers,
			Fn<void()> finish) {
		if (nonMembers.empty()) {
			invite(users);
			finish();
			return;
		}
		const auto name = _peer->name;
		const auto text = (nonMembers.size() == 1)
			? tr::lng_group_call_add_to_group_one(
				tr::now,
				lt_user,
				nonMembers.front()->shortName(),
				lt_group,
				name)
			: (nonMembers.size() < users.size())
			? tr::lng_group_call_add_to_group_some(tr::now, lt_group, name)
			: tr::lng_group_call_add_to_group_all(tr::now, lt_group, name);
		const auto shared = std::make_shared<QPointer<Ui::GenericBox>>();
		const auto finishWithConfirm = [=] {
			if (*shared) {
				(*shared)->closeBox();
			}
			finish();
		};
		const auto done = [=] {
			inviteWithAdd(users, nonMembers, finishWithConfirm);
		};
		auto box = ConfirmBox({
			.text = { text },
			.button = tr::lng_participant_invite(),
			.callback = done,
		});
		*shared = box.data();
		_layerBg->showBox(std::move(box));
	};
	auto initBox = [=, controller = controller.get()](
			not_null<PeerListsBox*> box) {
		box->setTitle(tr::lng_group_call_invite_title());
		box->addButton(tr::lng_group_call_invite_button(), [=] {
			const auto rows = box->collectSelectedRows();

			const auto users = ranges::views::all(
				rows
			) | ranges::views::transform([](not_null<PeerData*> peer) {
				return not_null<UserData*>(peer->asUser());
			}) | ranges::to_vector;

			const auto nonMembers = ranges::views::all(
				users
			) | ranges::views::filter([&](not_null<UserData*> user) {
				return !controller->hasRowFor(user);
			}) | ranges::to_vector;

			const auto finish = [box = Ui::MakeWeak(box)]() {
				if (box) {
					box->closeBox();
				}
			};
			inviteWithConfirmation(users, nonMembers, finish);
		});
		box->addButton(tr::lng_cancel(), [=] { box->closeBox(); });
	};

	auto controllers = std::vector<std::unique_ptr<PeerListController>>();
	controllers.push_back(std::move(controller));
	controllers.push_back(std::move(contactsController));
	_layerBg->showBox(Box<PeerListsBox>(std::move(controllers), initBox));
}

void Panel::kickParticipant(not_null<PeerData*> participantPeer) {
	_layerBg->showBox(Box([=](not_null<Ui::GenericBox*> box) {
		box->addRow(
			object_ptr<Ui::FlatLabel>(
				box.get(),
				(!participantPeer->isUser()
					? tr::lng_group_call_remove_channel(
						tr::now,
						lt_channel,
						participantPeer->name)
					: (_peer->isBroadcast()
						? tr::lng_profile_sure_kick_channel
						: tr::lng_profile_sure_kick)(
						tr::now,
						lt_user,
						participantPeer->asUser()->firstName)),
				st::groupCallBoxLabel),
			style::margins(
				st::boxRowPadding.left(),
				st::boxPadding.top(),
				st::boxRowPadding.right(),
				st::boxPadding.bottom()));
		box->addButton(tr::lng_box_remove(), [=] {
			box->closeBox();
			kickParticipantSure(participantPeer);
		});
		box->addButton(tr::lng_cancel(), [=] { box->closeBox(); });
	}));
}

void Panel::kickParticipantSure(not_null<PeerData*> participantPeer) {
	if (const auto chat = _peer->asChat()) {
		chat->session().api().kickParticipant(chat, participantPeer);
	} else if (const auto channel = _peer->asChannel()) {
		const auto currentRestrictedRights = [&] {
			const auto user = participantPeer->asUser();
			if (!channel->mgInfo || !user) {
				return ChannelData::EmptyRestrictedRights(participantPeer);
			}
			const auto i = channel->mgInfo->lastRestricted.find(user);
			return (i != channel->mgInfo->lastRestricted.cend())
				? i->second.rights
				: ChannelData::EmptyRestrictedRights(participantPeer);
		}();
		channel->session().api().kickParticipant(
			channel,
			participantPeer,
			currentRestrictedRights);
	}
}

void Panel::initLayout() {
	initGeometry();

#ifndef Q_OS_MAC
	_controls->raise();

	Ui::Platform::TitleControlsLayoutChanged(
	) | rpl::start_with_next([=] {
		// _menuToggle geometry depends on _controls arrangement.
		crl::on_main(widget(), [=] { updateControlsGeometry(); });
	}, widget()->lifetime());

#endif // !Q_OS_MAC
}

void Panel::showControls() {
	Expects(_call != nullptr);

	widget()->showChildren();
}

void Panel::closeBeforeDestroy() {
	_window->close();
	_callLifetime.destroy();
}

void Panel::initGeometry() {
	const auto center = Core::App().getPointForCallPanelCenter();
	const auto rect = QRect(0, 0, st::groupCallWidth, st::groupCallHeight);
	_window->setGeometry(rect.translated(center - rect.center()));
	_window->setMinimumSize(rect.size());
	_window->show();
	updateControlsGeometry();
}

QRect Panel::computeTitleRect() const {
	const auto skip = st::groupCallTitleTop;
	const auto remove = skip + (_menuToggle
		? (_menuToggle->width() + st::groupCallMenuTogglePosition.x())
		: 0) + (_joinAsToggle
			? (_joinAsToggle->width() + st::groupCallMenuTogglePosition.x())
			: 0);
	const auto width = widget()->width();
#ifdef Q_OS_MAC
	return QRect(70, 0, width - remove - 70, 28);
#else // Q_OS_MAC
	const auto controls = _controls->geometry();
	const auto right = controls.x() + controls.width() + skip;
	return (controls.center().x() < width / 2)
		? QRect(right, 0, width - right - remove, controls.height())
		: QRect(remove, 0, controls.x() - skip - remove, controls.height());
#endif // !Q_OS_MAC
}

bool Panel::updateMode() {
	const auto wide = _call->videoCall()
		&& (widget()->width() >= st::groupCallWideModeWidthMin);
	const auto mode = wide ? PanelMode::Wide : PanelMode::Default;
	if (_mode == mode) {
		return false;
	}
	_mode = mode;
	if (_title) {
		_title->setTextColorOverride(wide
			? std::make_optional(st::groupCallMemberNotJoinedStatus->c)
			: std::nullopt);
	}
	if (wide && _subtitle) {
		_subtitle.destroy();
	} else if (!wide && !_subtitle) {
		refreshTitle();
	}
	if (_members) {
		_members->setMode(mode);
	}
	if (_pinnedVideoWrap) {
		_pinnedVideoWrap->setVisible(mode == PanelMode::Wide);
		for (const auto &tile : _videoTiles) {
			tile.video->setVisible(mode == PanelMode::Wide);
		}
	}
	refreshControlsBackground();
	updateControlsGeometry();
	return true;
}

void Panel::refreshControlsBackground() {
	if (_mode != PanelMode::Wide) {
		_trackControlsLifetime.destroy();
		_controlsBackground.destroy();
	} else if (_controlsBackground) {
		return;
	}
	_controlsBackground.create(widget());
	_controlsBackground->show();
	auto &lifetime = _controlsBackground->lifetime();
	const auto color = lifetime.make_state<style::complex_color>([] {
		auto result = st::groupCallBg->c;
		result.setAlphaF(kControlsBackgroundOpacity);
		return result;
	});
	const auto corners = lifetime.make_state<Ui::RoundRect>(
		st::groupCallControlsBackRadius,
		color->color());
	_controlsBackground->paintRequest(
	) | rpl::start_with_next([=] {
		auto p = QPainter(_controlsBackground.data());
		corners->paint(p, _controlsBackground->rect());
	}, lifetime);

	raiseControls();
}

void Panel::trackControls(LargeVideo *video) {
	if (_trackControlsTile == video) {
		return;
	}
	_trackControlsTile = video;
	if (!video) {
		_trackControlsLifetime.destroy();
		_trackControlsOverStateLifetime.destroy();
		if (_pinnedVideoControlsShown != 1.) {
			_pinnedVideoControlsShown = 1.;
			updateButtonsGeometry();
		}
		return;
	}

	const auto trackOne = [=](auto &&widget) {
		if (widget) {
			widget->events(
			) | rpl::start_with_next([=](not_null<QEvent*> e) {
				if (e->type() == QEvent::Enter) {
					video->setControlsShown(true);
				} else if (e->type() == QEvent::Leave) {
					video->setControlsShown(false);
				}
			}, _trackControlsOverStateLifetime);
		}
	};
	const auto trackOverState = [=] {
		trackOne(_mute);
		trackOne(_video);
		trackOne(_screenShare);
		trackOne(_settings);
		trackOne(_callShare);
		trackOne(_hangup);
		trackOne(_controlsBackground);
	};

	video->controlsShown(
	) | rpl::filter([=](float64 shown) {
		return (_pinnedVideoControlsShown != shown);
	}) | rpl::start_with_next([=](float64 shown) {
		const auto hiding = (shown <= _pinnedVideoControlsShown);
		_pinnedVideoControlsShown = shown;
		if (hiding && _trackControlsLifetime) {
			_trackControlsOverStateLifetime.destroy();
		} else if (!hiding && !_trackControlsOverStateLifetime) {
			trackOverState();
		}
		updateButtonsGeometry();
	}, _trackControlsLifetime);
}

void Panel::updateControlsGeometry() {
	if (widget()->size().isEmpty() || (!_settings && !_callShare)) {
		return;
	}
	updateButtonsGeometry();
	updateMembersGeometry();
	refreshTitle();

#ifdef Q_OS_MAC
	const auto controlsOnTheLeft = true;
#else // Q_OS_MAC
	const auto controlsOnTheLeft = _controls->geometry().center().x()
		< widget()->width() / 2;
#endif // Q_OS_MAC
	const auto menux = st::groupCallMenuTogglePosition.x();
	const auto menuy = st::groupCallMenuTogglePosition.y();
	if (controlsOnTheLeft) {
		if (_menuToggle) {
			_menuToggle->moveToRight(menux, menuy);
		} else if (_joinAsToggle) {
			_joinAsToggle->moveToRight(menux, menuy);
		}
	} else {
		if (_menuToggle) {
			_menuToggle->moveToLeft(menux, menuy);
		} else if (_joinAsToggle) {
			_joinAsToggle->moveToLeft(menux, menuy);
		}
	}
}

void Panel::updateButtonsGeometry() {
	const auto toggle = [&](bool shown) {
		const auto toggleOne = [&](auto &widget) {
			if (widget && widget->isHidden() == shown) {
				widget->setVisible(shown);
			}
		};
		toggleOne(_mute);
		toggleOne(_video);
		toggleOne(_screenShare);
		toggleOne(_settings);
		toggleOne(_callShare);
		toggleOne(_hangup);
	};
	if (_videoMode.current()) {
		_mute->setStyle(st::callMuteButtonSmall);
		toggle(_mode != PanelMode::Wide || _pinnedVideoControlsShown > 0.);

		const auto buttonsTop = widget()->height()
			- (_mode == PanelMode::Wide
				? anim::interpolate(
					0,
					st::groupCallButtonBottomSkipWide,
					_pinnedVideoControlsShown)
				: st::groupCallButtonBottomSkipSmall);
		const auto addSkip = st::callMuteButtonSmall.active.outerRadius;
		const auto muteSize = _mute->innerSize().width() + 2 * addSkip;
		const auto skip = (_video ? 1 : 2) * st::groupCallButtonSkipSmall;
		const auto fullWidth = muteSize
			+ (_video ? _video->width() + skip : 0)
			+ (_screenShare ? _screenShare->width() + skip : 0)
			+ (_settings ? _settings : _callShare)->width() + skip
			+ _hangup->width() + skip;
		const auto membersSkip = st::groupCallNarrowSkip;
		const auto membersWidth = st::groupCallNarrowMembersWidth
			+ 2 * membersSkip;
		auto left = (_mode == PanelMode::Default)
			? (widget()->width() - fullWidth) / 2
			: (membersWidth
				+ (widget()->width()
					- membersWidth
					- membersSkip
					- fullWidth) / 2);
		_mute->moveInner({ left + addSkip, buttonsTop + addSkip });
		left += muteSize + skip;
		if (_video) {
			_video->moveToLeft(left, buttonsTop);
			left += _video->width() + skip;
		}
		if (_screenShare) {
			_screenShare->moveToLeft(left, buttonsTop);
			left += _video->width() + skip;
		}
		if (_settings) {
			_settings->setStyle(st::groupCallSettingsSmall);
			_settings->moveToLeft(left, buttonsTop);
			left += _settings->width() + skip;
		}
		if (_callShare) {
			_callShare->setStyle(st::groupCallShareSmall);
			_callShare->moveToLeft(left, buttonsTop);
			left += _callShare->width() + skip;
		}
		_hangup->setStyle(st::groupCallHangupSmall);
		_hangup->moveToLeft(left, buttonsTop);
		left += _hangup->width();
		if (_controlsBackground) {
			const auto rect = QRect(
				left - fullWidth,
				buttonsTop,
				fullWidth,
				_hangup->height());
			_controlsBackground->setGeometry(
				rect.marginsAdded(st::groupCallControlsBackMargin));
		}
	} else {
		_mute->setStyle(st::callMuteButton);
		toggle(true);

		const auto muteTop = widget()->height()
			- st::groupCallMuteBottomSkip;
		const auto buttonsTop = widget()->height()
			- st::groupCallButtonBottomSkip;
		const auto muteSize = _mute->innerSize().width();
		const auto fullWidth = muteSize
			+ 2 * (_settings ? _settings : _callShare)->width()
			+ 2 * st::groupCallButtonSkip;
		_mute->moveInner({ (widget()->width() - muteSize) / 2, muteTop });
		const auto leftButtonLeft = (widget()->width() - fullWidth) / 2;
		if (_settings) {
			_settings->setStyle(st::groupCallSettings);
			_settings->moveToLeft(leftButtonLeft, buttonsTop);
		}
		if (_callShare) {
			_callShare->setStyle(st::groupCallShare);
			_callShare->moveToLeft(leftButtonLeft, buttonsTop);
		}
		_hangup->setStyle(st::groupCallHangup);
		_hangup->moveToRight(leftButtonLeft, buttonsTop);
	}
}

void Panel::updateMembersGeometry() {
	if (!_members) {
		return;
	}
	const auto desiredHeight = _members->desiredHeight();
	if (_mode == PanelMode::Wide) {
		const auto skip = st::groupCallNarrowSkip;
		const auto membersWidth = st::groupCallNarrowMembersWidth;
		const auto top = st::groupCallWideVideoTop;
		_members->setGeometry(
			skip,
			top,
			membersWidth,
			std::min(desiredHeight, widget()->height()));
		_pinnedVideoWrap->setGeometry(
			membersWidth + 2 * skip,
			top,
			widget()->width() - membersWidth - 3 * skip,
			widget()->height() - top - skip);
	} else {
		const auto membersBottom = _videoMode.current()
			? (widget()->height() - st::groupCallMembersBottomSkipSmall)
			: (widget()->height() - st::groupCallMuteBottomSkip);
		const auto membersTop = st::groupCallMembersTop;
		const auto availableHeight = membersBottom
			- st::groupCallMembersMargin.bottom()
			- membersTop;
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
		_members->setGeometry(
			(widget()->width() - membersWidth) / 2,
			membersTop,
			membersWidth,
			std::min(desiredHeight, availableHeight));
	}
}

void Panel::refreshTitle() {
	if (!_title) {
		auto text = rpl::combine(
			Info::Profile::NameValue(_peer),
			rpl::single(
				QString()
			) | rpl::then(_call->real(
			) | rpl::map([=](not_null<Data::GroupCall*> real) {
				return real->titleValue();
			}) | rpl::flatten_latest())
		) | rpl::map([=](
				const TextWithEntities &name,
				const QString &title) {
			return title.isEmpty() ? name.text : title;
		}) | rpl::after_next([=] {
			refreshTitleGeometry();
		});
		_title.create(
			widget(),
			rpl::duplicate(text),
			st::groupCallTitleLabel);
		_title->show();
		_title->setAttribute(Qt::WA_TransparentForMouseEvents);
	}
	refreshTitleGeometry();
	if (!_subtitle && _mode == PanelMode::Default) {
		_subtitle.create(
			widget(),
			rpl::single(
				_call->scheduleDate()
			) | rpl::then(
				_call->real(
				) | rpl::map([=](not_null<Data::GroupCall*> real) {
					return real->scheduleDateValue();
				}) | rpl::flatten_latest()
			) | rpl::map([=](TimeId scheduleDate) {
				if (scheduleDate) {
					return tr::lng_group_call_scheduled_status();
				} else if (!_members) {
					setupMembers();
				}
				return tr::lng_group_call_members(
					lt_count_decimal,
					_members->fullCountValue() | rpl::map([](int value) {
						return (value > 0) ? float64(value) : 1.;
					}));
			}) | rpl::flatten_latest(),
			st::groupCallSubtitleLabel);
		_subtitle->show();
		_subtitle->setAttribute(Qt::WA_TransparentForMouseEvents);
	}
	if (_subtitle) {
		const auto middle = _title
			? (_title->x() + _title->width() / 2)
			: (widget()->width() / 2);
		const auto top = _title
			? st::groupCallSubtitleTop
			: st::groupCallTitleTop;
		_subtitle->moveToLeft(
			(widget()->width() - _subtitle->width()) / 2,
			top);
	}
}

void Panel::refreshTitleGeometry() {
	if (!_title) {
		return;
	}
	const auto fullRect = computeTitleRect();
	const auto recordingWidth = 2 * st::groupCallRecordingMarkSkip
		+ st::groupCallRecordingMark;
	const auto titleRect = _recordingMark
		? QRect(
			fullRect.x(),
			fullRect.y(),
			fullRect.width() - _recordingMark->width(),
			fullRect.height())
		: fullRect;
	const auto best = _title->naturalWidth();
	const auto from = (widget()->width() - best) / 2;
	const auto top = (_mode == PanelMode::Default)
		? st::groupCallTitleTop
		: (st::groupCallWideVideoTop
			- st::groupCallTitleLabel.style.font->height) / 2;
	const auto left = titleRect.x();
	if (from >= left && from + best <= left + titleRect.width()) {
		_title->resizeToWidth(best);
		_title->moveToLeft(from, top);
	} else if (titleRect.width() < best) {
		_title->resizeToWidth(titleRect.width());
		_title->moveToLeft(left, top);
	} else if (from < left) {
		_title->resizeToWidth(best);
		_title->moveToLeft(left, top);
	} else {
		_title->resizeToWidth(best);
		_title->moveToLeft(left + titleRect.width() - best, top);
	}
	if (_recordingMark) {
		const auto markTop = top + st::groupCallRecordingMarkTop;
		_recordingMark->move(
			_title->x() + _title->width(),
			markTop - st::groupCallRecordingMarkSkip);
	}
}

void Panel::paint(QRect clip) {
	Painter p(widget());

	auto region = QRegion(clip);
	for (const auto rect : region) {
		p.fillRect(rect, st::groupCallBg);
	}
}

bool Panel::handleClose() {
	if (_call) {
		_window->hide();
		return true;
	}
	return false;
}

not_null<Ui::RpWidget*> Panel::widget() const {
	return _window->body();
}

} // namespace Calls::Group
