/*
This file is part of Telegram Desktop,
the official desktop application for the Telegram messaging service.

For license and copyright information please follow this link:
https://github.com/telegramdesktop/tdesktop/blob/master/LEGAL
*/
#include "calls/calls_group_members.h"

#include "calls/calls_group_call.h"
#include "calls/calls_group_common.h"
#include "calls/calls_group_menu.h"
#include "calls/calls_volume_item.h"
#include "data/data_channel.h"
#include "data/data_chat.h"
#include "data/data_user.h"
#include "data/data_changes.h"
#include "data/data_group_call.h"
#include "data/data_peer_values.h" // Data::CanWriteValue.
#include "data/data_session.h" // Data::Session::invitedToCallUsers.
#include "settings/settings_common.h" // Settings::CreateButton.
#include "info/profile/info_profile_values.h" // Info::Profile::AboutValue.
#include "ui/paint/arcs.h"
#include "ui/paint/blobs.h"
#include "ui/widgets/buttons.h"
#include "ui/widgets/scroll_area.h"
#include "ui/widgets/popup_menu.h"
#include "ui/text/text_utilities.h"
#include "ui/effects/ripple_animation.h"
#include "ui/effects/cross_line.h"
#include "core/application.h" // Core::App().domain, Core::App().activeWindow.
#include "main/main_domain.h" // Core::App().domain().activate.
#include "main/main_session.h"
#include "base/timer.h"
#include "boxes/peers/edit_participants_box.h" // SubscribeToMigration.
#include "lang/lang_keys.h"
#include "window/window_controller.h" // Controller::sessionController.
#include "window/window_session_controller.h"
#include "styles/style_calls.h"

namespace Calls::Group {
namespace {

constexpr auto kBlobsEnterDuration = crl::time(250);
constexpr auto kLevelDuration = 100. + 500. * 0.23;
constexpr auto kBlobScale = 0.605;
constexpr auto kMinorBlobFactor = 0.9f;
constexpr auto kUserpicMinScale = 0.8;
constexpr auto kMaxLevel = 1.;
constexpr auto kWideScale = 5;
constexpr auto kKeepRaisedHandStatusDuration = 3 * crl::time(1000);

const auto kSpeakerThreshold = std::vector<float>{
	Group::kDefaultVolume * 0.1f / Group::kMaxVolume,
	Group::kDefaultVolume * 0.9f / Group::kMaxVolume };

constexpr auto kArcsStrokeRatio = 0.8;

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

class Row;

class RowDelegate {
public:
	struct IconState {
		float64 speaking = 0.;
		float64 active = 0.;
		float64 muted = 0.;
		bool mutedByMe = false;
		bool raisedHand = false;
	};
	virtual bool rowIsMe(not_null<PeerData*> participantPeer) = 0;
	virtual bool rowCanMuteMembers() = 0;
	virtual void rowUpdateRow(not_null<Row*> row) = 0;
	virtual void rowScheduleRaisedHandStatusRemove(not_null<Row*> row) = 0;
	virtual void rowPaintIcon(
		Painter &p,
		QRect rect,
		IconState state) = 0;
};

class Row final : public PeerListRow {
public:
	Row(
		not_null<RowDelegate*> delegate,
		not_null<PeerData*> participantPeer);

	enum class State {
		Active,
		Inactive,
		Muted,
		RaisedHand,
		MutedByMe,
		Invited,
	};

	void setAbout(const QString &about);
	void setSkipLevelUpdate(bool value);
	void updateState(const Data::GroupCall::Participant *participant);
	void updateLevel(float level);
	void updateBlobAnimation(crl::time now);
	void clearRaisedHandStatus();
	[[nodiscard]] State state() const {
		return _state;
	}
	[[nodiscard]] uint32 ssrc() const {
		return _ssrc;
	}
	[[nodiscard]] bool sounding() const {
		return _sounding;
	}
	[[nodiscard]] bool speaking() const {
		return _speaking;
	}
	[[nodiscard]] crl::time speakingLastTime() const {
		return _speakingLastTime;
	}
	[[nodiscard]] int volume() const {
		return _volume;
	}
	[[nodiscard]] uint64 raisedHandRating() const {
		return _raisedHandRating;
	}

	void addActionRipple(QPoint point, Fn<void()> updateCallback) override;
	void stopLastActionRipple() override;

	QSize actionSize() const override {
		return QSize(
			st::groupCallActiveButton.width,
			st::groupCallActiveButton.height);
	}
	bool actionDisabled() const override {
		return _delegate->rowIsMe(peer())
			|| (_state == State::Invited)
			|| !_delegate->rowCanMuteMembers();
	}
	QMargins actionMargins() const override {
		return QMargins(
			0,
			0,
			st::groupCallMemberButtonSkip,
			0);
	}
	void paintAction(
		Painter &p,
		int x,
		int y,
		int outerWidth,
		bool selected,
		bool actionSelected) override;

	auto generatePaintUserpicCallback() -> PaintRoundImageCallback override;

	void paintStatusText(
		Painter &p,
		const style::PeerListItem &st,
		int x,
		int y,
		int availableWidth,
		int outerWidth,
		bool selected) override;

private:
	struct BlobsAnimation {
		BlobsAnimation(
			std::vector<Ui::Paint::Blobs::BlobData> blobDatas,
			float levelDuration,
			float maxLevel)
		: blobs(std::move(blobDatas), levelDuration, maxLevel) {
			style::PaletteChanged(
			) | rpl::start_with_next([=] {
				userpicCache = QImage();
			}, lifetime);
		}

		Ui::Paint::Blobs blobs;
		crl::time lastTime = 0;
		crl::time lastSoundingUpdateTime = 0;
		float64 enter = 0.;

		QImage userpicCache;
		InMemoryKey userpicKey;

		rpl::lifetime lifetime;
	};

	struct StatusIcon {
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

	int statusIconWidth() const;
	int statusIconHeight() const;
	void paintStatusIcon(
		Painter &p,
		const style::PeerListItem &st,
		const style::font &font,
		bool selected);

	void refreshStatus() override;
	void setSounding(bool sounding);
	void setSpeaking(bool speaking);
	void setState(State state);
	void setSsrc(uint32 ssrc);
	void setVolume(int volume);

	void ensureUserpicCache(
		std::shared_ptr<Data::CloudImageView> &view,
		int size);

