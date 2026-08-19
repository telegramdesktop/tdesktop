/*
This file is part of Telegram Desktop,
the official desktop application for the Telegram messaging service.

For license and copyright information please follow this link:
https://github.com/telegramdesktop/tdesktop/blob/master/LEGAL
*/
#pragma once

#include "base/object_ptr.h"
#include "base/unique_qptr.h"
#include "data/data_star_gift.h"
#include "ui/effects/animations.h"
#include "ui/effects/radial_animation.h"
#include "lottie/lottie_icon.h"
#include "ui/text/text_custom_emoji.h"

namespace ChatHelpers {
class Show;
} // namespace ChatHelpers

namespace Main {
class Session;
} // namespace Main

namespace HistoryView {
class StickerPlayer;
} // namespace HistoryView

namespace Info::PeerGifts {
class GiftButton;
} // namespace Info::PeerGifts

namespace Ui {

class FlatLabel;
class GenericBox;
class RpWidget;
class VerticalLayout;
class UniqueGiftCoverWidget;

struct CraftResultError {
	QString type;
};

struct CraftResultWait {
	TimeId seconds = 0;
};

using CraftResult = std::variant<
	std::shared_ptr<Data::GiftUpgradeResult>,
	CraftResultError,
	CraftResultWait>;

struct BackdropView {
	Data::UniqueGiftBackdrop colors;
	QImage gradient;
};

struct PatternView {
	std::unique_ptr<Text::CustomEmoji> emoji;
	base::flat_map<int, base::flat_map<float64, QImage>> emojis;
};

struct CraftState {
	struct Cover {
		BackdropView backdrop;
		PatternView pattern;
		QColor button1;
		QColor button2;
		Animations::Simple shownAnimation;
		bool shown = false;
	};

	std::array<Cover, 5> covers; // Last one for failed background.
	rpl::variable<QColor> edgeColor;
	QColor button1;
	QColor button2;
	bool coversAnimate = false;
	bool craftingStarted = false;
	QImage craftBg;

	QImage topPart;
	QRect topPartRect;
	QImage bottomPart;
	int bottomPartY = 0;

	struct CornerSnapshot {
		base::unique_qptr<Info::PeerGifts::GiftButton> giftButton;
		mutable QImage giftFrame;
		QImage percentBadge;
		QImage removeButton;
		QImage addButton;
		QRect originalRect;
		mutable bool giftFrameFinal = false;

		[[nodiscard]] QImage gift(float64 progress) const;
	};
	std::array<CornerSnapshot, 4> corners;

	QRect forgeRect;

	QColor forgeBgOverlay;
	QColor forgeBg1;
	QColor forgeBg2;
	QColor forgeFail;
	QImage forgePercent;

	struct EmptySide {
		QColor bg;
		QImage frame;
	};
	std::array<EmptySide, 6> forgeSides;

	Main::Session *session = nullptr;

	int containerHeight = 0;
	int craftingTop = 0;
	int craftingBottom = 0;
	int craftingAreaCenterY = 0;
	int originalButtonY = 0;

	QString giftName;
	int successPermille = 0;
	struct LostGift {
		int cornerIndex = -1;
		QString number;
		mutable QImage badgeCache;
	};
	std::vector<LostGift> lostGifts;
	QImage lostRadial;

	void paint(
		QPainter &p,
		QSize size,
		int craftingHeight,
		float64 slideProgress = 0.);
	void updateForGiftCount(int count, Fn<void()> repaint);
	[[nodiscard]] EmptySide prepareEmptySide(int index) const;
};

struct FacePlacement {
	int face = -1;
	int rotation = 0;
};

struct CraftDoneAnimation {
	object_ptr<UniqueGiftCoverWidget> owned;
	UniqueGiftCoverWidget *widget = nullptr;
	Animations::Simple shownAnimation;
	Animations::Simple heightAnimation;
	QImage frame;
	int coverHeight = 0;
	int coverShift = 0;
	float64 expanded = 0.;
	bool finished = false;
	bool hiding = false;
};

struct CraftFailAnimation {
	QImage frame;
	std::unique_ptr<Lottie::Icon> lottie;
	bool started = false;
	int finalCoverIndex = -1;
	rpl::lifetime lifetime;
};

struct CraftAnimationState {
	std::shared_ptr<CraftState> shared;

	Animations::Simple slideOutAnimation;
	Animations::Basic continuousAnimation;

	float64 rotationX = 0.;
	float64 rotationY = 0.;

	int currentlyFlying = -1;
	int giftsLanded = 0;
	int totalGifts = 0;
	bool allGiftsLanded = false;
	bool currentPhaseFinished = false;
	std::array<FacePlacement, 4> giftToSide;
	Animations::Simple flightAnimation;

	int currentPhaseIndex = -1;
	crl::time animationStartTime = 0;
	crl::time animationDuration = 0;
	float64 initialRotationX = 0.;
	float64 initialRotationY = 0.;
	int nextFaceIndex = 0;
	int nextFaceRotation = 0;
	bool nextFaceRevealed = false;

	std::optional<std::array<QPointF, 4>> flightTargetCorners;

	std::optional<std::shared_ptr<Data::GiftUpgradeResult>> craftResult;
	std::unique_ptr<InfiniteRadialAnimation> loadingAnimation;
	Animations::Simple loadingShownAnimation;
	crl::time loadingStartedTime = 0;
	bool loadingFadingOut = false;

	std::unique_ptr<CraftDoneAnimation> successAnimation;
	std::unique_ptr<CraftFailAnimation> failureAnimation;

	rpl::variable<float64> progressOpacity;
	Animations::Simple progressFadeIn;
	rpl::variable<bool> progressShown;

	rpl::variable<float64> failureOpacity;
	Animations::Simple failureFadeIn;
	rpl::variable<bool> failureShown;

	Fn<void()> retryWithNewGift;
};

void StartCraftAnimation(
	not_null<GenericBox*> box,
	std::shared_ptr<ChatHelpers::Show> show,
	std::shared_ptr<CraftState> state,
	Fn<void(Fn<void(CraftResult)> callback)> startRequest,
	Fn<void(Fn<void()> closeCurrent)> retryWithNewGift);

} // namespace Ui
