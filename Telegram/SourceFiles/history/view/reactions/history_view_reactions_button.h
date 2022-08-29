/*
This file is part of Telegram Desktop,
the official desktop application for the Telegram messaging service.

For license and copyright information please follow this link:
https://github.com/telegramdesktop/tdesktop/blob/master/LEGAL
*/
#pragma once

#include "base/timer.h"
#include "base/unique_qptr.h"
#include "ui/effects/animations.h"
#include "ui/effects/round_area_with_shadow.h"
#include "history/view/reactions/history_view_reactions_strip.h"
#include "ui/chat/chat_style.h" // Ui::ReactionPaintInfo

namespace Ui {
struct ChatPaintContext;
struct ReactionPaintInfo;
class PopupMenu;
} // namespace Ui

namespace Data {
struct ReactionId;
struct Reaction;
struct PossibleItemReactionsRef;
class DocumentMedia;
} // namespace Data

namespace HistoryView {
using PaintContext = Ui::ChatPaintContext;
struct TextState;
} // namespace HistoryView

namespace Main {
class Session;
} // namespace Main

namespace HistoryView::Reactions {

enum class ExpandDirection {
	Up,
	Down,
};

struct ButtonParameters {
	[[nodiscard]] ButtonParameters translated(QPoint delta) const {
		auto result = *this;
		result.center += delta;
		result.pointer += delta;
		return result;
	}

	FullMsgId context;
	QPoint center;
	QPoint pointer;
	QPoint globalPointer;
	int reactionsCount = 1;
	int visibleTop = 0;
	int visibleBottom = 0;
	bool outside = false;
	bool cursorLeft = false;
};

enum class ButtonState {
	Hidden,
	Shown,
	Active,
	Inside,
};

class Button final {
public:
	Button(
		Fn<void(QRect)> update,
		ButtonParameters parameters,
		Fn<void()> hide);
	~Button();

	void applyParameters(ButtonParameters parameters);

	using State = ButtonState;
	void applyState(State state);

	[[nodiscard]] bool expandUp() const;
	[[nodiscard]] bool isHidden() const;
	[[nodiscard]] QRect geometry() const;
	[[nodiscard]] int expandedHeight() const;
	[[nodiscard]] int scroll() const;
	[[nodiscard]] int scrollMax() const;
	[[nodiscard]] float64 currentScale() const;
	[[nodiscard]] float64 currentOpacity() const;
	[[nodiscard]] float64 expandAnimationOpacity(float64 expandRatio) const;
	[[nodiscard]] int expandAnimationScroll(float64 expandRatio) const;
	[[nodiscard]] bool consumeWheelEvent(not_null<QWheelEvent*> e);

	[[nodiscard]] static float64 ScaleForState(State state);
	[[nodiscard]] static float64 OpacityForScale(float64 scale);

private:
	enum class CollapseType {
		Scroll,
		Fade,
	};

	void updateGeometry(Fn<void(QRect)> update);
	void applyState(State satte, Fn<void(QRect)> update);
	void applyParameters(
		ButtonParameters parameters,
		Fn<void(QRect)> update);
	void updateExpandDirection(const ButtonParameters &parameters);

	const Fn<void(QRect)> _update;

	State _state = State::Hidden;
	float64 _finalScale = 0.;
	Ui::Animations::Simple _scaleAnimation;
	Ui::Animations::Simple _opacityAnimation;
	Ui::Animations::Simple _heightAnimation;

	QRect _collapsed;
	QRect _geometry;
	int _expandedInnerHeight = 0;
	int _expandedHeight = 0;
	int _finalHeight = 0;
	int _scroll = 0;
	ExpandDirection _expandDirection = ExpandDirection::Up;
	CollapseType _collapseType = CollapseType::Scroll;

	base::Timer _expandTimer;
	base::Timer _hideTimer;
	std::optional<QPoint> _lastGlobalPosition;

};

class Manager final : public base::has_weak_ptr {
public:
	Manager(
		QWidget *wheelEventsTarget,
		Fn<void(QRect)> buttonUpdate,
		IconFactory iconFactory);
	~Manager();

	using ReactionId = ::Data::ReactionId;

	void applyList(const Data::PossibleItemReactionsRef &reactions);