	const not_null<RowDelegate*> _delegate;
	State _state = State::Inactive;
	std::unique_ptr<Ui::RippleAnimation> _actionRipple;
	std::unique_ptr<BlobsAnimation> _blobsAnimation;
	std::unique_ptr<StatusIcon> _statusIcon;
	Ui::Animations::Simple _speakingAnimation; // For gray-red/green icon.
	Ui::Animations::Simple _mutedAnimation; // For gray/red icon.
	Ui::Animations::Simple _activeAnimation; // For icon cross animation.
	QString _aboutText;
	crl::time _speakingLastTime = 0;
	uint64 _raisedHandRating = 0;
	uint32 _ssrc = 0;
	int _volume = Group::kDefaultVolume;
	bool _sounding = false;
	bool _speaking = false;
	bool _raisedHandStatus = false;
	bool _skipLevelUpdate = false;

};

class MembersController final
	: public PeerListController
	, public RowDelegate
	, public base::has_weak_ptr {
public:
	MembersController(
		not_null<GroupCall*> call,
		not_null<QWidget*> menuParent);
	~MembersController();

	using MuteRequest = Group::MuteRequest;
	using VolumeRequest = Group::VolumeRequest;

	Main::Session &session() const override;
	void prepare() override;
	void rowClicked(not_null<PeerListRow*> row) override;
	void rowActionClicked(not_null<PeerListRow*> row) override;
	base::unique_qptr<Ui::PopupMenu> rowContextMenu(
		QWidget *parent,
		not_null<PeerListRow*> row) override;
	void loadMoreRows() override;

	[[nodiscard]] rpl::producer<int> fullCountValue() const {
		return _fullCount.value();
	}
	[[nodiscard]] rpl::producer<MuteRequest> toggleMuteRequests() const;
	[[nodiscard]] rpl::producer<VolumeRequest> changeVolumeRequests() const;
	[[nodiscard]] auto kickParticipantRequests() const
		-> rpl::producer<not_null<PeerData*>>;

	bool rowIsMe(not_null<PeerData*> participantPeer) override;
	bool rowCanMuteMembers() override;
	void rowUpdateRow(not_null<Row*> row) override;
	void rowScheduleRaisedHandStatusRemove(not_null<Row*> row) override;
	void rowPaintIcon(
		Painter &p,
		QRect rect,
		IconState state) override;

private:
	[[nodiscard]] std::unique_ptr<Row> createRowForMe();
	[[nodiscard]] std::unique_ptr<Row> createRow(
		const Data::GroupCall::Participant &participant);
	[[nodiscard]] std::unique_ptr<Row> createInvitedRow(
		not_null<PeerData*> participantPeer);

	[[nodiscard]] bool isMe(not_null<PeerData*> participantPeer) const;
	void prepareRows(not_null<Data::GroupCall*> real);
	//void repaintByTimer();

	[[nodiscard]] base::unique_qptr<Ui::PopupMenu> createRowContextMenu(
		QWidget *parent,
		not_null<PeerListRow*> row);
	void addMuteActionsToContextMenu(
		not_null<Ui::PopupMenu*> menu,
		not_null<PeerData*> participantPeer,
		bool participantIsCallAdmin,
		not_null<Row*> row);
	void setupListChangeViewers();
	void subscribeToChanges(not_null<Data::GroupCall*> real);
	void updateRow(
		const std::optional<Data::GroupCall::Participant> &was,
		const Data::GroupCall::Participant &now);
	void updateRow(
		not_null<Row*> row,
		const Data::GroupCall::Participant *participant);
	void removeRow(not_null<Row*> row);
	void updateRowLevel(not_null<Row*> row, float level);
	void checkRowPosition(not_null<Row*> row);
	[[nodiscard]] bool needToReorder(not_null<Row*> row) const;
	[[nodiscard]] bool allRowsAboveAreSpeaking(not_null<Row*> row) const;
	[[nodiscard]] bool allRowsAboveMoreImportantThanHand(
		not_null<Row*> row,
		uint64 raiseHandRating) const;
	Row *findRow(not_null<PeerData*> participantPeer) const;

	void appendInvitedUsers();
	void scheduleRaisedHandStatusRemove();

	const not_null<GroupCall*> _call;
	not_null<PeerData*> _peer;
	bool _prepared = false;

	rpl::event_stream<MuteRequest> _toggleMuteRequests;
	rpl::event_stream<VolumeRequest> _changeVolumeRequests;
	rpl::event_stream<not_null<PeerData*>> _kickParticipantRequests;
	rpl::variable<int> _fullCount = 1;

	not_null<QWidget*> _menuParent;
	base::unique_qptr<Ui::PopupMenu> _menu;
	base::flat_set<not_null<PeerData*>> _menuCheckRowsAfterHidden;

	base::flat_map<PeerListRowId, crl::time> _raisedHandStatusRemoveAt;
	base::Timer _raisedHandStatusRemoveTimer;

	base::flat_map<uint32, not_null<Row*>> _soundingRowBySsrc;
	Ui::Animations::Basic _soundingAnimation;

	crl::time _soundingAnimationHideLastTime = 0;
	bool _skipRowLevelUpdate = false;

	Ui::CrossLineAnimation _inactiveCrossLine;
	Ui::CrossLineAnimation _coloredCrossLine;

	rpl::lifetime _lifetime;

};

[[nodiscard]] QString StatusPercentString(float volume) {
	return QString::number(int(std::round(volume * 200))) + '%';
}

[[nodiscard]] int StatusPercentWidth(const QString &percent) {
	return st::normalFont->width(percent);
}

Row::StatusIcon::StatusIcon(bool shown, float volume)
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

Row::Row(
	not_null<RowDelegate*> delegate,
	not_null<PeerData*> participantPeer)
: PeerListRow(participantPeer)
, _delegate(delegate) {
	refreshStatus();
	_aboutText = participantPeer->about();
}

void Row::setSkipLevelUpdate(bool value) {
	_skipLevelUpdate = value;
}

void Row::updateState(const Data::GroupCall::Participant *participant) {
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

void Row::setSpeaking(bool speaking) {
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

void Row::setSounding(bool sounding) {
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

void Row::clearRaisedHandStatus() {
	if (!_raisedHandStatus) {
		return;
	}
	_raisedHandStatus = false;
	refreshStatus();
	_delegate->rowUpdateRow(this);
}

void Row::setState(State state) {
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

void Row::setSsrc(uint32 ssrc) {
	_ssrc = ssrc;
}

void Row::setVolume(int volume) {
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

void Row::updateLevel(float level) {
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

void Row::updateBlobAnimation(crl::time now) {
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

void Row::ensureUserpicCache(
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

auto Row::generatePaintUserpicCallback() -> PaintRoundImageCallback {
	auto userpic = ensureUserpicView();
	return [=](Painter &p, int x, int y, int outerWidth, int size) mutable {
		if (_blobsAnimation) {
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

			const auto enter = _blobsAnimation->enter;
			const auto &minScale = kUserpicMinScale;
			const auto scaleUserpic = minScale
				+ (1. - minScale) * _blobsAnimation->blobs.currentLevel();
			const auto scale = scaleUserpic * enter + 1. * (1. - enter);
			if (scale == 1.) {
				peer()->paintUserpicLeft(p, userpic, x, y, outerWidth, size);
			} else {
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
		} else {
			peer()->paintUserpicLeft(p, userpic, x, y, outerWidth, size);
		}
	};
}

int Row::statusIconWidth() const {
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

int Row::statusIconHeight() const {
	return (_statusIcon && _speaking) ? _statusIcon->speaker.height() : 0;
}

void Row::paintStatusIcon(
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

void Row::setAbout(const QString &about) {
	if (_aboutText == about) {
		return;
	}
	_aboutText = about;
	_delegate->rowUpdateRow(this);
}

void Row::paintStatusText(
		Painter &p,
		const style::PeerListItem &st,
		int x,
		int y,
		int availableWidth,
		int outerWidth,
		bool selected) {
	const auto &font = st::normalFont;
	const auto about = (_state == State::Inactive
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
	if (_state == State::MutedByMe) {
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

void Row::paintAction(
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
	const auto speaking = _speakingAnimation.value(_speaking ? 1. : 0.);
	const auto active = _activeAnimation.value((_state == State::Active) ? 1. : 0.);
	const auto muted = _mutedAnimation.value(
		(_state == State::Muted || _state == State::RaisedHand) ? 1. : 0.);
	const auto mutedByMe = (_state == State::MutedByMe);
	_delegate->rowPaintIcon(p, iconRect, {
		.speaking = speaking,
		.active = active,
		.muted = muted,
		.mutedByMe = (_state == State::MutedByMe),
		.raisedHand = (_state == State::RaisedHand),
	});
}

void Row::refreshStatus() {
	setCustomStatus(
		(_speaking
			? tr::lng_group_call_active(tr::now)
			: _raisedHandStatus
			? tr::lng_group_call_raised_hand_status(tr::now)
			: tr::lng_group_call_inactive(tr::now)),
		_speaking);
}

void Row::addActionRipple(QPoint point, Fn<void()> updateCallback) {
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

void Row::stopLastActionRipple() {
	if (_actionRipple) {
		_actionRipple->lastStop();
	}
}

MembersController::MembersController(
	not_null<GroupCall*> call,
	not_null<QWidget*> menuParent)
: _call(call)
, _peer(call->peer())
, _menuParent(menuParent)
, _raisedHandStatusRemoveTimer([=] { scheduleRaisedHandStatusRemove(); })
, _inactiveCrossLine(st::groupCallMemberInactiveCrossLine)
, _coloredCrossLine(st::groupCallMemberColoredCrossLine) {
	setupListChangeViewers();

	style::PaletteChanged(
	) | rpl::start_with_next([=] {
		_inactiveCrossLine.invalidate();
		_coloredCrossLine.invalidate();
	}, _lifetime);

	rpl::combine(
		rpl::single(anim::Disabled()) | rpl::then(anim::Disables()),
		Core::App().appDeactivatedValue()
	) | rpl::start_with_next([=](bool animDisabled, bool deactivated) {
		const auto hide = !(!animDisabled && !deactivated);

		if (!(hide && _soundingAnimationHideLastTime)) {
			_soundingAnimationHideLastTime = hide ? crl::now() : 0;
		}
		for (const auto &[_, row] : _soundingRowBySsrc) {
			if (hide) {
				updateRowLevel(row, 0.);
			}
			row->setSkipLevelUpdate(hide);
		}
		if (!hide && !_soundingAnimation.animating()) {
			_soundingAnimation.start();
		}
		_skipRowLevelUpdate = hide;
	}, _lifetime);

	_soundingAnimation.init([=](crl::time now) {
		if (const auto &last = _soundingAnimationHideLastTime; (last > 0)
			&& (now - last >= kBlobsEnterDuration)) {
			_soundingAnimation.stop();
			return false;
		}
		for (const auto &[ssrc, row] : _soundingRowBySsrc) {
			row->updateBlobAnimation(now);
			delegate()->peerListUpdateRow(row);
		}
		return true;
	});

	_peer->session().changes().peerUpdates(
		Data::PeerUpdate::Flag::About
	) | rpl::start_with_next([=](const Data::PeerUpdate &update) {
		if (const auto row = findRow(update.peer)) {
			row->setAbout(update.peer->about());
		}
	}, _lifetime);
}

MembersController::~MembersController() {
	base::take(_menu);
}

void MembersController::setupListChangeViewers() {
	_call->real(
	) | rpl::start_with_next([=](not_null<Data::GroupCall*> real) {
		subscribeToChanges(real);
	}, _lifetime);

	_call->stateValue(
	) | rpl::start_with_next([=] {
		if (const auto real = _call->lookupReal()) {
			//updateRow(channel->session().user());
		}
	}, _lifetime);

	_call->levelUpdates(
	) | rpl::start_with_next([=](const LevelUpdate &update) {
		const auto i = _soundingRowBySsrc.find(update.ssrc);
		if (i != end(_soundingRowBySsrc)) {
			updateRowLevel(i->second, update.value);
		}
	}, _lifetime);

	_call->rejoinEvents(
	) | rpl::start_with_next([=](const Group::RejoinEvent &event) {
		const auto guard = gsl::finally([&] {
			delegate()->peerListRefreshRows();
		});
		if (const auto row = findRow(event.wasJoinAs)) {
			removeRow(row);
		}
		if (findRow(event.nowJoinAs)) {
			return;
		} else if (auto row = createRowForMe()) {
			delegate()->peerListAppendRow(std::move(row));
		}
	}, _lifetime);
}

void MembersController::subscribeToChanges(not_null<Data::GroupCall*> real) {
	_fullCount = real->fullCountValue();

	real->participantsSliceAdded(
	) | rpl::start_with_next([=] {
		prepareRows(real);
	}, _lifetime);

	using Update = Data::GroupCall::ParticipantUpdate;
	real->participantUpdated(
	) | rpl::start_with_next([=](const Update &update) {
		Expects(update.was.has_value() || update.now.has_value());

		const auto participantPeer = update.was
			? update.was->peer
			: update.now->peer;
		if (!update.now) {
			if (const auto row = findRow(participantPeer)) {
				const auto owner = &participantPeer->owner();
				if (isMe(participantPeer)) {
					updateRow(row, nullptr);
				} else {
					removeRow(row);
					delegate()->peerListRefreshRows();
				}
			}
		} else {
			updateRow(update.was, *update.now);
		}
	}, _lifetime);

	if (_prepared) {
		appendInvitedUsers();
	}
}

void MembersController::appendInvitedUsers() {
	if (const auto id = _call->id()) {
		for (const auto user : _peer->owner().invitedToCallUsers(id)) {
			if (auto row = createInvitedRow(user)) {
				delegate()->peerListAppendRow(std::move(row));
			}
		}
		delegate()->peerListRefreshRows();
	}

	using Invite = Data::Session::InviteToCall;
	_peer->owner().invitesToCalls(
	) | rpl::filter([=](const Invite &invite) {
		return (invite.id == _call->id());
	}) | rpl::start_with_next([=](const Invite &invite) {
		if (auto row = createInvitedRow(invite.user)) {
			delegate()->peerListAppendRow(std::move(row));
			delegate()->peerListRefreshRows();
		}
	}, _lifetime);
}

void MembersController::updateRow(
		const std::optional<Data::GroupCall::Participant> &was,
		const Data::GroupCall::Participant &now) {
	auto reorderIfInvitedBefore = 0;
	auto checkPosition = (Row*)nullptr;
	auto addedToBottom = (Row*)nullptr;
	if (const auto row = findRow(now.peer)) {
		if (row->state() == Row::State::Invited) {
			reorderIfInvitedBefore = row->absoluteIndex();
		}
		updateRow(row, &now);
		if ((now.speaking && (!was || !was->speaking))
			|| (now.raisedHandRating != (was ? was->raisedHandRating : 0))
			|| (!now.canSelfUnmute && was && was->canSelfUnmute)) {
			checkPosition = row;
		}
	} else if (auto row = createRow(now)) {
		if (row->speaking()) {
			delegate()->peerListPrependRow(std::move(row));
		} else {
			reorderIfInvitedBefore = delegate()->peerListFullRowsCount();
			if (now.raisedHandRating != 0) {
				checkPosition = row.get();
			} else {
				addedToBottom = row.get();
			}
			delegate()->peerListAppendRow(std::move(row));
		}
		delegate()->peerListRefreshRows();
	}
	static constexpr auto kInvited = Row::State::Invited;
	const auto reorder = [&] {
		const auto count = reorderIfInvitedBefore;
		if (count <= 0) {
			return false;
		}
		const auto row = delegate()->peerListRowAt(
			reorderIfInvitedBefore - 1).get();
		return (static_cast<Row*>(row)->state() == kInvited);
	}();
	if (reorder) {
		delegate()->peerListPartitionRows([](const PeerListRow &row) {
			return static_cast<const Row&>(row).state() != kInvited;
		});
	}
	if (checkPosition) {
		checkRowPosition(checkPosition);
	} else if (addedToBottom) {
		const auto real = _call->lookupReal();
		if (real && real->joinedToTop()) {
			const auto proj = [&](const PeerListRow &other) {
				const auto &real = static_cast<const Row&>(other);
				return real.speaking()
					? 2
					: (&real == addedToBottom)
					? 1
					: 0;
			};
			delegate()->peerListSortRows([&](
					const PeerListRow &a,
					const PeerListRow &b) {
				return proj(a) > proj(b);
			});
		}
	}
}

bool MembersController::allRowsAboveAreSpeaking(not_null<Row*> row) const {
	const auto count = delegate()->peerListFullRowsCount();
	for (auto i = 0; i != count; ++i) {
		const auto above = delegate()->peerListRowAt(i);
		if (above == row) {
			// All rows above are speaking.
			return true;
		} else if (!static_cast<Row*>(above.get())->speaking()) {
			break;
		}
	}
	return false;
}

bool MembersController::allRowsAboveMoreImportantThanHand(
		not_null<Row*> row,
		uint64 raiseHandRating) const {
	Expects(raiseHandRating > 0);

	const auto count = delegate()->peerListFullRowsCount();
	for (auto i = 0; i != count; ++i) {
		const auto above = delegate()->peerListRowAt(i);
		if (above == row) {
			// All rows above are 'more important' than this raised hand.
			return true;
		}
		const auto real = static_cast<Row*>(above.get());
		const auto state = real->state();
		if (state == Row::State::Muted
			|| (state == Row::State::RaisedHand
				&& real->raisedHandRating() < raiseHandRating)) {
			break;
		}
	}
	return false;
}

bool MembersController::needToReorder(not_null<Row*> row) const {
	// All reorder cases:
	// - bring speaking up
	// - bring raised hand up
	// - bring muted down

	if (row->speaking()) {
		return !allRowsAboveAreSpeaking(row);
	} else if (!_peer->canManageGroupCall()) {
		// Raising hands reorder participants only for voice chat admins.
		return false;
	}

	const auto rating = row->raisedHandRating();
	if (!rating && row->state() != Row::State::Muted) {
		return false;
	}
	if (rating > 0 && !allRowsAboveMoreImportantThanHand(row, rating)) {
		return true;
	}
	const auto index = row->absoluteIndex();
	if (index + 1 == delegate()->peerListFullRowsCount()) {
		// Last one, can't bring lower.
		return false;
	}
	const auto next = delegate()->peerListRowAt(index + 1);
	const auto state = static_cast<Row*>(next.get())->state();
	if ((state != Row::State::Muted) && (state != Row::State::RaisedHand)) {
		return true;
	}
	if (!rating && static_cast<Row*>(next.get())->raisedHandRating()) {
		return true;
	}
	return false;
}

void MembersController::checkRowPosition(not_null<Row*> row) {
	if (_menu) {
		// Don't reorder rows while we show the popup menu.
		_menuCheckRowsAfterHidden.emplace(row->peer());
		return;
	} else if (!needToReorder(row)) {
		return;
	}

	// Someone started speaking and has a non-speaking row above him.
	// Or someone raised hand and has force muted above him.
	// Or someone was forced muted and had can_unmute_self below him. Sort.
	static constexpr auto kTop = std::numeric_limits<uint64>::max();
	const auto projForAdmin = [&](const PeerListRow &other) {
		const auto &real = static_cast<const Row&>(other);
		return real.speaking()
			// Speaking 'row' to the top, all other speaking below it.
			? (&real == row.get() ? kTop : (kTop - 1))
			: (real.raisedHandRating() > 0)
			// Then all raised hands sorted by rating.
			? real.raisedHandRating()
			: (real.state() == Row::State::Muted)
			// All force muted at the bottom, but 'row' still above others.
			? (&real == row.get() ? 1ULL : 0ULL)
			// All not force-muted lie between raised hands and speaking.
			: (kTop - 2);
	};
	const auto projForOther = [&](const PeerListRow &other) {
		const auto &real = static_cast<const Row&>(other);
		return real.speaking()
			// Speaking 'row' to the top, all other speaking below it.
			? (&real == row.get() ? kTop : (kTop - 1))
			: 0ULL;
	};

	using Comparator = Fn<bool(const PeerListRow&, const PeerListRow&)>;
	const auto makeComparator = [&](const auto &proj) -> Comparator {
		return [&](const PeerListRow &a, const PeerListRow &b) {
			return proj(a) > proj(b);
		};
	};
	delegate()->peerListSortRows(_peer->canManageGroupCall()
		? makeComparator(projForAdmin)
		: makeComparator(projForOther));
}

void MembersController::updateRow(
		not_null<Row*> row,
		const Data::GroupCall::Participant *participant) {
	const auto wasSounding = row->sounding();
	const auto wasSsrc = row->ssrc();
	const auto wasInChat = (row->state() != Row::State::Invited);
	row->setSkipLevelUpdate(_skipRowLevelUpdate);
	row->updateState(participant);
	const auto nowSounding = row->sounding();
	const auto nowSsrc = row->ssrc();

	const auto wasNoSounding = _soundingRowBySsrc.empty();
	if (wasSsrc == nowSsrc) {
		if (nowSounding != wasSounding) {
			if (nowSounding) {
				_soundingRowBySsrc.emplace(nowSsrc, row);
			} else {
				_soundingRowBySsrc.remove(nowSsrc);
			}
		}
	} else {
		_soundingRowBySsrc.remove(wasSsrc);
		if (nowSounding) {
			Assert(nowSsrc != 0);
			_soundingRowBySsrc.emplace(nowSsrc, row);
		}
	}
	const auto nowNoSounding = _soundingRowBySsrc.empty();
	if (wasNoSounding && !nowNoSounding) {
		_soundingAnimation.start();
	} else if (nowNoSounding && !wasNoSounding) {
		_soundingAnimation.stop();
	}

	delegate()->peerListUpdateRow(row);
}

void MembersController::removeRow(not_null<Row*> row) {
	_soundingRowBySsrc.remove(row->ssrc());
	delegate()->peerListRemoveRow(row);
}

void MembersController::updateRowLevel(
		not_null<Row*> row,
		float level) {
	if (_skipRowLevelUpdate) {
		return;
	}
	row->updateLevel(level);
}

Row *MembersController::findRow(not_null<PeerData*> participantPeer) const {
	return static_cast<Row*>(
		delegate()->peerListFindRow(participantPeer->id.value));
}

Main::Session &MembersController::session() const {
	return _call->peer()->session();
}

void MembersController::prepare() {
	delegate()->peerListSetSearchMode(PeerListSearchMode::Disabled);
	//delegate()->peerListSetTitle(std::move(title));
	setDescriptionText(tr::lng_contacts_loading(tr::now));
	setSearchNoResultsText(tr::lng_blocked_list_not_found(tr::now));

	if (const auto real = _call->lookupReal()) {
		prepareRows(real);
	} else if (auto row = createRowForMe()) {
		delegate()->peerListAppendRow(std::move(row));
		delegate()->peerListRefreshRows();
	}

	loadMoreRows();
	appendInvitedUsers();
	_prepared = true;
}

bool MembersController::isMe(not_null<PeerData*> participantPeer) const {
	return (_call->joinAs() == participantPeer);
}

void MembersController::prepareRows(not_null<Data::GroupCall*> real) {
	auto foundMe = false;
	auto changed = false;
	const auto &participants = real->participants();
	auto count = delegate()->peerListFullRowsCount();
	for (auto i = 0; i != count;) {
		auto row = delegate()->peerListRowAt(i);
		auto participantPeer = row->peer();
		if (isMe(participantPeer)) {
			foundMe = true;
			++i;
			continue;
		}
		const auto contains = ranges::contains(
			participants,
			participantPeer,
			&Data::GroupCall::Participant::peer);
		if (contains) {
			++i;
		} else {
			changed = true;
			removeRow(static_cast<Row*>(row.get()));
			--count;
		}
	}
	if (!foundMe) {
		const auto me = _call->joinAs();
		const auto i = ranges::find(
			participants,
			me,
			&Data::GroupCall::Participant::peer);
		auto row = (i != end(participants))
			? createRow(*i)
			: createRowForMe();
		if (row) {
			changed = true;
			delegate()->peerListAppendRow(std::move(row));
		}
	}
	for (const auto &participant : participants) {
		if (auto row = createRow(participant)) {
			changed = true;
			delegate()->peerListAppendRow(std::move(row));
		}
	}
	if (changed) {
		delegate()->peerListRefreshRows();
	}
}

void MembersController::loadMoreRows() {
	if (const auto real = _call->lookupReal()) {
		real->requestParticipants();
	}
}

auto MembersController::toggleMuteRequests() const
-> rpl::producer<MuteRequest> {
	return _toggleMuteRequests.events();
}

auto MembersController::changeVolumeRequests() const
-> rpl::producer<VolumeRequest> {
	return _changeVolumeRequests.events();
}

bool MembersController::rowIsMe(not_null<PeerData*> participantPeer) {
	return isMe(participantPeer);
}

bool MembersController::rowCanMuteMembers() {
	return _peer->canManageGroupCall();
}

void MembersController::rowUpdateRow(not_null<Row*> row) {
	delegate()->peerListUpdateRow(row);
}

void MembersController::rowScheduleRaisedHandStatusRemove(
		not_null<Row*> row) {
	const auto id = row->id();
	const auto when = crl::now() + kKeepRaisedHandStatusDuration;
	const auto i = _raisedHandStatusRemoveAt.find(id);
	if (i != _raisedHandStatusRemoveAt.end()) {
		i->second = when;
	} else {
		_raisedHandStatusRemoveAt.emplace(id, when);
	}
	scheduleRaisedHandStatusRemove();
}

void MembersController::scheduleRaisedHandStatusRemove() {
	auto waiting = crl::time(0);
	const auto now = crl::now();
	for (auto i = begin(_raisedHandStatusRemoveAt)
		; i != end(_raisedHandStatusRemoveAt);) {
		if (i->second <= now) {
			if (const auto row = delegate()->peerListFindRow(i->first)) {
				static_cast<Row*>(row)->clearRaisedHandStatus();
			}
			i = _raisedHandStatusRemoveAt.erase(i);
		} else {
			if (!waiting || waiting > (i->second - now)) {
				waiting = i->second - now;
			}
			++i;
		}
	}
	if (waiting > 0) {
		if (!_raisedHandStatusRemoveTimer.isActive()
			|| _raisedHandStatusRemoveTimer.remainingTime() > waiting) {
			_raisedHandStatusRemoveTimer.callOnce(waiting);
		}
	}
}

void MembersController::rowPaintIcon(
		Painter &p,
		QRect rect,
		IconState state) {
	const auto &greenIcon = st::groupCallMemberColoredCrossLine.icon;
	const auto left = rect.x() + (rect.width() - greenIcon.width()) / 2;
	const auto top = rect.y() + (rect.height() - greenIcon.height()) / 2;
	if (state.speaking == 1. && !state.mutedByMe) {
		// Just green icon, no cross, no coloring.
		greenIcon.paintInCenter(p, rect);
		return;
	} else if (state.speaking == 0.) {
		if (state.active == 1.) {
			// Just gray icon, no cross, no coloring.
			st::groupCallMemberInactiveCrossLine.icon.paintInCenter(p, rect);
			return;
		} else if (state.active == 0.) {
			if (state.muted == 1.) {
				if (state.raisedHand) {
					st::groupCallMemberRaisedHand.paintInCenter(p, rect);
					return;
				}
				// Red crossed icon, colorized once, cached as last frame.
				_coloredCrossLine.paint(
					p,
					left,
					top,
					1.,
					st::groupCallMemberMutedIcon->c);
				return;
			} else if (state.muted == 0.) {
				// Gray crossed icon, no coloring, cached as last frame.
				_inactiveCrossLine.paint(p, left, top, 1.);
				return;
			}
		}
	}
	const auto activeInactiveColor = anim::color(
		st::groupCallMemberInactiveIcon,
		(state.mutedByMe
			? st::groupCallMemberMutedIcon
			: st::groupCallMemberActiveIcon),
		state.speaking);
	const auto iconColor = anim::color(
		activeInactiveColor,
		st::groupCallMemberMutedIcon,
		state.muted);

	// Don't use caching of the last frame,
	// because 'muted' may animate color.
	const auto crossProgress = std::min(1. - state.active, 0.9999);
	_inactiveCrossLine.paint(p, left, top, crossProgress, iconColor);
}

auto MembersController::kickParticipantRequests() const
-> rpl::producer<not_null<PeerData*>>{
	return _kickParticipantRequests.events();
}

void MembersController::rowClicked(not_null<PeerListRow*> row) {
	delegate()->peerListShowRowMenu(row, [=](not_null<Ui::PopupMenu*> menu) {
		if (!_menu || _menu.get() != menu) {
			return;
		}
		auto saved = base::take(_menu);
		for (const auto peer : base::take(_menuCheckRowsAfterHidden)) {
			if (const auto row = findRow(peer)) {
				checkRowPosition(row);
			}
		}
		_menu = std::move(saved);
	});
}

void MembersController::rowActionClicked(
		not_null<PeerListRow*> row) {
	rowClicked(row);
}

base::unique_qptr<Ui::PopupMenu> MembersController::rowContextMenu(
		QWidget *parent,
		not_null<PeerListRow*> row) {
	auto result = createRowContextMenu(parent, row);

	if (result) {
		// First clear _menu value, so that we don't check row positions yet.
		base::take(_menu);

		// Here unique_qptr is used like a shared pointer, where
		// not the last destroyed pointer destroys the object, but the first.
		_menu = base::unique_qptr<Ui::PopupMenu>(result.get());
	}

	return result;
}

base::unique_qptr<Ui::PopupMenu> MembersController::createRowContextMenu(
		QWidget *parent,
		not_null<PeerListRow*> row) {
	const auto participantPeer = row->peer();
	const auto real = static_cast<Row*>(row.get());

	auto result = base::make_unique_q<Ui::PopupMenu>(
		parent,
		st::groupCallPopupMenu);

	const auto muteState = real->state();
	const auto admin = IsGroupCallAdmin(_peer, participantPeer);
	const auto session = &_peer->session();
	const auto getCurrentWindow = [=]() -> Window::SessionController* {
		if (const auto window = Core::App().activeWindow()) {
			if (const auto controller = window->sessionController()) {
				if (&controller->session() == session) {
					return controller;
				}
			}
		}
		return nullptr;
	};
	const auto getWindow = [=] {
		if (const auto current = getCurrentWindow()) {
			return current;
		} else if (&Core::App().domain().active() != &session->account()) {
			Core::App().domain().activate(&session->account());
		}
		return getCurrentWindow();
	};
	const auto performOnMainWindow = [=](auto callback) {
		if (const auto window = getWindow()) {
			if (_menu) {
				_menu->discardParentReActivate();

				// We must hide PopupMenu before we activate the MainWindow,
				// otherwise we set focus in field inside MainWindow and then
				// PopupMenu::hide activates back the group call panel :(
				_menu = nullptr;
			}
			callback(window);
			window->widget()->activate();
		}
	};
	const auto showProfile = [=] {
		performOnMainWindow([=](not_null<Window::SessionController*> window) {
			window->showPeerInfo(participantPeer);
		});
	};
	const auto showHistory = [=] {
		performOnMainWindow([=](not_null<Window::SessionController*> window) {
			window->showPeerHistory(
				participantPeer,
				Window::SectionShow::Way::Forward);
		});
	};
	const auto removeFromVoiceChat = crl::guard(this, [=] {
		_kickParticipantRequests.fire_copy(participantPeer);
	});

	if (real->ssrc() != 0
		&& (!isMe(participantPeer) || _peer->canManageGroupCall())) {
		addMuteActionsToContextMenu(result, participantPeer, admin, real);
	}

	if (isMe(participantPeer)) {
		if (_call->muted() == MuteState::RaisedHand) {
			const auto removeHand = [=] {
				if (_call->muted() == MuteState::RaisedHand) {
					_call->setMutedAndUpdate(MuteState::ForceMuted);
				}
			};
			result->addAction(
				tr::lng_group_call_context_remove_hand(tr::now),
				removeHand);
		}
	} else {
		result->addAction(
			(participantPeer->isUser()
				? tr::lng_context_view_profile(tr::now)
				: participantPeer->isBroadcast()
				? tr::lng_context_view_channel(tr::now)
				: tr::lng_context_view_group(tr::now)),
			showProfile);
		if (participantPeer->isUser()) {
			result->addAction(
				tr::lng_context_send_message(tr::now),
				showHistory);
		}
		const auto canKick = [&] {
			const auto user = participantPeer->asUser();
			if (static_cast<Row*>(row.get())->state()
				== Row::State::Invited) {
				return false;
			} else if (const auto chat = _peer->asChat()) {
				return chat->amCreator()
					|| (user
						&& chat->canBanMembers()
						&& !chat->admins.contains(user));
			} else if (const auto channel = _peer->asChannel()) {
				return channel->canRestrictParticipant(participantPeer);
			}
			return false;
		}();
		if (canKick) {
			result->addAction(MakeAttentionAction(
				result->menu(),
				tr::lng_group_call_context_remove(tr::now),
				removeFromVoiceChat));
		}
	}
	if (result->empty()) {
		return nullptr;
	}
	return result;
}

void MembersController::addMuteActionsToContextMenu(
		not_null<Ui::PopupMenu*> menu,
		not_null<PeerData*> participantPeer,
		bool participantIsCallAdmin,
		not_null<Row*> row) {
	const auto muteString = [=] {
		return (_peer->canManageGroupCall()
			? tr::lng_group_call_context_mute
			: tr::lng_group_call_context_mute_for_me)(tr::now);
	};

	const auto unmuteString = [=] {
		return (_peer->canManageGroupCall()
			? tr::lng_group_call_context_unmute
			: tr::lng_group_call_context_unmute_for_me)(tr::now);
	};

	const auto toggleMute = crl::guard(this, [=](bool mute, bool local) {
		_toggleMuteRequests.fire(Group::MuteRequest{
			.peer = participantPeer,
			.mute = mute,
			.locallyOnly = local,
		});
	});
	const auto changeVolume = crl::guard(this, [=](
			int volume,
			bool local) {
		_changeVolumeRequests.fire(Group::VolumeRequest{
			.peer = participantPeer,
			.volume = std::clamp(volume, 1, Group::kMaxVolume),
			.locallyOnly = local,
		});
	});

	const auto muteState = row->state();
	const auto isMuted = (muteState == Row::State::Muted)
		|| (muteState == Row::State::RaisedHand)
		|| (muteState == Row::State::MutedByMe);

	auto mutesFromVolume = rpl::never<bool>() | rpl::type_erased();

	if (!isMuted || _call->joinAs() == participantPeer) {
		auto otherParticipantStateValue
			= _call->otherParticipantStateValue(
		) | rpl::filter([=](const Group::ParticipantState &data) {
			return data.peer == participantPeer;
		});

		auto volumeItem = base::make_unique_q<MenuVolumeItem>(
			menu->menu(),
			st::groupCallPopupMenu.menu,
			otherParticipantStateValue,
			row->volume(),
			Group::kMaxVolume,
			isMuted);

		mutesFromVolume = volumeItem->toggleMuteRequests();

		volumeItem->toggleMuteRequests(
		) | rpl::start_with_next([=](bool muted) {
			if (muted) {
				// Slider value is changed after the callback is called.
				// To capture good state inside the slider frame we postpone.
				crl::on_main(menu, [=] {
					menu->hideMenu();
				});
			}
			toggleMute(muted, false);
		}, volumeItem->lifetime());

		volumeItem->toggleMuteLocallyRequests(
		) | rpl::start_with_next([=](bool muted) {
			if (!isMe(participantPeer)) {
				toggleMute(muted, true);
			}
		}, volumeItem->lifetime());

		volumeItem->changeVolumeRequests(
		) | rpl::start_with_next([=](int volume) {
			changeVolume(volume, false);
		}, volumeItem->lifetime());

		volumeItem->changeVolumeLocallyRequests(
		) | rpl::start_with_next([=](int volume) {
			if (!isMe(participantPeer)) {
				changeVolume(volume, true);
			}
		}, volumeItem->lifetime());

		menu->addAction(std::move(volumeItem));
	};

	const auto muteAction = [&]() -> QAction* {
		if (muteState == Row::State::Invited
			|| isMe(participantPeer)
			|| (muteState == Row::State::Inactive
				&& participantIsCallAdmin
				&& _peer->canManageGroupCall())
			|| (isMuted
				&& !_peer->canManageGroupCall()
				&& muteState != Row::State::MutedByMe)) {
			return nullptr;
		}
		auto callback = [=] {
			const auto state = row->state();
			const auto muted = (state == Row::State::Muted)
				|| (state == Row::State::RaisedHand)
				|| (state == Row::State::MutedByMe);
			toggleMute(!muted, false);
		};
		return menu->addAction(
			isMuted ? unmuteString() : muteString(),
			std::move(callback));
	}();

	if (muteAction) {
		std::move(
			mutesFromVolume
		) | rpl::start_with_next([=](bool muted) {
			muteAction->setText(muted ? unmuteString() : muteString());
		}, menu->lifetime());
	}
}

std::unique_ptr<Row> MembersController::createRowForMe() {
	auto result = std::make_unique<Row>(this, _call->joinAs());
	updateRow(result.get(), nullptr);
	return result;
}

std::unique_ptr<Row> MembersController::createRow(
		const Data::GroupCall::Participant &participant) {
	auto result = std::make_unique<Row>(this, participant.peer);
	updateRow(result.get(), &participant);
	return result;
}

std::unique_ptr<Row> MembersController::createInvitedRow(
		not_null<PeerData*> participantPeer) {
	if (findRow(participantPeer)) {
		return nullptr;
	}
	auto result = std::make_unique<Row>(this, participantPeer);
	updateRow(result.get(), nullptr);
	return result;
}

} // namespace

Members::Members(
	not_null<QWidget*> parent,
	not_null<GroupCall*> call)
: RpWidget(parent)
, _call(call)
, _scroll(this, st::defaultSolidScroll)
, _listController(std::make_unique<MembersController>(call, parent)) {
	setupAddMember(call);
	setupList();
	setContent(_list);
	setupFakeRoundCorners();
	_listController->setDelegate(static_cast<PeerListDelegate*>(this));
}

auto Members::toggleMuteRequests() const
-> rpl::producer<Group::MuteRequest> {
	return static_cast<MembersController*>(
		_listController.get())->toggleMuteRequests();
}

auto Members::changeVolumeRequests() const
-> rpl::producer<Group::VolumeRequest> {
	return static_cast<MembersController*>(
		_listController.get())->changeVolumeRequests();
}

auto Members::kickParticipantRequests() const
-> rpl::producer<not_null<PeerData*>> {
	return static_cast<MembersController*>(
		_listController.get())->kickParticipantRequests();
}

int Members::desiredHeight() const {
	const auto top = _addMember ? _addMember->height() : 0;
	auto count = [&] {
		if (const auto real = _call->lookupReal()) {
			return real->fullCount();
		}
		return 0;
	}();
	const auto use = std::max(count, _list->fullRowsCount());
	return top
		+ (use * st::groupCallMembersList.item.height)
		+ (use ? st::lineWidth : 0);
}

rpl::producer<int> Members::desiredHeightValue() const {
	const auto controller = static_cast<MembersController*>(
		_listController.get());
	return rpl::combine(
		heightValue(),
		_addMemberButton.value(),
		controller->fullCountValue()
	) | rpl::map([=] {
		return desiredHeight();
	});
}

void Members::setupAddMember(not_null<GroupCall*> call) {
	using namespace rpl::mappers;

	const auto peer = call->peer();
	if (const auto channel = peer->asBroadcast()) {
		_canAddMembers = rpl::single(
			false
		) | rpl::then(_call->real(
		) | rpl::map([=] {
			return Data::PeerFlagValue(
				channel,
				MTPDchannel::Flag::f_username);
		}) | rpl::flatten_latest());
	} else {
		_canAddMembers = Data::CanWriteValue(peer.get());
		SubscribeToMigration(
			peer,
			lifetime(),
			[=](not_null<ChannelData*> channel) {
				_canAddMembers = Data::CanWriteValue(channel.get());
			});
	}

	_canAddMembers.value(
	) | rpl::start_with_next([=](bool can) {
		if (!can) {
			_addMemberButton = nullptr;
			_addMember.destroy();
			updateControlsGeometry();
			return;
		}
		_addMember = Settings::CreateButton(
			this,
			tr::lng_group_call_invite(),
			st::groupCallAddMember,
			&st::groupCallAddMemberIcon,
			st::groupCallAddMemberIconLeft);
		_addMember->show();

		_addMember->addClickHandler([=] { // TODO throttle(ripple duration)
			_addMemberRequests.fire({});
		});
		_addMemberButton = _addMember.data();

		resizeToList();
	}, lifetime());
}

rpl::producer<int> Members::fullCountValue() const {
	return static_cast<MembersController*>(
		_listController.get())->fullCountValue();
}

void Members::setupList() {
	_listController->setStyleOverrides(&st::groupCallMembersList);
	_list = _scroll->setOwnedWidget(object_ptr<ListWidget>(
		this,
		_listController.get()));

	_list->heightValue(
	) | rpl::start_with_next([=] {
		resizeToList();
	}, _list->lifetime());

	rpl::combine(
		_scroll->scrollTopValue(),
		_scroll->heightValue()
	) | rpl::start_with_next([=](int scrollTop, int scrollHeight) {
		_list->setVisibleTopBottom(scrollTop, scrollTop + scrollHeight);
	}, _scroll->lifetime());

	updateControlsGeometry();
}

void Members::resizeEvent(QResizeEvent *e) {
	updateControlsGeometry();
}

void Members::resizeToList() {
	if (!_list) {
		return;
	}
	const auto listHeight = _list->height();
	const auto newHeight = (listHeight > 0)
		? ((_addMember ? _addMember->height() : 0)
			+ listHeight
			+ st::lineWidth)
		: 0;
	if (height() == newHeight) {
		updateControlsGeometry();
	} else {
		resize(width(), newHeight);
	}
}

void Members::updateControlsGeometry() {
	if (!_list) {
		return;
	}
	auto topSkip = 0;
	if (_addMember) {
		_addMember->resizeToWidth(width());
		_addMember->move(0, 0);
		topSkip = _addMember->height();
	}
	_scroll->setGeometry(0, topSkip, width(), height() - topSkip);
	_list->resizeToWidth(width());
}

void Members::setupFakeRoundCorners() {
	const auto size = st::roundRadiusLarge;
	const auto full = 3 * size;
	const auto imagePartSize = size * cIntRetinaFactor();
	const auto imageSize = full * cIntRetinaFactor();
	const auto image = std::make_shared<QImage>(
		QImage(imageSize, imageSize, QImage::Format_ARGB32_Premultiplied));
	image->setDevicePixelRatio(cRetinaFactor());

	const auto refreshImage = [=] {
		image->fill(st::groupCallBg->c);
		{
			QPainter p(image.get());
			PainterHighQualityEnabler hq(p);
			p.setCompositionMode(QPainter::CompositionMode_Source);
			p.setPen(Qt::NoPen);
			p.setBrush(Qt::transparent);
			p.drawRoundedRect(0, 0, full, full, size, size);
		}
	};

	const auto create = [&](QPoint imagePartOrigin) {
		const auto result = Ui::CreateChild<Ui::RpWidget>(this);
		result->show();
		result->resize(size, size);
		result->setAttribute(Qt::WA_TransparentForMouseEvents);
		result->paintRequest(
		) | rpl::start_with_next([=] {
			QPainter(result).drawImage(
				result->rect(),
				*image,
				QRect(imagePartOrigin, QSize(imagePartSize, imagePartSize)));
		}, result->lifetime());
		result->raise();
		return result;
	};
	const auto shift = imageSize - imagePartSize;
	const auto topleft = create({ 0, 0 });
	const auto topright = create({ shift, 0 });
	const auto bottomleft = create({ 0, shift });
	const auto bottomright = create({ shift, shift });

	sizeValue(
	) | rpl::start_with_next([=](QSize size) {
		topleft->move(0, 0);
		topright->move(size.width() - topright->width(), 0);
		bottomleft->move(0, size.height() - bottomleft->height());
		bottomright->move(
			size.width() - bottomright->width(),
			size.height() - bottomright->height());
	}, lifetime());

	refreshImage();
	style::PaletteChanged(
	) | rpl::start_with_next([=] {
		refreshImage();
		topleft->update();
		topright->update();
		bottomleft->update();
		bottomright->update();
	}, lifetime());
}

void Members::peerListSetTitle(rpl::producer<QString> title) {
}

void Members::peerListSetAdditionalTitle(rpl::producer<QString> title) {
}

void Members::peerListSetHideEmpty(bool hide) {
}

bool Members::peerListIsRowChecked(not_null<PeerListRow*> row) {
	return false;
}

void Members::peerListScrollToTop() {
}

int Members::peerListSelectedRowsCount() {
	return 0;
}

void Members::peerListAddSelectedPeerInBunch(not_null<PeerData*> peer) {
	Unexpected("Item selection in Calls::Members.");
}

void Members::peerListAddSelectedRowInBunch(not_null<PeerListRow*> row) {
	Unexpected("Item selection in Calls::Members.");
}

void Members::peerListFinishSelectedRowsBunch() {
}

void Members::peerListSetDescription(
		object_ptr<Ui::FlatLabel> description) {
	description.destroy();
}

} // namespace Calls::Group
