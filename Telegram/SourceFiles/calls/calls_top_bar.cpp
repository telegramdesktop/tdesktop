/*
This file is part of Telegram Desktop,
the official desktop application for the Telegram messaging service.

For license and copyright information please follow this link:
https://github.com/telegramdesktop/tdesktop/blob/master/LEGAL
*/
#include "calls/calls_top_bar.h"

#include "ui/widgets/buttons.h"
#include "ui/widgets/labels.h"
#include "ui/chat/group_call_bar.h" // Ui::GroupCallBarContent.
#include "ui/layers/generic_box.h"
#include "ui/wrap/padding_wrap.h"
#include "ui/text/format_values.h"
#include "lang/lang_keys.h"
#include "core/application.h"
#include "calls/calls_call.h"
#include "calls/calls_instance.h"
#include "calls/calls_signal_bars.h"
#include "calls/calls_group_panel.h" // LeaveGroupCallBox.
#include "history/view/history_view_group_call_tracker.h" // ContentByCall.
#include "data/data_user.h"
#include "data/data_group_call.h"
#include "data/data_channel.h"
#include "data/data_changes.h"
#include "main/main_session.h"
#include "boxes/abstract_box.h"
#include "base/timer.h"
#include "app.h"
#include "styles/style_calls.h"
#include "styles/style_layers.h"

namespace Calls {
namespace {

constexpr auto kUpdateDebugTimeoutMs = crl::time(500);

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
	setTitle(rpl::single(qsl("Call Debug")));

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

TopBar::TopBar(
	QWidget *parent,
	const base::weak_ptr<Call> &call)
: TopBar(parent, call, nullptr) {
}

TopBar::TopBar(
	QWidget *parent,
	const base::weak_ptr<GroupCall> &call)
: TopBar(parent, nullptr, call) {
}

TopBar::TopBar(
	QWidget *parent,
	const base::weak_ptr<Call> &call,
	const base::weak_ptr<GroupCall> &groupCall)
: RpWidget(parent)
, _call(call)
, _groupCall(groupCall)
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
		tr::lng_call_bar_hangup(tr::now).toUpper())
	: object_ptr<Ui::LabelSimple>(nullptr))
, _mute(this, st::callBarMuteToggle)
, _info(this)
, _hangup(this, st::callBarHangup) {
	initControls();
	resize(width(), st::callBarHeight);
}

void TopBar::initControls() {
	_mute->setClickedCallback([=] {
		if (const auto call = _call.get()) {
			call->setMuted(!call->muted());
		} else if (const auto group = _groupCall.get()) {
			if (group->muted() != MuteState::ForceMuted) {
				group->setMuted((group->muted() == MuteState::Active)
					? MuteState::Muted
					: MuteState::Active);
			}
		}
	});

	using namespace rpl::mappers;
	auto muted = _call
		? _call->mutedValue()
		: (_groupCall->mutedValue() | rpl::map(_1 != MuteState::Active));
	std::move(
		muted
	) | rpl::start_with_next([=](bool muted) {
		setMuted(muted);
		update();
	}, lifetime());

	if (const auto group = _groupCall.get()) {
		group->mutedValue(
		) | rpl::start_with_next([=](MuteState state) {
			if (state == MuteState::ForceMuted) {
				_mute->clearState();
			}
			_mute->setAttribute(
				Qt::WA_TransparentForMouseEvents,
				(state == MuteState::ForceMuted));
		}, _mute->lifetime());

		subscribeToMembersChanges(group);
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
				Ui::show(Box<DebugInfoBox>(_call));
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
			Ui::show(Box(
				LeaveGroupCallBox,
				group,
				false,
				BoxContext::MainWindow));
		}
	});
	_updateDurationTimer.setCallback([this] { updateDurationText(); });
	updateDurationText();
}

void TopBar::subscribeToMembersChanges(not_null<GroupCall*> call) {
	const auto channel = call->channel();
	channel->session().changes().peerFlagsValue(
		channel,
		Data::PeerUpdate::Flag::GroupCall
	) | rpl::map([=] {
		return channel->call();
	}) | rpl::filter([=](Data::GroupCall *real) {
		const auto call = _groupCall.get();
		return call && real && (real->id() == call->id());
	}) | rpl::take(
		1
	) | rpl::map([=](not_null<Data::GroupCall*> real) {
		return HistoryView::GroupCallTracker::ContentByCall(
			real,
			HistoryView::UserpicsInRowStyle{
				.size = st::groupCallTopBarUserpicSize,
				.shift = st::groupCallTopBarUserpicShift,
				.stroke = st::groupCallTopBarUserpicStroke,
			});
	}) | rpl::flatten_latest(
	) | rpl::start_with_next([=](const Ui::GroupCallBarContent &content) {
		_userpics = content.userpics;
		update();
	}, lifetime());
}

void TopBar::updateInfoLabels() {
	setInfoLabels();
	updateControlsGeometry();
}

void TopBar::setInfoLabels() {
	if (const auto call = _call.get()) {
		const auto user = call->user();
		const auto fullName = user->name;
		const auto shortName = user->firstName;
		_fullInfoLabel->setText(fullName.toUpper());
		_shortInfoLabel->setText(shortName.toUpper());
	} else if (const auto group = _groupCall.get()) {
		const auto channel = group->channel();
		const auto name = channel->name;
		_fullInfoLabel->setText(name.toUpper());
		_shortInfoLabel->setText(name.toUpper());
	}
}

void TopBar::setMuted(bool mute) {
	_mute->setIconOverride(mute ? &st::callBarUnmuteIcon : nullptr);
	_mute->setRippleColorOverride(mute ? &st::callBarUnmuteRipple : nullptr);
	_hangup->setRippleColorOverride(mute ? &st::callBarUnmuteRipple : nullptr);
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

	auto fullWidth = _fullInfoLabel->naturalWidth();
	auto showFull = (left + fullWidth + right <= width());
	_fullInfoLabel->setVisible(showFull);
	_shortInfoLabel->setVisible(!showFull);

	auto setInfoLabelGeometry = [this, left, right](auto &&infoLabel) {
		auto minPadding = qMax(left, right);
		auto infoWidth = infoLabel->naturalWidth();
		auto infoLeft = (width() - infoWidth) / 2;
		if (infoLeft < minPadding) {
			infoLeft = left;
			infoWidth = width() - left - right;
		}
		infoLabel->setGeometryToLeft(infoLeft, st::callBarLabelTop, infoWidth, st::callBarInfoLabel.style.font->height);
	};
	setInfoLabelGeometry(_fullInfoLabel);
	setInfoLabelGeometry(_shortInfoLabel);
}

void TopBar::paintEvent(QPaintEvent *e) {
	Painter p(this);
	p.fillRect(e->rect(), _muted ? st::callBarBgMuted : st::callBarBg);
	if (!_userpics.isNull()) {
		const auto imageSize = _userpics.size()
			/ _userpics.devicePixelRatio();
		const auto top = (height() - imageSize.height()) / 2;
		p.drawImage(_mute->width(), top, _userpics);
	}
}

TopBar::~TopBar() = default;

} // namespace Calls