	void updateButton(ButtonParameters parameters);
	void paint(Painter &p, const PaintContext &context);
	[[nodiscard]] TextState buttonTextState(QPoint position) const;
	void remove(FullMsgId context);

	[[nodiscard]] bool consumeWheelEvent(not_null<QWheelEvent*> e);

	[[nodiscard]] rpl::producer<ChosenReaction> chosen() const {
		return _chosen.events();
	}
	[[nodiscard]] rpl::producer<FullMsgId> premiumPromoChosen() const {
		return _premiumPromoChosen.events();
	}
	[[nodiscard]] rpl::producer<FullMsgId> expandChosen() const {
		return _expandChosen.events();
	}

	[[nodiscard]] std::optional<QRect> lookupEffectArea(
		FullMsgId itemId) const;
	void startEffectsCollection();
	[[nodiscard]] auto currentReactionPaintInfo()
		-> not_null<Ui::ReactionPaintInfo*>;
	void recordCurrentReactionEffect(FullMsgId itemId, QPoint origin);

	bool showContextMenu(
		QWidget *parent,
		QContextMenuEvent *e,
		const ReactionId &favorite);
	[[nodiscard]] rpl::producer<ReactionId> faveRequests() const;

	[[nodiscard]] rpl::lifetime &lifetime() {
		return _lifetime;
	}

private:
	void showButtonDelayed();
	void stealWheelEvents(not_null<QWidget*> target);

	[[nodiscard]] ChosenReaction lookupChosen(const ReactionId &id) const;
	[[nodiscard]] bool overCurrentButton(QPoint position) const;

	void removeStaleButtons();
	void paintButton(
		Painter &p,
		const PaintContext &context,
		not_null<Button*> button);
	void paintButton(
		Painter &p,
		const PaintContext &context,
		not_null<Button*> button,
		int frame,
		float64 scale);
	void paintInnerGradients(
		Painter &p,
		const QColor &background,
		not_null<Button*> button,
		int scroll,
		float64 expandRatio);

	void clearAppearAnimations();
	[[nodiscard]] QRect cacheRect(int frameIndex, int columnIndex) const;

	[[nodiscard]] QMargins innerMargins() const;
	[[nodiscard]] QRect buttonInner() const;
	[[nodiscard]] QRect buttonInner(not_null<Button*> button) const;

	[[nodiscard]] ClickHandlerPtr computeButtonLink(QPoint position) const;
	[[nodiscard]] ClickHandlerPtr resolveButtonLink(
		const ReactionId &id) const;

	void updateCurrentButton() const;

	QSize _outer;
	QRect _inner;
	Strip _strip;
	Ui::RoundAreaWithShadow _cachedRound;
	QImage _expandedBuffer;
	QColor _gradientBackground;
	QImage _topGradient;
	QImage _bottomGradient;
	QColor _gradient;

	rpl::event_stream<ChosenReaction> _chosen;
	rpl::event_stream<FullMsgId> _premiumPromoChosen;
	rpl::event_stream<FullMsgId> _expandChosen;
	mutable base::flat_map<ReactionId, ClickHandlerPtr> _links;
	mutable ClickHandlerPtr _premiumPromoLink;
	mutable ClickHandlerPtr _expandLink;

	rpl::variable<int> _uniqueLimit = 0;
	bool _showingAll = false;

	std::optional<ButtonParameters> _scheduledParameters;
	base::Timer _buttonShowTimer;
	const Fn<void(QRect)> _buttonUpdate;
	std::unique_ptr<Button> _button;
	std::vector<std::unique_ptr<Button>> _buttonHiding;
	FullMsgId _buttonContext;
	mutable base::flat_map<ReactionId, ClickHandlerPtr> _reactionsLinks;
	Fn<Fn<void()>(ReactionId)> _createChooseCallback;

	base::flat_map<FullMsgId, QRect> _activeEffectAreas;

	Ui::ReactionPaintInfo _currentReactionInfo;
	base::flat_map<FullMsgId, Ui::ReactionPaintInfo> _collectedEffects;

	base::unique_qptr<Ui::PopupMenu> _menu;
	rpl::event_stream<ReactionId> _faveRequests;

	rpl::lifetime _lifetime;

};

void SetupManagerList(
	not_null<Manager*> manager,
	rpl::producer<HistoryItem*> items);

} // namespace HistoryView
