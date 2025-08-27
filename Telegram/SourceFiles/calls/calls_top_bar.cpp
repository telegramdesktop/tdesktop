/*
This file is part of Telegram Desktop,
the official desktop application for the Telegram messaging service.

For license and copyright information please follow this link:
https://github.com/telegramdesktop/tdesktop/blob/master/LEGAL
*/
#include "calls/calls_top_bar.h"

#include "ui/effects/cross_line.h"
#include "ui/paint/blobs_linear.h"
#include "ui/widgets/buttons.h"
#include "ui/widgets/labels.h"
#include "ui/chat/group_call_userpics.h" // Ui::GroupCallUser.
#include "ui/chat/group_call_bar.h" // Ui::GroupCallBarContent.
#include "ui/layers/generic_box.h"
#include "ui/wrap/padding_wrap.h"
#include "ui/text/format_values.h"
#include "ui/toast/toast.h"
#include "ui/power_saving.h"
#include "lang/lang_keys.h"
#include "core/application.h"
#include "calls/calls_call.h"
#include "calls/calls_instance.h"
#include "calls/calls_signal_bars.h"
#include "calls/group/calls_group_call.h"
#include "calls/group/calls_group_menu.h" // Group::LeaveBox.
#include "history/view/history_view_group_call_bar.h" // ContentByCall.
#include "data/data_user.h"
#include "data/data_group_call.h"
#include "data/data_peer.h"
#include "data/data_changes.h"
#include "data/data_session.h"
#include "main/main_session.h"
#include "boxes/abstract_box.h"
#include "base/timer.h"
#include "styles/style_basic.h"
#include "styles/style_calls.h"
#include "styles/style_chat_helpers.h" // style::GroupCallUserpics
#include "styles/style_layers.h"

namespace Calls {

enum class BarState {
	Connecting,
	Active,
	Muted,
	ForceMuted,
};

namespace {

constexpr auto kUpdateDebugTimeoutMs = crl::time(500);

constexpr auto kMinorBlobAlpha = 76. / 255.;

constexpr auto kHideBlobsDuration = crl::time(500);
constexpr auto kBlobLevelDuration = crl::time(250);
constexpr auto kBlobUpdateInterval = crl::time(100);

auto BarStateFromMuteState(
		MuteState state,
		GroupCall::InstanceState instanceState,
		TimeId scheduledDate) {
	return scheduledDate
		? BarState::ForceMuted
		: (instanceState == GroupCall::InstanceState::Disconnected)
		? BarState::Connecting
		: (state == MuteState::ForceMuted || state == MuteState::RaisedHand)
		? BarState::ForceMuted
		: (state == MuteState::Muted)
		? BarState::Muted
		: BarState::Active;
};

auto LinearBlobs() {
	return std::vector<Ui::Paint::LinearBlobs::BlobData>{
		{
			.segmentsCount = 5,
			.minRadius = 0.,
			.maxRadius = (float)st::groupCallMajorBlobMaxRadius,
			.idleRadius = (float)st::groupCallMinorBlobIdleRadius,
			.speedScale = .3,
			.alpha = 1.,
		},
		{
			.segmentsCount = 7,
			.minRadius = 0.,
			.maxRadius = (float)st::groupCallMinorBlobMaxRadius,
			.idleRadius = (float)st::groupCallMinorBlobIdleRadius,
			.speedScale = .7,
			.alpha = kMinorBlobAlpha,
		},
		{
			.segmentsCount = 8,
			.minRadius = 0.,
			.maxRadius = (float)st::groupCallMinorBlobMaxRadius,
			.idleRadius = (float)st::groupCallMinorBlobIdleRadius,
			.speedScale = .7,
			.alpha = kMinorBlobAlpha,
		},
	};
}

auto Colors() {
	using Vector = std::vector<QColor>;
	using Colors = anim::gradient_colors;
	return base::flat_map<BarState, Colors>{
		{
			BarState::ForceMuted,
			Colors(QGradientStops{
				{ 0.0, st::groupCallForceMutedBar1->c },
				{ .35, st::groupCallForceMutedBar2->c },
				{ 1.0, st::groupCallForceMutedBar3->c } })
		},
		{
			BarState::Active,
			Colors(Vector{ st::groupCallLive1->c, st::groupCallLive2->c })
		},
		{
			BarState::Muted,
			Colors(Vector{ st::groupCallMuted1->c, st::groupCallMuted2->c })
		},
		{
			BarState::Connecting,
			Colors(st::callBarBgMuted->c)
		},
	};
}

class DebugInfoBox : public Ui::BoxContent {
public:
	DebugInfoBox(QWidget*, base::weak_ptr<Call> call);

protected:
	void prepare() override;

private:
	void updateText();

