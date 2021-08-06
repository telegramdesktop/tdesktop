/*
This file is part of Telegram Desktop,
the official desktop application for the Telegram messaging service.

For license and copyright information please follow this link:
https://github.com/telegramdesktop/tdesktop/blob/master/LEGAL
*/
#include "calls/group/calls_group_members_row.h"

#include "calls/group/calls_group_call.h"
#include "calls/group/calls_group_common.h"
#include "data/data_peer.h"
#include "data/data_group_call.h"
#include "ui/paint/arcs.h"
#include "ui/paint/blobs.h"
#include "ui/text/text_options.h"
#include "ui/effects/ripple_animation.h"
#include "lang/lang_keys.h"
#include "webrtc/webrtc_video_track.h"
#include "styles/style_calls.h"

namespace Calls::Group {
namespace {

constexpr auto kLevelDuration = 100. + 500. * 0.23;
constexpr auto kBlobScale = 0.605;
constexpr auto kMinorBlobFactor = 0.9f;
constexpr auto kUserpicMinScale = 0.8;
constexpr auto kMaxLevel = 1.;
constexpr auto kWideScale = 5;

constexpr auto kArcsStrokeRatio = 0.8;

const auto kSpeakerThreshold = std::vector<float>{
	Group::kDefaultVolume * 0.1f / Group::kMaxVolume,
	Group::kDefaultVolume * 0.9f / Group::kMaxVolume };

auto RowBlobs() -> std::array<Ui::Paint::Blobs::BlobData, 2> {
	return { {
		{
			.segmentsCount = 6,
			.minScale = kBlobScale * kMinorBlobFactor,
			.minRadius = st::groupCallRowBlobMinRadius * kMinorBlobFactor,
			.maxRadius = st::groupCallRowBlobMaxRadius * kMinorBlobFactor,
			.speedScale = 1.,
			.alpha = .5,
		},
		{
			.segmentsCount = 8,
			.minScale = kBlobScale,
			.minRadius = (float)st::groupCallRowBlobMinRadius,
			.maxRadius = (float)st::groupCallRowBlobMaxRadius,
			.speedScale = 1.,
			.alpha = .2,
		},
	} };
}

[[nodiscard]] QString StatusPercentString(float volume) {
	return QString::number(int(std::round(volume * 200))) + '%';
}

[[nodiscard]] int StatusPercentWidth(const QString &percent) {
	return st::normalFont->width(percent);
}

} // namespace

struct MembersRow::BlobsAnimation {
	BlobsAnimation(
		std::vector<Ui::Paint::Blobs::BlobData> blobDatas,
		float levelDuration,
		float maxLevel);

	Ui::Paint::Blobs blobs;
	crl::time lastTime = 0;
	crl::time lastSoundingUpdateTime = 0;
	float64 enter = 0.;

	QImage userpicCache;
	InMemoryKey userpicKey;

	rpl::lifetime lifetime;
};

struct MembersRow::StatusIcon {
	StatusIcon(bool shown, float volume);

	const style::icon &speaker;
	Ui::Paint::ArcsAnimation arcs;
	Ui::Animations::Simple arcsAnimation;
	Ui::Animations::Simple shownAnimation;
	QString percent;
	int percentWidth = 0;
	int arcsWidth = 0;
	int wasArcsWidth = 0;
	bool shown = true;

