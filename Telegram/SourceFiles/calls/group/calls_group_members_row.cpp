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
, _delegate(delegate) {
	refreshStatus();
	_aboutText = participantPeer->about();
}

MembersRow::~MembersRow() = default;

void MembersRow::setSkipLevelUpdate(bool value) {
	_skipLevelUpdate = value;
}

void MembersRow::updateState(
		const Data::GroupCallParticipant *participant) {
	setSsrc(participant ? participant->ssrc : 0);
	setVolume(participant
		? participant->volume
		: Group::kDefaultVolume);
	if (!participant) {
		setState(State::Invited);
		setSounding(false);
		setSpeaking(false);
		_raisedHandRating = 0;
	} else if (!participant->muted
		|| (participant->sounding && participant->ssrc != 0)) {
		setState(participant->mutedByMe ? State::MutedByMe : State::Active);
		setSounding(participant->sounding && participant->ssrc != 0);
		setSpeaking(participant->speaking && participant->ssrc != 0);
		_raisedHandRating = 0;
	} else if (participant->canSelfUnmute) {
		setState(participant->mutedByMe
			? State::MutedByMe
			: State::Inactive);
		setSounding(false);
		setSpeaking(false);
		_raisedHandRating = 0;
	} else {
		_raisedHandRating = participant->raisedHandRating;
		setState(_raisedHandRating ? State::RaisedHand : State::Muted);
		setSounding(false);
		setSpeaking(false);
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
		|| (_state == State::MutedByMe)
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

void MembersRow::setSsrc(uint32 ssrc) {
	_ssrc = ssrc;
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

bool MembersRow::paintVideo(
		Painter &p,
		int x,
		int y,
		int sizew,
		int sizeh,
		PanelMode mode) {
	if (!_videoTrackShown) {
		return false;
	}
	const auto guard = gsl::finally([&] {
		_videoTrackShown->markFrameShown();
	});
	const auto videoSize = _videoTrackShown->frameSize();
	if (videoSize.isEmpty()
		|| _videoTrackShown->state() != Webrtc::VideoState::Active) {
		return false;
	}
	const auto videow = videoSize.width();
	const auto videoh = videoSize.height();
	const auto resize = (videow * sizeh > videoh * sizew)
		? QSize(videow * sizeh / videoh, sizeh)
		: QSize(sizew, videoh * sizew / videow);
	const auto request = Webrtc::FrameRequest{
		.resize = resize * cIntRetinaFactor(),
		.outer = QSize(sizew, sizeh) * cIntRetinaFactor(),
	};
	const auto frame = _videoTrackShown->frame(request);
	auto copy = frame; // #TODO calls optimize.
	copy.detach();
	if (mode == PanelMode::Default) {
		Images::prepareCircle(copy);
	} else {
		Images::prepareRound(copy, ImageRoundRadius::Large);
	}
	p.drawImage(
		QRect(QPoint(x, y), copy.size() / cIntRetinaFactor()),
		copy);
	return true;
}

std::tuple<int, int, int> MembersRow::UserpicInNarrowMode(
		int x,
		int y,
		int sizew,
		int sizeh) {
	const auto useSize = st::groupCallMembersList.item.photoSize;
	const auto skipx = (sizew - useSize) / 2;
	return { x + skipx, y + st::groupCallNarrowUserpicTop, useSize };
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
	if (mode == PanelMode::Wide) {
		std::tie(x, y, size) = UserpicInNarrowMode(x, y, sizew, sizeh);
	}
	const auto mutedByMe = (_state == State::MutedByMe);
	const auto shift = QPointF(x + size / 2., y + size / 2.);
	auto hq = PainterHighQualityEnabler(p);
	p.translate(shift);
	const auto brush = mutedByMe
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
	if (mode == PanelMode::Wide) {
		std::tie(x, y, size) = UserpicInNarrowMode(x, y, sizew, sizeh);
	}
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

void MembersRow::paintNarrowName(
		Painter &p,
		int x,
		int y,
		int sizew,
		int sizeh,
		MembersRowStyle style) {
	if (_narrowName.isEmpty()) {
		_narrowName.setText(
			st::semiboldTextStyle,
			generateShortName(),
			Ui::NameTextOptions());
	}
	if (style == MembersRowStyle::Video) {
		_delegate->rowPaintNarrowShadow(p, x, y, sizew, sizeh);
	}
	const auto &icon = st::groupCallVideoCrossLine.icon;
	const auto added = icon.width() - st::groupCallNarrowIconLess;
	const auto available = sizew - 2 * st::normalFont->spacew - added;
	const auto use = std::min(available, _narrowName.maxWidth());
	const auto left = x + (sizew - use - added) / 2;
	const auto iconRect = QRect(
		left - st::groupCallNarrowIconLess,
		y + st::groupCallNarrowIconTop,
		icon.width(),
		icon.height());
	const auto &state = computeIconState(style);
	_delegate->rowPaintIcon(p, iconRect, state);

	p.setPen([&] {
		if (style == MembersRowStyle::Video) {
			return st::groupCallVideoTextFg->p;
		} else if (state.speaking == 1. && !state.mutedByMe) {
			return st::groupCallMemberActiveIcon->p;
		} else if (state.speaking == 0.) {
			if (state.active == 1.) {
				return st::groupCallMemberInactiveIcon->p;
			} else if (state.active == 0.) {
				if (state.muted == 1.) {
					return state.raisedHand
						? st::groupCallMemberInactiveStatus->p
						: st::groupCallMemberMutedIcon->p;
				} else if (state.muted == 0.) {
					return st::groupCallMemberInactiveIcon->p;
				}
			}
		}
		const auto activeInactiveColor = anim::color(
			st::groupCallMemberInactiveIcon,
			(state.mutedByMe
				? st::groupCallMemberMutedIcon
				: st::groupCallMemberActiveIcon),
			state.speaking);
		return anim::pen(
			activeInactiveColor,
			st::groupCallMemberMutedIcon,
			state.muted);
	}());
	const auto nameLeft = iconRect.x() + icon.width();
	const auto nameTop = y + st::groupCallNarrowNameTop;
	if (use == available) {
		_narrowName.drawLeftElided(p, nameLeft, nameTop, available, sizew);
	} else {
		_narrowName.drawLeft(p, nameLeft, nameTop, available, sizew);
	}
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
	if (mode == PanelMode::Wide) {
		if (paintVideo(p, x, y, sizew, sizeh, mode)) {
			paintNarrowName(p, x, y, sizew, sizeh, MembersRowStyle::Video);
			_delegate->rowPaintNarrowBorder(p, x, y, this);
			return;
		}
		_delegate->rowPaintNarrowBackground(p, x, y, selected);
		paintRipple(p, x, y, outerWidth);
	}
	paintBlobs(p, x, y, sizew, sizeh, mode);
	if (mode == PanelMode::Default
		&& paintVideo(p, x, y, sizew, sizeh, mode)) {
		return;
	}
	paintScaledUserpic(
		p,
		ensureUserpicView(),
		x,
		y,
		outerWidth,
		sizew,
		sizeh,
		mode);
	if (mode == PanelMode::Wide) {
		paintNarrowName(p, x, y, sizew, sizeh, MembersRowStyle::Userpic);
		_delegate->rowPaintNarrowBorder(p, x, y, this);
	}
}

int MembersRow::statusIconWidth() const {
	if (!_statusIcon || !_speaking) {
		return 0;
	}
	const auto shown = _statusIcon->shownAnimation.value(
		_statusIcon->shown ? 1. : 0.);
	const auto full = _statusIcon->speaker.width()
		+ _statusIcon->arcsWidth
		+ _statusIcon->percentWidth
		+ st::normalFont->spacew;
	return int(std::round(shown * full));
}

int MembersRow::statusIconHeight() const {
	return (_statusIcon && _speaking) ? _statusIcon->speaker.height() : 0;
}

void MembersRow::paintStatusIcon(
		Painter &p,
		const style::PeerListItem &st,
		const style::font &font,
		bool selected) {
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
		st.statusPosition
			+ QPoint(0, (font->height - statusIconHeight()) / 2),
		_statusIcon->speaker.size());
	const auto arcPosition = speakerRect.topLeft()
		+ QPoint(
			speakerRect.width() - st::groupCallStatusSpeakerArcsSkip,
			speakerRect.height() / 2);
	const auto fullWidth = speakerRect.width()
		+ _statusIcon->arcsWidth
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
	_statusIcon->speaker.paint(
		p,
		speakerRect.topLeft(),
		speakerRect.width(),
		color);
	p.translate(arcPosition);
	_statusIcon->arcs.paint(p, color);
	p.translate(-arcPosition);
	p.setFont(st::normalFont);
	p.setPen(st.statusFgActive);
	p.drawTextLeft(
		st.statusPosition.x() + speakerRect.width() + _statusIcon->arcsWidth,
		st.statusPosition.y(),
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
		MembersRowStyle::None);
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
	const auto &font = st::normalFont;
	const auto about = (style == MembersRowStyle::LargeVideo)
		? QString()
		: (_state == State::Inactive
		|| _state == State::Muted
		|| (_state == State::RaisedHand && !_raisedHandStatus))
		? _aboutText
		: QString();
	if (about.isEmpty()
		&& _state != State::Invited
		&& _state != State::MutedByMe) {
		paintStatusIcon(p, st, font, selected);

		const auto translatedWidth = statusIconWidth();
		p.translate(translatedWidth, 0);
		const auto guard = gsl::finally([&] {
			p.translate(-translatedWidth, 0);
		});

		PeerListRow::paintStatusText(
			p,
			st,
			x,
			y,
			availableWidth - translatedWidth,
			outerWidth,
			selected);
		return;
	}
	p.setFont(font);
	if (style == MembersRowStyle::LargeVideo) {
		p.setPen(st::groupCallVideoSubTextFg);
	} else if (_state == State::MutedByMe) {
		p.setPen(st::groupCallMemberMutedIcon);
	} else {
		p.setPen(st::groupCallMemberNotJoinedStatus);
	}
	p.drawTextLeft(
		x,
		y,
		outerWidth,
		(_state == State::MutedByMe
			? tr::lng_group_call_muted_by_me_status(tr::now)
			: !about.isEmpty()
			? font->m.elidedText(about, Qt::ElideRight, availableWidth)
			: _delegate->rowIsMe(peer())
			? tr::lng_status_connecting(tr::now)
			: tr::lng_group_call_invited_status(tr::now)));
}

QSize MembersRow::actionSize() const {
	return QSize(
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
		st::groupCallMemberInvited.paint(
			p,
			QPoint(x, y) + st::groupCallMemberInvitedPosition,
			outerWidth);
		return;
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
	const auto mutedByMe = (_state == State::MutedByMe);
	return {
		.speaking = speaking,
		.active = active,
		.muted = muted,
		.mutedByMe = (_state == State::MutedByMe),
		.raisedHand = (_state == State::RaisedHand),
		.style = style,
	};
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

not_null<Webrtc::VideoTrack*> MembersRow::createVideoTrack(
		const std::string &endpoint) {
	_videoTrackShown = nullptr;
	_videoTrackEndpoint = endpoint;
	_videoTrack = std::make_unique<Webrtc::VideoTrack>(
		Webrtc::VideoState::Active);
	setVideoTrack(_videoTrack.get());
	return _videoTrack.get();
}

const std::string &MembersRow::videoTrackEndpoint() const {
	return _videoTrackEndpoint;
}

void MembersRow::clearVideoTrack() {
	_videoTrackLifetime.destroy();
	_videoTrackEndpoint = std::string();
	_videoTrackShown = nullptr;
	_videoTrack = nullptr;
	_delegate->rowUpdateRow(this);
}

void MembersRow::setVideoTrack(not_null<Webrtc::VideoTrack*> track) {
	_videoTrackLifetime.destroy();
	_videoTrackShown = track;
	_videoTrackShown->renderNextFrame(
	) | rpl::start_with_next([=] {
		_delegate->rowUpdateRow(this);
		if (_videoTrackShown->frameSize().isEmpty()) {
			_videoTrackShown->markFrameShown();
		}
	}, _videoTrackLifetime);
	_delegate->rowUpdateRow(this);
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
	_narrowName = Ui::Text::String();
}

void MembersRow::stopLastActionRipple() {
	if (_actionRipple) {
		_actionRipple->lastStop();
	}
}

} // namespace Calls::Group