	base::weak_ptr<Call> _call;
	QPointer<Ui::FlatLabel> _text;
	base::Timer _updateTextTimer;

};

DebugInfoBox::DebugInfoBox(QWidget*, base::weak_ptr<Call> call)
: _call(call) {
}

void DebugInfoBox::prepare() {
	setTitle(rpl::single(u"Call Debug"_q));

	addButton(tr::lng_close(), [this] { closeBox(); });
	_text = setInnerWidget(
		object_ptr<Ui::PaddingWrap<Ui::FlatLabel>>(
			this,
			object_ptr<Ui::FlatLabel>(this, st::callDebugLabel),
			st::callDebugPadding))->entity();
	_text->setSelectable(true);
	updateText();
	_updateTextTimer.setCallback([this] { updateText(); });
	_updateTextTimer.callEach(kUpdateDebugTimeoutMs);
	setDimensions(st::boxWideWidth, st::boxMaxListHeight);
}

void DebugInfoBox::updateText() {
	if (auto call = _call.get()) {
		_text->setText(call->getDebugLog());
	}
}

} // namespace

struct TopBar::User {
	Ui::GroupCallUser data;
};

class Mute final : public Ui::IconButton {
public:
	Mute(QWidget *parent, const style::IconButton &st)
	: Ui::IconButton(parent, st)
	, _st(st)
	, _crossLineMuteAnimation(st::callTopBarMuteCrossLine) {
		resize(_st.width, _st.height);
		installEventFilter(this);

		style::PaletteChanged(
		) | rpl::start_with_next([=] {
			_crossLineMuteAnimation.invalidate();
		}, lifetime());
	}

	void setProgress(float64 progress) {
		if (_progress == progress) {
			return;
		}
		_progress = progress;
		update();
	}

	void setRippleColorOverride(const style::color *colorOverride) {
		_rippleColorOverride = colorOverride;
	}

protected:
	bool eventFilter(QObject *object, QEvent *event) {
		if (event->type() == QEvent::Paint) {
			auto p = QPainter(this);
			paintRipple(
				p,
				_st.rippleAreaPosition.x(),
				_st.rippleAreaPosition.y(),
				_rippleColorOverride ? &(*_rippleColorOverride)->c : nullptr);
			_crossLineMuteAnimation.paint(p, _st.iconPosition, _progress);
			return true;
		}
		return QObject::eventFilter(object, event);
	}

private:
	float64 _progress = 0.;

