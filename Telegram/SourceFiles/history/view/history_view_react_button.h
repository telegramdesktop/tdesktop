/*
This file is part of Telegram Desktop,
the official desktop application for the Telegram messaging service.

For license and copyright information please follow this link:
https://github.com/telegramdesktop/tdesktop/blob/master/LEGAL
*/
#pragma once

#include "ui/effects/animations.h"
#include "ui/widgets/scroll_area.h"
#include "ui/chat/chat_style.h"

namespace Ui {
struct ChatPaintContext;
class PopupMenu;
} // namespace Ui

namespace Data {
struct Reaction;
class DocumentMedia;
} // namespace Data

namespace HistoryView {
using PaintContext = Ui::ChatPaintContext;
struct TextState;
} // namespace HistoryView

namespace Main {
class Session;
} // namespace Main

namespace Lottie {
class Icon;
} // namespace Lottie

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
		Fn<void()> hideMe);
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

using IconFactory = Fn<std::shared_ptr<Lottie::Icon>(
	not_null<Data::DocumentMedia*>,
	int)>;

class Manager final : public base::has_weak_ptr {
public:
	Manager(
		QWidget *wheelEventsTarget,
		rpl::producer<int> uniqueLimitValue,
		Fn<void(QRect)> buttonUpdate,
		IconFactory iconFactory);
	~Manager();

	using AllowedSublist = std::optional<base::flat_set<QString>>;

	void applyList(
		const std::vector<Data::Reaction> &list,
		const QString &favorite);
	void updateAllowedSublist(AllowedSublist filter);
	[[nodiscard]] const AllowedSublist &allowedSublist() const;
	void updateUniqueLimit(not_null<HistoryItem*> item);

	void updateButton(ButtonParameters parameters);
	void paint(Painter &p, const PaintContext &context);
	[[nodiscard]] TextState buttonTextState(QPoint position) const;
	void remove(FullMsgId context);

	[[nodiscard]] bool consumeWheelEvent(not_null<QWheelEvent*> e);

	struct Chosen {
		FullMsgId context;
		QString emoji;
		std::shared_ptr<Lottie::Icon> icon;
		QRect geometry;

		explicit operator bool() const {
			return context && !emoji.isNull();
		}
	};
	[[nodiscard]] rpl::producer<Chosen> chosen() const {
		return _chosen.events();
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
		const QString &favorite);
	[[nodiscard]] rpl::producer<QString> faveRequests() const;

	[[nodiscard]] rpl::lifetime &lifetime() {
		return _lifetime;
	}

private:
	struct ReactionDocument {
		std::shared_ptr<Data::DocumentMedia> media;
		std::shared_ptr<Lottie::Icon> icon;
	};
	struct ReactionIcons {
		QString emoji;
		not_null<DocumentData*> appearAnimation;
		not_null<DocumentData*> selectAnimation;
		std::shared_ptr<Lottie::Icon> appear;
		std::shared_ptr<Lottie::Icon> select;
		mutable ClickHandlerPtr link;
		mutable Ui::Animations::Simple selectedScale;
		bool appearAnimated = false;
		mutable bool selected = false;
		mutable bool selectAnimated = false;
	};
	struct OverlayImage {
		not_null<QImage*> cache;
		QRect source;
	};
	static constexpr auto kFramesCount = 32;

	void applyListFilters();
	void showButtonDelayed();
	void stealWheelEvents(not_null<QWidget*> target);

	[[nodiscard]] Chosen lookupChosen(const QString &emoji) const;
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
	void paintAllEmoji(
		Painter &p,
		not_null<Button*> button,
		int scroll,
		float64 scale,
		QPoint position,
		QPoint mainEmojiPosition);
	void paintInnerGradients(
		Painter &p,
		const QColor &background,
		not_null<Button*> button,
		int scroll,
		float64 expandRatio);
	void overlayExpandedBorder(
		Painter &p,
		QSize size,
		float64 expandRatio,
		float64 scale,
		const QColor &shadow);
	void paintLongImage(
		QPainter &p,
		QRect geometry,
		const QImage &image,
		QRect source);

	void resolveMainReactionIcon();
	void setMainReactionIcon();
	void clearAppearAnimations();
	[[nodiscard]] QRect cacheRect(int frameIndex, int columnIndex) const;
	[[nodiscard]] QRect overlayCacheRect(
		int frameIndex,
		int columnIndex) const;
	QRect validateShadow(
		int frameIndex,
		float64 scale,
		const QColor &shadow);
	QRect validateEmoji(int frameIndex, float64 scale);
	QRect validateFrame(
		int frameIndex,
		float64 scale,
		const QColor &background,
		const QColor &shadow);
	OverlayImage validateOverlayMask(
		int frameIndex,
		QSize innerSize,
		float64 radius,
		float64 scale);
	OverlayImage validateOverlayShadow(
		int frameIndex,
		QSize innerSize,
		float64 radius,
		float64 scale,
		const QColor &shadow,
		const OverlayImage &mask);
	void setBackgroundColor(const QColor &background);
	void setShadowColor(const QColor &shadow);