	rpl::lifetime lifetime;
};

MembersRow::BlobsAnimation::BlobsAnimation(
	std::vector<Ui::Paint::Blobs::BlobData> blobDatas,
	float levelDuration,
	float maxLevel)
: blobs(std::move(blobDatas), levelDuration, maxLevel) {
	style::PaletteChanged(
	) | rpl::start_with_next([=] {
		userpicCache = QImage();
	}, lifetime);
}

MembersRow::StatusIcon::StatusIcon(bool shown, float volume)
: speaker(st::groupCallStatusSpeakerIcon)
, arcs(
	st::groupCallStatusSpeakerArcsAnimation,
	kSpeakerThreshold,
	volume,
	Ui::Paint::ArcsAnimation::Direction::Right)
, percent(StatusPercentString(volume))
, percentWidth(StatusPercentWidth(percent))
, shown(shown) {
}

MembersRow::MembersRow(
	not_null<MembersRowDelegate*> delegate,
	not_null<PeerData*> participantPeer)
: PeerListRow(participantPeer)
, _delegate(delegate)
, _sounding(false)
, _speaking(false)
, _raisedHandStatus(false)
, _skipLevelUpdate(false)
, _mutedByMe(false) {
	refreshStatus();
	_aboutText = participantPeer->about();
}

MembersRow::~MembersRow() = default;

void MembersRow::setSkipLevelUpdate(bool value) {
	_skipLevelUpdate = value;
}

void MembersRow::updateState(
		const Data::GroupCallParticipant *participant) {
	setVolume(participant
		? participant->volume
		: Group::kDefaultVolume);
	if (!participant) {
		setState(State::Invited);
		setSounding(false);
		setSpeaking(false);
		_mutedByMe = false;
		_raisedHandRating = 0;
	} else if (!participant->muted
		|| (participant->sounding && participant->ssrc != 0)
		|| (participant->additionalSounding
			&& GetAdditionalAudioSsrc(participant->videoParams) != 0)) {
		setState(State::Active);
		setSounding((participant->sounding && participant->ssrc != 0)
			|| (participant->additionalSounding
				&& GetAdditionalAudioSsrc(participant->videoParams) != 0));
		setSpeaking((participant->speaking && participant->ssrc != 0)
			|| (participant->additionalSpeaking
				&& GetAdditionalAudioSsrc(participant->videoParams) != 0));
		_mutedByMe = participant->mutedByMe;
		_raisedHandRating = 0;
	} else if (participant->canSelfUnmute) {
		setState(State::Inactive);
		setSounding(false);
		setSpeaking(false);
		_mutedByMe = participant->mutedByMe;
		_raisedHandRating = 0;
	} else {
		setSounding(false);
		setSpeaking(false);
		_mutedByMe = participant->mutedByMe;
		_raisedHandRating = participant->raisedHandRating;
		setState(_raisedHandRating ? State::RaisedHand : State::Muted);
	}
	refreshStatus();
}

void MembersRow::setSpeaking(bool speaking) {
	if (_speaking == speaking) {
		return;
	}
	_speaking = speaking;
	_speakingAnimation.start(
		[=] { _delegate->rowUpdateRow(this); },
		_speaking ? 0. : 1.,
		_speaking ? 1. : 0.,
		st::widgetFadeDuration);

	if (!_speaking
		|| _mutedByMe
		|| (_state == State::Muted)
		|| (_state == State::RaisedHand)) {
		if (_statusIcon) {
			_statusIcon = nullptr;
			_delegate->rowUpdateRow(this);
		}
	} else if (!_statusIcon) {
		_statusIcon = std::make_unique<StatusIcon>(
			(_volume != Group::kDefaultVolume),
			(float)_volume / Group::kMaxVolume);
		_statusIcon->arcs.setStrokeRatio(kArcsStrokeRatio);
		_statusIcon->arcsWidth = _statusIcon->arcs.finishedWidth();
		_statusIcon->arcs.startUpdateRequests(
		) | rpl::start_with_next([=] {
			if (!_statusIcon->arcsAnimation.animating()) {
				_statusIcon->wasArcsWidth = _statusIcon->arcsWidth;
			}
			auto callback = [=](float64 value) {
				_statusIcon->arcs.update(crl::now());
				_statusIcon->arcsWidth = anim::interpolate(
					_statusIcon->wasArcsWidth,
					_statusIcon->arcs.finishedWidth(),
					value);
				_delegate->rowUpdateRow(this);
			};
			_statusIcon->arcsAnimation.start(
				std::move(callback),
				0.,
				1.,
				st::groupCallSpeakerArcsAnimation.duration);
		}, _statusIcon->lifetime);
	}
}

void MembersRow::setSounding(bool sounding) {
	if (_sounding == sounding) {
		return;
	}
	_sounding = sounding;
	if (!_sounding) {
		_blobsAnimation = nullptr;
	} else if (!_blobsAnimation) {
		_blobsAnimation = std::make_unique<BlobsAnimation>(
			RowBlobs() | ranges::to_vector,
			kLevelDuration,
			kMaxLevel);
		_blobsAnimation->lastTime = crl::now();
		updateLevel(GroupCall::kSpeakLevelThreshold);
	}
}

void MembersRow::clearRaisedHandStatus() {
	if (!_raisedHandStatus) {
		return;
	}
	_raisedHandStatus = false;
	refreshStatus();
	_delegate->rowUpdateRow(this);
}

void MembersRow::setState(State state) {
	if (_state == state) {
		return;
	}
	const auto wasActive = (_state == State::Active);
	const auto wasMuted = (_state == State::Muted)
		|| (_state == State::RaisedHand);
	const auto wasRaisedHand = (_state == State::RaisedHand);
	_state = state;
	const auto nowActive = (_state == State::Active);
	const auto nowMuted = (_state == State::Muted)
		|| (_state == State::RaisedHand);
	const auto nowRaisedHand = (_state == State::RaisedHand);
	if (!wasRaisedHand && nowRaisedHand) {
		_raisedHandStatus = true;
		_delegate->rowScheduleRaisedHandStatusRemove(this);
	}
	if (nowActive != wasActive) {
		_activeAnimation.start(
			[=] { _delegate->rowUpdateRow(this); },
			nowActive ? 0. : 1.,
			nowActive ? 1. : 0.,
			st::widgetFadeDuration);
	}
	if (nowMuted != wasMuted) {
		_mutedAnimation.start(
			[=] { _delegate->rowUpdateRow(this); },
			nowMuted ? 0. : 1.,
			nowMuted ? 1. : 0.,
			st::widgetFadeDuration);
	}
}

void MembersRow::setVolume(int volume) {
	_volume = volume;
	if (_statusIcon) {
		const auto floatVolume = (float)volume / Group::kMaxVolume;
		_statusIcon->arcs.setValue(floatVolume);
		_statusIcon->percent = StatusPercentString(floatVolume);
		_statusIcon->percentWidth = StatusPercentWidth(_statusIcon->percent);

		const auto shown = (volume != Group::kDefaultVolume);
		if (_statusIcon->shown != shown) {
			_statusIcon->shown = shown;
			_statusIcon->shownAnimation.start(
				[=] { _delegate->rowUpdateRow(this); },
				shown ? 0. : 1.,
				shown ? 1. : 0.,
				st::groupCallSpeakerArcsAnimation.duration);
		}
	}
}

void MembersRow::updateLevel(float level) {
	Expects(_blobsAnimation != nullptr);

	const auto spoke = (level >= GroupCall::kSpeakLevelThreshold)
		? crl::now()
		: crl::time();
	if (spoke && _speaking) {
		_speakingLastTime = spoke;
	}

	if (_skipLevelUpdate) {
		return;
	}

	if (spoke) {
		_blobsAnimation->lastSoundingUpdateTime = spoke;
	}
	_blobsAnimation->blobs.setLevel(level);
}

void MembersRow::updateBlobAnimation(crl::time now) {
	Expects(_blobsAnimation != nullptr);

	const auto soundingFinishesAt = _blobsAnimation->lastSoundingUpdateTime
		+ Data::GroupCall::kSoundStatusKeptFor;
	const auto soundingStartsFinishing = soundingFinishesAt
		- kBlobsEnterDuration;
	const auto soundingFinishes = (soundingStartsFinishing < now);
	if (soundingFinishes) {
		_blobsAnimation->enter = std::clamp(
			(soundingFinishesAt - now) / float64(kBlobsEnterDuration),
			0.,
			1.);
	} else if (_blobsAnimation->enter < 1.) {
		_blobsAnimation->enter = std::clamp(
			(_blobsAnimation->enter
				+ ((now - _blobsAnimation->lastTime)
					/ float64(kBlobsEnterDuration))),
			0.,
			1.);
	}
	_blobsAnimation->blobs.updateLevel(now - _blobsAnimation->lastTime);
	_blobsAnimation->lastTime = now;
}

void MembersRow::ensureUserpicCache(
		std::shared_ptr<Data::CloudImageView> &view,
		int size) {
	Expects(_blobsAnimation != nullptr);

	const auto user = peer();
	const auto key = user->userpicUniqueKey(view);
	const auto full = QSize(size, size) * kWideScale * cIntRetinaFactor();
	auto &cache = _blobsAnimation->userpicCache;
	if (cache.isNull()) {
		cache = QImage(full, QImage::Format_ARGB32_Premultiplied);
		cache.setDevicePixelRatio(cRetinaFactor());
	} else if (_blobsAnimation->userpicKey == key
		&& cache.size() == full) {
		return;
	}
	_blobsAnimation->userpicKey = key;
	cache.fill(Qt::transparent);
	{
		Painter p(&cache);
		const auto skip = (kWideScale - 1) / 2 * size;
		user->paintUserpicLeft(p, view, skip, skip, kWideScale * size, size);
	}
}

void MembersRow::paintBlobs(
		Painter &p,
		int x,
		int y,
		int sizew,
		int sizeh,
		PanelMode mode) {
	if (!_blobsAnimation) {
		return;
	}
	auto size = sizew;
	const auto shift = QPointF(x + size / 2., y + size / 2.);
	auto hq = PainterHighQualityEnabler(p);
	p.translate(shift);
	const auto brush = _mutedByMe
		? st::groupCallMemberMutedIcon->b
		: anim::brush(
			st::groupCallMemberInactiveStatus,
			st::groupCallMemberActiveStatus,
			_speakingAnimation.value(_speaking ? 1. : 0.));
	_blobsAnimation->blobs.paint(p, brush);
	p.translate(-shift);
	p.setOpacity(1.);
}

void MembersRow::paintScaledUserpic(
		Painter &p,
		std::shared_ptr<Data::CloudImageView> &userpic,
		int x,
		int y,
		int outerWidth,
		int sizew,
		int sizeh,
		PanelMode mode) {
	auto size = sizew;
	if (!_blobsAnimation) {
		peer()->paintUserpicLeft(p, userpic, x, y, outerWidth, size);
		return;
	}
	const auto enter = _blobsAnimation->enter;
	const auto &minScale = kUserpicMinScale;
	const auto scaleUserpic = minScale
		+ (1. - minScale) * _blobsAnimation->blobs.currentLevel();
	const auto scale = scaleUserpic * enter + 1. * (1. - enter);
	if (scale == 1.) {
		peer()->paintUserpicLeft(p, userpic, x, y, outerWidth, size);
		return;
	}
	ensureUserpicCache(userpic, size);

	PainterHighQualityEnabler hq(p);

	auto target = QRect(
		x + (1 - kWideScale) / 2 * size,
		y + (1 - kWideScale) / 2 * size,
		kWideScale * size,
		kWideScale * size);
	auto shrink = anim::interpolate(
		(1 - kWideScale) / 2 * size,
		0,
		scale);
	auto margins = QMargins(shrink, shrink, shrink, shrink);
	p.drawImage(
		target.marginsAdded(margins),
		_blobsAnimation->userpicCache);
}

void MembersRow::paintMuteIcon(
		Painter &p,
		QRect iconRect,
		MembersRowStyle style) {
	_delegate->rowPaintIcon(p, iconRect, computeIconState(style));
}

auto MembersRow::generatePaintUserpicCallback() -> PaintRoundImageCallback {
	return [=](Painter &p, int x, int y, int outerWidth, int size) {
		const auto outer = outerWidth;
		paintComplexUserpic(p, x, y, outer, size, size, PanelMode::Default);
	};
}

void MembersRow::paintComplexUserpic(
		Painter &p,
		int x,
		int y,
		int outerWidth,
		int sizew,
		int sizeh,
		PanelMode mode,
		bool selected) {
	paintBlobs(p, x, y, sizew, sizeh, mode);
	paintScaledUserpic(
		p,
		ensureUserpicView(),
		x,
		y,
		outerWidth,
		sizew,
		sizeh,
		mode);
}

int MembersRow::statusIconWidth(bool skipIcon) const {
	if (!_statusIcon || !_speaking) {
		return 0;
	}
	const auto shown = _statusIcon->shownAnimation.value(
		_statusIcon->shown ? 1. : 0.);
	const auto iconWidth = skipIcon
		? 0
		: (_statusIcon->speaker.width() + _statusIcon->arcsWidth);
	const auto full = iconWidth
		+ _statusIcon->percentWidth
		+ st::normalFont->spacew;
	return int(std::round(shown * full));
}

int MembersRow::statusIconHeight() const {
	return (_statusIcon && _speaking) ? _statusIcon->speaker.height() : 0;
}

void MembersRow::paintStatusIcon(
		Painter &p,
		int x,
		int y,
		const style::PeerListItem &st,
		const style::font &font,
		bool selected,
		bool skipIcon) {
	if (!_statusIcon) {
		return;
	}
	const auto shown = _statusIcon->shownAnimation.value(
		_statusIcon->shown ? 1. : 0.);
	if (shown == 0.) {
		return;
	}

	p.setFont(font);
	const auto color = (_speaking
		? st.statusFgActive
		: (selected ? st.statusFgOver : st.statusFg))->c;
	p.setPen(color);

	const auto speakerRect = QRect(
		QPoint(x, y + (font->height - statusIconHeight()) / 2),
		_statusIcon->speaker.size());
	const auto arcPosition = speakerRect.topLeft()
		+ QPoint(
			speakerRect.width() - st::groupCallStatusSpeakerArcsSkip,
			speakerRect.height() / 2);
	const auto iconWidth = skipIcon
		? 0
		: (speakerRect.width() + _statusIcon->arcsWidth);
	const auto fullWidth = iconWidth
		+ _statusIcon->percentWidth
		+ st::normalFont->spacew;

	p.save();
	if (shown < 1.) {
		const auto centerx = speakerRect.x() + fullWidth / 2;
		const auto centery = speakerRect.y() + speakerRect.height() / 2;
		p.translate(centerx, centery);
		p.scale(shown, shown);
		p.translate(-centerx, -centery);
	}
	if (!skipIcon) {
		_statusIcon->speaker.paint(
			p,
			speakerRect.topLeft(),
			speakerRect.width(),
			color);
		p.translate(arcPosition);
		_statusIcon->arcs.paint(p, color);
		p.translate(-arcPosition);
	}
	p.setFont(st::normalFont);
	p.setPen(st.statusFgActive);
	p.drawTextLeft(
		x + iconWidth,
		y,
		fullWidth,
		_statusIcon->percent);
	p.restore();
}

void MembersRow::setAbout(const QString &about) {
	if (_aboutText == about) {
		return;
	}
	_aboutText = about;
	_delegate->rowUpdateRow(this);
}

void MembersRow::paintStatusText(
		Painter &p,
		const style::PeerListItem &st,
		int x,
		int y,
		int availableWidth,
		int outerWidth,
		bool selected) {
	paintComplexStatusText(
		p,
		st,
		x,
		y,
		availableWidth,
		outerWidth,
		selected,
		MembersRowStyle::Default);
}

void MembersRow::paintComplexStatusText(
		Painter &p,
		const style::PeerListItem &st,
		int x,
		int y,
		int availableWidth,
		int outerWidth,
		bool selected,
		MembersRowStyle style) {
	const auto skip = (style == MembersRowStyle::Default)
		? _delegate->rowPaintStatusIcon(
			p,
			x,
			y,
			outerWidth,
			this,
			computeIconState(MembersRowStyle::Narrow))
		: 0;
	const auto narrowMode = (skip > 0);
	x += skip;
	availableWidth -= skip;
	const auto &font = st::normalFont;
	const auto about = (style == MembersRowStyle::Video)
		? QString()
		: ((_state == State::RaisedHand && !_raisedHandStatus)
			|| (_state != State::RaisedHand && !_speaking))
		? _aboutText
		: QString();
	if (about.isEmpty()
		&& _state != State::Invited
		&& !_mutedByMe) {
		paintStatusIcon(p, x, y, st, font, selected, narrowMode);

		const auto translatedWidth = statusIconWidth(narrowMode);
		p.translate(translatedWidth, 0);
		const auto guard = gsl::finally([&] {
			p.translate(-translatedWidth, 0);
		});

		const auto &style = (!narrowMode
			|| (_state == State::RaisedHand && _raisedHandStatus))
			? st
			: st::groupCallNarrowMembersListItem;
		PeerListRow::paintStatusText(
			p,
			style,
			x,
			y,
			availableWidth - translatedWidth,
			outerWidth,
			selected);
		return;
	}
	p.setFont(font);
	if (style == MembersRowStyle::Video) {
		p.setPen(st::groupCallVideoSubTextFg);
	} else if (_mutedByMe) {
		p.setPen(st::groupCallMemberMutedIcon);
	} else {
		p.setPen(st::groupCallMemberNotJoinedStatus);
	}
	p.drawTextLeft(
		x,
		y,
		outerWidth,
		(_mutedByMe
			? tr::lng_group_call_muted_by_me_status(tr::now)
			: !about.isEmpty()
			? font->m.elidedText(about, Qt::ElideRight, availableWidth)
			: _delegate->rowIsMe(peer())
			? tr::lng_status_connecting(tr::now)
			: tr::lng_group_call_invited_status(tr::now)));
}

QSize MembersRow::actionSize() const {
	return _delegate->rowIsNarrow() ? QSize() : QSize(
		st::groupCallActiveButton.width,
		st::groupCallActiveButton.height);
}

bool MembersRow::actionDisabled() const {
	return _delegate->rowIsMe(peer())
		|| (_state == State::Invited)
		|| !_delegate->rowCanMuteMembers();
}

QMargins MembersRow::actionMargins() const {
	return QMargins(
		0,
		0,
		st::groupCallMemberButtonSkip,
		0);
}

void MembersRow::paintAction(
		Painter &p,
		int x,
		int y,
		int outerWidth,
		bool selected,
		bool actionSelected) {
	auto size = actionSize();
	const auto iconRect = style::rtlrect(
		x,
		y,
		size.width(),
		size.height(),
		outerWidth);
	if (_state == State::Invited) {
		_actionRipple = nullptr;
	}
	if (_actionRipple) {
		_actionRipple->paint(
			p,
			x + st::groupCallActiveButton.rippleAreaPosition.x(),
			y + st::groupCallActiveButton.rippleAreaPosition.y(),
			outerWidth);
		if (_actionRipple->empty()) {
			_actionRipple.reset();
		}
	}
	paintMuteIcon(p, iconRect);
}

MembersRowDelegate::IconState MembersRow::computeIconState(
		MembersRowStyle style) const {
	const auto speaking = _speakingAnimation.value(_speaking ? 1. : 0.);
	const auto active = _activeAnimation.value(
		(_state == State::Active) ? 1. : 0.);
	const auto muted = _mutedAnimation.value(
		(_state == State::Muted || _state == State::RaisedHand) ? 1. : 0.);
	return {
		.speaking = speaking,
		.active = active,
		.muted = muted,
		.mutedByMe = _mutedByMe,
		.raisedHand = (_state == State::RaisedHand),
		.invited = (_state == State::Invited),
		.style = style,
	};
}

void MembersRow::showContextMenu() {
	return _delegate->rowShowContextMenu(this);
}

void MembersRow::refreshStatus() {
	setCustomStatus(
		(_speaking
			? tr::lng_group_call_active(tr::now)
			: _raisedHandStatus
			? tr::lng_group_call_raised_hand_status(tr::now)
			: tr::lng_group_call_inactive(tr::now)),
		_speaking);
}

void MembersRow::addActionRipple(QPoint point, Fn<void()> updateCallback) {
	if (!_actionRipple) {
		auto mask = Ui::RippleAnimation::ellipseMask(QSize(
			st::groupCallActiveButton.rippleAreaSize,
			st::groupCallActiveButton.rippleAreaSize));
		_actionRipple = std::make_unique<Ui::RippleAnimation>(
			st::groupCallActiveButton.ripple,
			std::move(mask),
			std::move(updateCallback));
	}
	_actionRipple->add(point - st::groupCallActiveButton.rippleAreaPosition);
}

void MembersRow::refreshName(const style::PeerListItem &st) {
	PeerListRow::refreshName(st);
	//_narrowName = Ui::Text::String();
}

void MembersRow::stopLastActionRipple() {
	if (_actionRipple) {
		_actionRipple->lastStop();
	}
}

} // namespace Calls::Group