	const style::IconButton &_st;
	Ui::CrossLineAnimation _crossLineMuteAnimation;
	const style::color *_rippleColorOverride = nullptr;

};

TopBar::TopBar(
	QWidget *parent,
	Call *call,
	std::shared_ptr<Ui::Show> show)
: TopBar(parent, show, call, nullptr) {
}

TopBar::TopBar(
	QWidget *parent,
	GroupCall *call,
	std::shared_ptr<Ui::Show> show)
: TopBar(parent, show, nullptr, call) {
}

TopBar::TopBar(
	QWidget *parent,
	std::shared_ptr<Ui::Show> show,
	Call *call,
	GroupCall *groupCall)
: RpWidget(parent)
, _call(call)
, _groupCall(groupCall)
, _show(show)
, _userpics(call
	? nullptr
	: std::make_unique<Ui::GroupCallUserpics>(
		st::groupCallTopBarUserpics,
		rpl::single(true),
		[=] { updateUserpics(); }))
, _durationLabel(_call
	? object_ptr<Ui::LabelSimple>(this, st::callBarLabel)
	: object_ptr<Ui::LabelSimple>(nullptr))
, _signalBars(_call
	? object_ptr<SignalBars>(this, _call.get(), st::callBarSignalBars)
	: object_ptr<SignalBars>(nullptr))
, _fullInfoLabel(this, st::callBarInfoLabel)
, _shortInfoLabel(this, st::callBarInfoLabel)
, _hangupLabel(_call
	? object_ptr<Ui::LabelSimple>(
		this,
		st::callBarLabel,
		tr::lng_call_bar_hangup(tr::now))
	: object_ptr<Ui::LabelSimple>(nullptr))
, _mute(this, st::callBarMuteToggle)
, _info(this)
, _hangup(this, st::callBarHangup)
, _gradients(Colors(), QPointF(), QPointF())
, _updateDurationTimer([=] { updateDurationText(); }) {
	initControls();
	resize(width(), st::callBarHeight);
	setupInitialBrush();
}

void TopBar::setupInitialBrush() {
	Expects(_switchStateCallback != nullptr);

	_switchStateAnimation.stop();
	_switchStateCallback(1.);
}

void TopBar::initControls() {
	_mute->setClickedCallback([=] {
		if (const auto call = _call.get()) {
			call->setMuted(!call->muted());
		} else if (const auto group = _groupCall.get()) {
			if (group->mutedByAdmin()) {
				_show->showToast(
					tr::lng_group_call_force_muted_sub(tr::now));
			} else {
				group->setMuted((group->muted() == MuteState::Muted)
					? MuteState::Active
					: MuteState::Muted);
			}
		}
	});

	const auto mapToState = [](bool muted) {
		return muted ? MuteState::Muted : MuteState::Active;
	};
	const auto fromState = _mute->lifetime().make_state<BarState>(
		BarStateFromMuteState(
			_call
				? mapToState(_call->muted())
				: _groupCall->muted(),
			GroupCall::InstanceState::Connected,
			_call ? TimeId(0) : _groupCall->scheduleDate()));
	using namespace rpl::mappers;
	auto muted = _call
		? rpl::combine(
			_call->mutedValue() | rpl::map(mapToState),
			rpl::single(GroupCall::InstanceState::Connected),
			rpl::single(TimeId(0))
		) | rpl::type_erased()
		: rpl::combine(
			(_groupCall->mutedValue()
				| MapPushToTalkToActive()
				| rpl::distinct_until_changed()
				| rpl::type_erased()),
			rpl::single(
				_groupCall->instanceState()
			) | rpl::then(_groupCall->instanceStateValue() | rpl::filter(
				_1 != GroupCall::InstanceState::TransitionToRtc)),
			rpl::single(
				_groupCall->scheduleDate()
			) | rpl::then(_groupCall->real(
			) | rpl::map([](not_null<Data::GroupCall*> call) {
				return call->scheduleDateValue();
			}) | rpl::flatten_latest()));
	std::move(
		muted
	) | rpl::map(
		BarStateFromMuteState
	) | rpl::start_with_next([=](BarState state) {
		_isGroupConnecting = (state == BarState::Connecting);
		setMuted(state != BarState::Active);
		update();

		const auto isForceMuted = (state == BarState::ForceMuted);
		if (isForceMuted) {
			_mute->clearState();
		}
		_mute->setPointerCursor(!isForceMuted);

		const auto to = 1.;
		const auto from = _switchStateAnimation.animating()
			? (to - _switchStateAnimation.value(0.))
			: 0.;
		const auto fromMuted = *fromState;
		const auto toMuted = state;
		*fromState = state;

		const auto crossFrom = (fromMuted != BarState::Active) ? 1. : 0.;
		const auto crossTo = (toMuted != BarState::Active) ? 1. : 0.;

		_switchStateCallback = [=](float64 value) {
			if (_groupCall) {
				_groupBrush = QBrush(
					_gradients.gradient(fromMuted, toMuted, value));
				update();
			}

			const auto crossProgress = (crossFrom == crossTo)
				? crossTo
				: anim::interpolateToF(crossFrom, crossTo, value);
			_mute->setProgress(crossProgress);
		};

		_switchStateAnimation.stop();
		const auto duration = (to - from) * st::universalDuration;
		_switchStateAnimation.start(
			_switchStateCallback,
			from,
			to,
			duration);
	}, _mute->lifetime());

	if (const auto group = _groupCall.get()) {
		subscribeToMembersChanges(group);

		_isGroupConnecting.value(
		) | rpl::start_with_next([=](bool isConnecting) {
			_mute->setAttribute(
				Qt::WA_TransparentForMouseEvents,
				isConnecting);
			updateInfoLabels();
		}, lifetime());
	}

	if (const auto call = _call.get()) {
		call->user()->session().changes().peerUpdates(
			Data::PeerUpdate::Flag::Name
		) | rpl::filter([=](const Data::PeerUpdate &update) {
			// _user may change for the same Panel.
			return (_call != nullptr) && (update.peer == _call->user());
		}) | rpl::start_with_next([=] {
			updateInfoLabels();
		}, lifetime());
	}

	setInfoLabels();
	_info->setClickedCallback([=] {
		if (const auto call = _call.get()) {
			if (Logs::DebugEnabled()
				&& (_info->clickModifiers() & Qt::ControlModifier)) {
				_show->showBox(
					Box<DebugInfoBox>(_call),
					Ui::LayerOption::CloseOther);
			} else {
				Core::App().calls().showInfoPanel(call);
			}
		} else if (const auto group = _groupCall.get()) {
			Core::App().calls().showInfoPanel(group);
		}
	});
	_hangup->setClickedCallback([this] {
		if (const auto call = _call.get()) {
			call->hangup();
		} else if (const auto group = _groupCall.get()) {
			if (!group->canManage()) {
				group->hangup();
			} else {
				_show->showBox(
					Box(
						Group::LeaveBox,
						group,
						false,
						Group::BoxContext::MainWindow),
					Ui::LayerOption::CloseOther);
			}
		}
	});
	updateDurationText();
}

void TopBar::initBlobsUnder(
		QWidget *blobsParent,
		rpl::producer<QRect> barGeometry) {
	const auto group = _groupCall.get();
	if (!group) {
		return;
	}

	struct State {
		Ui::Paint::LinearBlobs paint = {
			LinearBlobs(),
			kBlobLevelDuration,
			1.,
			Ui::Paint::LinearBlob::Direction::TopDown
		};
		Ui::Animations::Simple hideAnimation;
		Ui::Animations::Basic animation;
		base::Timer levelTimer;
		crl::time hideLastTime = 0;
		crl::time lastTime = 0;
		float lastLevel = 0.;
		float levelBeforeLast = 0.;
	};

	_blobs = base::make_unique_q<Ui::RpWidget>(blobsParent);

	const auto state = _blobs->lifetime().make_state<State>();
	state->levelTimer.setCallback([=] {
		state->levelBeforeLast = state->lastLevel;
		state->lastLevel = 0.;
		if (state->levelBeforeLast == 0.) {
			state->paint.setLevel(0.);
			state->levelTimer.cancel();
		}
	});

	state->animation.init([=](crl::time now) {
		if (const auto last = state->hideLastTime; (last > 0)
			&& (now - last >= kHideBlobsDuration)) {
			state->animation.stop();
			return false;
		}
		state->paint.updateLevel(now - state->lastTime);
		state->lastTime = now;

		_blobs->update();
		return true;
	});

	group->stateValue(
	) | rpl::start_with_next([=](Calls::GroupCall::State state) {
		if (state == Calls::GroupCall::State::HangingUp) {
			_blobs->hide();
		}
	}, lifetime());

	using namespace rpl::mappers;
	auto hideBlobs = rpl::combine(
		PowerSaving::OnValue(PowerSaving::kCalls),
		Core::App().appDeactivatedValue(),
		group->instanceStateValue()
	) | rpl::map(_1 || _2 || _3 == GroupCall::InstanceState::Disconnected);

	std::move(
		hideBlobs
	) | rpl::distinct_until_changed(
	) | rpl::start_with_next([=](bool hide) {
		if (hide) {
			state->paint.setLevel(0.);
		}
		state->hideLastTime = hide ? crl::now() : 0;
		if (!hide && !state->animation.animating()) {
			state->animation.start();
		}
		if (hide) {
			state->levelTimer.cancel();
		} else {
			state->lastLevel = 0.;
		}

		const auto from = hide ? 0. : 1.;
		const auto to = hide ? 1. : 0.;
		state->hideAnimation.start([=](float64) {
			_blobs->update();
		}, from, to, kHideBlobsDuration);
	}, lifetime());

	std::move(
		barGeometry
	) | rpl::start_with_next([=](QRect rect) {
		_blobs->resize(
			rect.width(),
			(int)state->paint.maxRadius());
		_blobs->moveToLeft(rect.x(), rect.y() + rect.height());
	}, lifetime());

	shownValue(
	) | rpl::start_with_next([=](bool shown) {
		_blobs->setVisible(shown);
	}, lifetime());

	_blobs->paintRequest(
	) | rpl::start_with_next([=](QRect clip) {
		const auto hidden = state->hideAnimation.value(
			state->hideLastTime ? 1. : 0.);
		if (hidden == 1.) {
			return;
		}

		auto p = QPainter(_blobs);
		if (hidden > 0.) {
			p.setOpacity(1. - hidden);
		}
		const auto top = -_blobs->height() * hidden;
		const auto width = _blobs->width();
		p.translate(0, top);
		state->paint.paint(p, _groupBrush, width);
	}, _blobs->lifetime());

	group->levelUpdates(
	) | rpl::filter([=](const LevelUpdate &update) {
		return !state->hideLastTime && (update.value > state->lastLevel);
	}) | rpl::start_with_next([=](const LevelUpdate &update) {
		if (state->lastLevel == 0.) {
			state->levelTimer.callEach(kBlobUpdateInterval);
		}
		state->lastLevel = update.value;
		state->paint.setLevel(update.value);
	}, _blobs->lifetime());

	_blobs->setAttribute(Qt::WA_TransparentForMouseEvents);
	_blobs->show();

	if (!state->hideLastTime) {
		state->animation.start();
	}
}

void TopBar::subscribeToMembersChanges(not_null<GroupCall*> call) {
	const auto peer = call->peer();
	const auto group = _groupCall.get();
	const auto conference = group && group->conference();
	auto realValue = conference
		? (rpl::single(group->conferenceCall().get()) | rpl::type_erased())
		: peer->session().changes().peerFlagsValue(
			peer,
			Data::PeerUpdate::Flag::GroupCall
		) | rpl::map([=] {
			return peer->groupCall();
		}) | rpl::filter([=](Data::GroupCall *real) {
			const auto call = _groupCall.get();
			return call && real && (real->id() == call->id());
		}) | rpl::take(1);
	std::move(
		realValue
	) | rpl::before_next([=](not_null<Data::GroupCall*> real) {
		real->titleValue() | rpl::start_with_next([=] {
			updateInfoLabels();
		}, lifetime());
	}) | rpl::map([=](not_null<Data::GroupCall*> real) {
		return HistoryView::GroupCallBarContentByCall(
			real,
			st::groupCallTopBarUserpics.size);
	}) | rpl::flatten_latest(
	) | rpl::filter([=](const Ui::GroupCallBarContent &content) {
		if (_users.size() != content.users.size()
			|| (conference && _usersCount != content.count)) {
			return true;
		}
		for (auto i = 0, count = int(_users.size()); i != count; ++i) {
			if (_users[i].userpicKey != content.users[i].userpicKey
				|| _users[i].id != content.users[i].id) {
				return true;
			}
		}
		return false;
	}) | rpl::start_with_next([=](const Ui::GroupCallBarContent &content) {
		_users = content.users;
		_usersCount = content.count;
		for (auto &user : _users) {
			user.speaking = false;
		}
		_userpics->update(_users, !isHidden());
		if (conference) {
			updateInfoLabels();
		}
	}, lifetime());

	_userpics->widthValue(
	) | rpl::start_with_next([=](int width) {
		_userpicsWidth = width;
		updateControlsGeometry();
	}, lifetime());

	call->peer()->session().changes().peerUpdates(
		Data::PeerUpdate::Flag::Name
	) | rpl::filter([=](const Data::PeerUpdate &update) {
		// _peer may change for the same Panel.
		const auto call = _groupCall.get();
		return (call != nullptr) && (update.peer == call->peer());
	}) | rpl::start_with_next([=] {
		updateInfoLabels();
	}, lifetime());
}

void TopBar::updateUserpics() {
	update(_mute->width(), 0, _userpics->maxWidth(), height());
}

void TopBar::updateInfoLabels() {
	setInfoLabels();
	updateControlsGeometry();
}

void TopBar::setInfoLabels() {
	if (const auto call = _call.get()) {
		const auto user = call->user();
		const auto fullName = user->name();
		const auto shortName = user->firstName;
		_fullInfoLabel->setText(fullName);
		_shortInfoLabel->setText(shortName);
	} else if (const auto group = _groupCall.get()) {
		const auto peer = group->peer();
		const auto real = peer->groupCall();
		const auto connecting = _isGroupConnecting.current();
		if (!group->conference()) {
			_shortInfoLabel.destroy();
		}
		if (!group->conference() || connecting) {
			const auto name = peer->name();
			const auto title = (real && real->id() == group->id())
				? real->title()
				: QString();
			const auto text = _isGroupConnecting.current()
				? tr::lng_group_call_connecting(tr::now)
				: !title.isEmpty()
				? title
				: name;
			_fullInfoLabel->setText(text);
			if (_shortInfoLabel) {
				_shortInfoLabel->setText(text);
			}
		} else if (!_usersCount
			|| _users.empty()
			|| (_users.size() == 1
				&& _users.front().id == peer->session().userPeerId().value
				&& _usersCount == 1)) {
			_fullInfoLabel->setText(tr::lng_confcall_join_title(tr::now));
			_shortInfoLabel->setText(tr::lng_confcall_join_title(tr::now));
		} else {
			const auto textWithUserpics = [&](int userpics) {
				const auto other = std::max(_usersCount - userpics, 0);
				auto names = QStringList();
				for (const auto &entry : _users) {
					const auto user = peer->owner().peer(PeerId(entry.id));
					names.push_back(user->shortName());
					if (names.size() >= userpics) {
						break;
					}
				}
				if (other > 0) {
					return tr::lng_forwarding_from(
						tr::now,
						lt_count,
						other,
						lt_user,
						names.join(u", "_q));
				} else if (userpics > 1) {
					return tr::lng_forwarding_from_two(
						tr::now,
						lt_user,
						names.mid(0, userpics - 1).join(u", "_q),
						lt_second_user,
						names.back());
				}
				return names.back();
			};
			_fullInfoLabel->setText(textWithUserpics(int(_users.size())));
			_shortInfoLabel->setText(textWithUserpics(1));
		}
	}
}

void TopBar::setMuted(bool mute) {
	_mute->setRippleColorOverride(&st::shadowFg);
	_hangup->setRippleColorOverride(&st::shadowFg);
	_muted = mute;
}

void TopBar::updateDurationText() {
	if (!_call || !_durationLabel) {
		return;
	}
	auto wasWidth = _durationLabel->width();
	auto durationMs = _call->getDurationMs();
	auto durationSeconds = durationMs / 1000;
	startDurationUpdateTimer(durationMs);
	_durationLabel->setText(Ui::FormatDurationText(durationSeconds));
	if (_durationLabel->width() != wasWidth) {
		updateControlsGeometry();
	}
}

void TopBar::startDurationUpdateTimer(crl::time currentDuration) {
	auto msTillNextSecond = 1000 - (currentDuration % 1000);
	_updateDurationTimer.callOnce(msTillNextSecond + 5);
}

void TopBar::resizeEvent(QResizeEvent *e) {
	updateControlsGeometry();
}

void TopBar::updateControlsGeometry() {
	auto left = 0;
	_mute->moveToLeft(left, 0);
	left += _mute->width();
	if (_durationLabel) {
		_durationLabel->moveToLeft(left, st::callBarLabelTop);
		left += _durationLabel->width() + st::callBarSkip;
	}
	if (_userpicsWidth) {
		const auto single = st::groupCallTopBarUserpics.size;
		const auto skip = anim::interpolate(
			0,
			st::callBarSkip,
			std::min(_userpicsWidth, single) / float64(single));
		left += _userpicsWidth + skip;
	}
	if (_signalBars) {
		_signalBars->moveToLeft(left, (height() - _signalBars->height()) / 2);
		left += _signalBars->width() + st::callBarSkip;
	}

	auto right = st::callBarRightSkip;
	if (_hangupLabel) {
		_hangupLabel->moveToRight(right, st::callBarLabelTop);
		right += _hangupLabel->width();
	} else {
		//right -= st::callBarRightSkip;
	}
	right += st::callBarHangup.width;
	_hangup->setGeometryToRight(0, 0, right, height());
	_info->setGeometryToLeft(
		_mute->width(),
		0,
		width() - _mute->width() - _hangup->width(),
		height());

	auto fullWidth = _fullInfoLabel->textMaxWidth();
	auto showFull = !_shortInfoLabel
		|| (left + fullWidth + right <= width());
	auto setInfoLabelGeometry = [this, left, right](auto &&infoLabel) {
		auto minPadding = qMax(left, right);
		auto infoWidth = infoLabel->textMaxWidth();
		auto infoLeft = (width() - infoWidth) / 2;
		if (infoLeft < minPadding) {
			infoLeft = left;
			infoWidth = width() - left - right;
		}
		infoLabel->setGeometryToLeft(infoLeft, st::callBarLabelTop, infoWidth, st::callBarInfoLabel.style.font->height);
	};

	_fullInfoLabel->setVisible(showFull);
	setInfoLabelGeometry(_fullInfoLabel);
	if (_shortInfoLabel) {
		_shortInfoLabel->setVisible(!showFull);
		setInfoLabelGeometry(_shortInfoLabel);
	}

	_gradients.set_points(
		QPointF(0, st::callBarHeight / 2),
		QPointF(width(), st::callBarHeight / 2));
	if (!_switchStateAnimation.animating()) {
		_switchStateCallback(1.);
	}
}

void TopBar::paintEvent(QPaintEvent *e) {
	auto p = QPainter(this);
	auto brush = _groupCall
		? _groupBrush
		: (_muted ? st::callBarBgMuted : st::callBarBg);
	p.fillRect(e->rect(), std::move(brush));

	if (_userpicsWidth) {
		const auto size = st::groupCallTopBarUserpics.size;
		const auto top = (height() - size) / 2;
		_userpics->paint(p, _mute->width(), top, size);
	}
}

TopBar::~TopBar() = default;

} // namespace Calls