	void setSelectedIcon(int index) const;
	void clearStateForHidden(ReactionIcons &icon);
	void clearStateForSelectFinished(ReactionIcons &icon);

	[[nodiscard]] QMargins innerMargins() const;
	[[nodiscard]] QRect buttonInner() const;
	[[nodiscard]] QRect buttonInner(not_null<Button*> button) const;

	[[nodiscard]] ClickHandlerPtr computeButtonLink(QPoint position) const;
	[[nodiscard]] ClickHandlerPtr resolveButtonLink(
		const ReactionIcons &reaction) const;

	void updateCurrentButton() const;
	[[nodiscard]] bool onlyMainEmojiVisible() const;
	[[nodiscard]] bool checkIconLoaded(ReactionDocument &entry) const;
	void loadIcons();
	void checkIcons();

	const IconFactory _iconFactory;
	rpl::event_stream<Chosen> _chosen;
	std::vector<ReactionIcons> _list;
	QString _favorite;
	AllowedSublist _filter;
	QSize _outer;
	QRect _inner;
	QSize _overlayFull;
	QImage _cacheBg;
	QImage _cacheParts;
	QImage _overlayCacheParts;
	QImage _overlayMaskScaled;
	QImage _overlayShadowScaled;
	QImage _shadowBuffer;
	QImage _expandedBuffer;
	QImage _topGradient;
	QImage _bottomGradient;
	std::array<bool, kFramesCount> _validBg = { { false } };
	std::array<bool, kFramesCount> _validShadow = { { false } };
	std::array<bool, kFramesCount> _validEmoji = { { false } };
	std::array<bool, kFramesCount> _validOverlayMask = { { false } };
	std::array<bool, kFramesCount> _validOverlayShadow = { { false } };
	QColor _background;
	QColor _gradient;
	QColor _shadow;

	std::shared_ptr<Data::DocumentMedia> _mainReactionMedia;
	std::shared_ptr<Lottie::Icon> _mainReactionIcon;
	QImage _mainReactionImage;
	rpl::lifetime _mainReactionLifetime;

	rpl::variable<int> _uniqueLimit = 0;
	base::flat_map<not_null<DocumentData*>, ReactionDocument> _loadCache;
	std::vector<not_null<ReactionIcons*>> _icons;
	rpl::lifetime _loadCacheLifetime;
	bool _showingAll = false;
	mutable int _selectedIcon = -1;

	std::optional<ButtonParameters> _scheduledParameters;
	base::Timer _buttonShowTimer;
	const Fn<void(QRect)> _buttonUpdate;
	std::unique_ptr<Button> _button;
	std::vector<std::unique_ptr<Button>> _buttonHiding;
	FullMsgId _buttonContext;
	base::flat_set<QString> _buttonAlreadyList;
	int _buttonAlreadyNotMineCount = 0;
	mutable base::flat_map<QString, ClickHandlerPtr> _reactionsLinks;
	Fn<Fn<void()>(QString)> _createChooseCallback;

	base::flat_map<FullMsgId, QRect> _activeEffectAreas;

	Ui::ReactionPaintInfo _currentReactionInfo;
	base::flat_map<FullMsgId, Ui::ReactionPaintInfo> _collectedEffects;

	base::unique_qptr<Ui::PopupMenu> _menu;
	rpl::event_stream<QString> _faveRequests;

	rpl::lifetime _lifetime;

};

class CachedIconFactory final {
public:
	CachedIconFactory() = default;
	CachedIconFactory(const CachedIconFactory &other) = delete;
	CachedIconFactory &operator=(const CachedIconFactory &other) = delete;

	[[nodiscard]] IconFactory createMethod();

private:
	base::flat_map<
		std::shared_ptr<Data::DocumentMedia>,
		std::shared_ptr<Lottie::Icon>> _cache;

};

void SetupManagerList(
	not_null<Manager*> manager,
	not_null<Main::Session*> session,
	rpl::producer<Manager::AllowedSublist> filter);

[[nodiscard]] std::shared_ptr<Lottie::Icon> DefaultIconFactory(
	not_null<Data::DocumentMedia*> media,
	int size);

} // namespace HistoryView
