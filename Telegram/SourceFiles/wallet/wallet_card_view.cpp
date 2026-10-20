/*
This file is part of Telegram Desktop,
the official desktop application for the Telegram messaging service.

For license and copyright information please follow this link:
https://github.com/telegramdesktop/tdesktop/blob/master/LEGAL
*/
#include "wallet/wallet_card_view.h"

#include "data/data_changes.h"
#include "data/data_session.h"
#include "data/data_user.h"
#include "data/data_user_wallet_card.h"
#include "info/profile/info_profile_balance_pill.h"
#include "info/profile/info_profile_values.h"
#include "lang/lang_keys.h"
#include "main/main_app_config.h"
#include "main/main_session.h"
#include "ui/controls/ton_common.h"
#include "ui/effects/layer_backdrop.h"
#include "ui/layers/layer_widget.h"
#include "ui/text/text_utilities.h"
#include "ui/widgets/buttons.h"
#include "ui/widgets/labels.h"
#include "ui/painter.h"
#include "ui/ui_utility.h"
#include "wallet/wallet_address.h"
#include "wallet/wallet_card_flight.h"
#include "wallet/wallet_card_look.h"
#include "wallet/wallet_card_stickers.h"
#include "wallet/wallet_card_tint.h"
#include "wallet/wallet_card_view_motion.h"
#include "wallet/wallet_content.h"
#include "wallet/wallet_fiat.h"
#include "wallet/wallet_panel.h"
#include "wallet/wallet_session.h"
#include "wallet/wallet_user_addresses.h"
#include "window/window_session_controller.h"

#include "styles/style_wallet.h"

namespace Wallet {
namespace {

constexpr auto kButtonFillLevel = 0.5;
constexpr auto kButtonRippleLevel = 1.;
constexpr auto kButtonRise = 0.0375;
constexpr auto kButtonScale = 0.82;
constexpr auto kInkSteps = 255;
constexpr auto kPreferredContrast = 4.6;
constexpr auto kRequiredContrast = 4.5;
constexpr auto kPlateLevels = std::array{ 0., 0.25, 0.5, 0.75, 1. };
constexpr auto kSlotFadeStops = std::array{ 0., 0.5, 1. };

using Controllers = base::flat_set<
	not_null<Window::SessionController*>>;

enum class Phase : uchar {
	Preparing,
	Opening,
	Shown,
	Closing,
};

enum class ButtonMode : uchar {
	None,
	HowItWorks,
	SendMoney,
	OpenWallet,
};

struct LabelInk {
	QColor color;
	float64 plate = 0.;
};

struct OriginObservation {
	QPointer<QWidget> origin;
	QPointer<QWidget> parent;
	QPointer<QWidget> window;
	QPointer<QWidget> underlying;
	QRect geometry;
	QRegion clipped;
	QSize size;
	int ratio = 1;
	uint64 appearance = 0;
	bool present = false;
	bool sameWindow = false;
	bool visible = false;
	bool actionable = false;
	bool special = false;

	friend bool operator==(
		const OriginObservation &,
		const OriginObservation &) = default;
};

struct Scene {
	QImage sharp;
	QImage empty;
	std::optional<QRect> slot;
	QRect rest;
	OriginObservation origin;
};

struct Parts {
	QImage card;
	QImage button;
};

struct Placement {
	QRect button;
	QRect label;
	int cardWidth = 0;
};

struct Foreground {
	Parts parts;
	CardViewPlaces places;
	Placement placement;
	QString text;
	QColor ink;
	QColor plate;
};

class CardView final : public Ui::LayerWidget {
public:
	CardView(
		not_null<Window::SessionController*> controller,
		not_null<UserData*> user,
		not_null<Info::Profile::BalancePill*> origin);
	~CardView();

	void parentResized() override;
	void showFinished() override;
	bool closeByOutsideClick() const override;
	bool closeByBackButton() override;

protected:
	int resizeGetHeight(int newWidth) override;
	void paintEvent(QPaintEvent *e) override;
	void keyPressEvent(QKeyEvent *e) override;
	void mousePressEvent(QMouseEvent *e) override;

private:
	void closeHook() override;

	[[nodiscard]] Ui::LayerStackWidget *stack() const;
	[[nodiscard]] bool covered() const;
	[[nodiscard]] bool onlyLayer(
		not_null<Ui::LayerStackWidget*> stack) const;
	[[nodiscard]] Ui::LayerWidget *underlyingLayer() const;
	[[nodiscard]] QImage captureScene();
	[[nodiscard]] QImage captureSharp(Scene &scene, uint64 &revision);
	[[nodiscard]] OriginObservation observeOrigin() const;
	[[nodiscard]] bool exposed() const;
	[[nodiscard]] Foreground foreground() const;
	[[nodiscard]] CardViewPlaces places() const;
	[[nodiscard]] Parts grabParts() const;
	[[nodiscard]] bool holding() const;
	[[nodiscard]] ButtonMode computeButtonMode() const;
	[[nodiscard]] QRect labelPlate() const;
	[[nodiscard]] QImage sampleLabelBackground(float64 plate) const;
	[[nodiscard]] LabelInk computeLabelInk() const;
	[[nodiscard]] bool tick(crl::time now);

	void setupContent();
	void setupCard();
	void setupButton();
	void setupLabel();
	void prepare();
	void layoutContent();
	void placeContent();
	void contentResized();
	void refreshButton();
	void refreshButtonColors();
	void refreshLabelInk();
	void acceptScene(Scene scene, uint64 revision);
	void acquireSlot(bool force = false);
	void checkOrigin();
	void loseSlot();
	void paintTransition(
		QPainter &p,
		const CardViewFrame &frame,
		const Foreground &shown);
	void paintSlot(QPainter &p, float64 lift);
	void applyLabel(const Foreground &shown);
	void paintLabel(QPainter &p, float64 opacity, const Foreground &shown);
	void paintButton(
		QPainter &p,
		float64 progress,
		const Foreground &shown) const;
	void paintCard(
		QPainter &p,
		const CardViewFrame &frame,
		const QImage &image) const;
	void startShowing();
	void finishShowing();
	void finishAtOnce();
	void releaseHeld();
	void finishGuard();
	void queueDeferred();
	void refreshParts();
	void queueParts();
	void queueRefresh();
	void refreshTransition();
	void activate();
	void closeView();
	void remove(Fn<void()> done = nullptr);

	const not_null<Window::SessionController*> _controller;
	const not_null<UserData*> _user;
	const QPointer<Info::Profile::BalancePill> _origin;
	const not_null<Ui::RpWidget*> _content;
	Ui::LayerBackdrop _backdrop;
	rpl::variable<Data::WalletCard> _data;
	rpl::variable<CardLook> _look;
	rpl::variable<ButtonMode> _buttonMode;
	Ui::RpWidget *_card = nullptr;
	Ui::RoundButton *_button = nullptr;
	Ui::FlatLabel *_label = nullptr;
	Scene _scene;
	Parts _parts;
	CardViewMotion _motion;
	CardViewFrame _painted;
	Foreground _displayed;
	Placement _rebasePlacement;
	QString _labelText;
	QString _labelAppliedText;
	QColor _labelAppliedInk;
	LabelInk _labelInk;
	crl::time _started = 0;
	crl::time _elapsed = 0;
	Phase _phase = Phase::Preparing;
	uint64 _sceneRevision = 1;
	uint64 _acceptedRevision = 0;
	bool _busy = false;
	bool _closeQueued = false;
	bool _finishQueued = false;
	bool _removeQueued = false;
	bool _deferredQueued = false;
	bool _slotLost = false;
	Fn<void()> _removeDone;
	bool _captureDeferred = false;
	bool _refreshQueued = false;
	bool _partsQueued = false;
	bool _placing = false;
	bool _removing = false;
	std::unique_ptr<CardStickers> _stickers;
	Ui::Animations::Basic _animation;

};

[[nodiscard]] Controllers &ControllersWithView() {
	static auto result = Controllers();
	return result;
}

[[nodiscard]] int OverlayAlpha(bool night) {
	return anim::interpolate(
		0,
		255,
		night ? st::walletCardViewDimNight : st::walletCardViewDimDay);
}

[[nodiscard]] Ui::LayerWidget *EnclosingLayer(not_null<QWidget*> widget) {
	auto parent = widget->parentWidget();
	while (parent) {
		if (const auto layer = dynamic_cast<Ui::LayerWidget*>(parent)) {
			return layer;
		}
		parent = parent->parentWidget();
	}
	return nullptr;
}

[[nodiscard]] std::optional<QRect> UsableSlot(
		not_null<QWidget*> view,
		Info::Profile::BalancePill *pill) {
	if (!pill
		|| (pill->window() != view->window())
		|| !pill->actionable()
		|| !pill->isVisible()) {
		return std::nullopt;
	}
	auto area = pill->rect();
	for (auto widget = not_null<QWidget*>(pill); !widget->isWindow();) {
		const auto mask = widget->mask();
		if (!mask.isEmpty() && !(QRegion(area) - mask).isEmpty()) {
			return std::nullopt;
		}
		area.translate(widget->pos());
		widget = widget->parentWidget();
		if (!widget->rect().contains(area)) {
			return std::nullopt;
		}
	}
	const auto result = Ui::MapFrom(view, pill, pill->rect());
	if (!view->rect().contains(result)) {
		return std::nullopt;
	}
	return result;
}

[[nodiscard]] QSize ImageSize(const QImage &image) {
	return image.size() / image.devicePixelRatio();
}

void PaintCardImage(
		QPainter &p,
		const QImage &image,
		const QTransform &transform) {
	auto hq = PainterHighQualityEnabler(p);
	p.setTransform(transform, true);
	p.drawImage(QRectF(QPointF(), QSizeF(ImageSize(image))), image);
}

[[nodiscard]] QImage SlotBand(
		const QImage &image,
		const QTransform &transform,
		QRect band,
		QRectF slot,
		float64 clip) {
	const auto ratio = style::DevicePixelRatio();
	auto result = QImage(
		band.size() * ratio,
		QImage::Format_ARGB32_Premultiplied);
	result.setDevicePixelRatio(ratio);
	result.fill(Qt::transparent);
	auto p = QPainter(&result);
	p.translate(-band.topLeft());
	PaintCardImage(p, image, transform);
	p.resetTransform();
	auto fade = QLinearGradient(
		QPointF(0., slot.y() - band.y()),
		QPointF(0., slot.y() + slot.height() - band.y()));
	for (const auto stop : kSlotFadeStops) {
		const auto alpha = (1. - clip) + clip * stop * stop;
		fade.setColorAt(stop, anim::with_alpha(QColor(0, 0, 0), alpha));
	}
	p.setCompositionMode(QPainter::CompositionMode_DestinationIn);
	p.fillRect(QRect(QPoint(), band.size()), fade);
	p.end();
	return result;
}

[[nodiscard]] QString ServedAddress(const QString &address) {
	const auto parsed = ParseAddress(address);
	return parsed
		? FormatFriendly(parsed->raw, false, parsed->testnet)
		: QString();
}

[[nodiscard]] QString ServedFiat(int64 nano, const FiatRate &rate) {
	return rate.available() ? FormatFiat(nano, rate) : QString();
}

[[nodiscard]] CardLook ServedLook(const Data::WalletCard &card) {
	return ResolveCardLook(card.stickers, card.backgroundAddress);
}

void PaintRestingStickers(
		QPainter &p,
		not_null<CardStickers*> stickers,
		QRect rest) {
	const auto fold = ComputeCardFold(rest, 0.);
	if (!fold.valid || fold.opacity <= 0.) {
		return;
	}
	auto hq = PainterHighQualityEnabler(p);
	stickers->paint(p, fold);
}

[[nodiscard]] rpl::producer<TextWithEntities> ButtonText(ButtonMode mode) {
	switch (mode) {
	case ButtonMode::HowItWorks:
		return tr::lng_wallet_profile_how_works(tr::marked);
	case ButtonMode::SendMoney:
		return tr::lng_wallet_profile_send_money(
		) | rpl::map([](const QString &text) {
			return Ui::Text::IconEmoji(
				&st::walletCardViewSendIcon
			).append(text);
		});
	case ButtonMode::OpenWallet:
		return tr::lng_wallet_details_open_wallet(tr::marked);
	case ButtonMode::None:
		break;
	}
	return rpl::single(TextWithEntities());
}

[[nodiscard]] rpl::producer<QString> LabelText(
		not_null<UserData*> user,
		rpl::producer<int64> balance) {
	if (user->isSelf()) {
		return tr::lng_wallet_profile_balance_public();
	}
	return rpl::combine(
		Info::Profile::NameValue(user),
		std::move(balance)
	) | rpl::map([](const QString &name, int64 balance) {
		// The same digits as the card's amount shows.
		const auto amount = Ui::FormatTonAmount(balance).wholeString
			+ ((balance % Ui::kNanosInOne)
				? GramMinorPart(balance)
				: QString());
		return (balance > 0)
			? tr::lng_wallet_profile_balance_owns(
				lt_count,
				rpl::single(balance / float64(Ui::kNanosInOne))
					| tr::to_count(),
				lt_name,
				rpl::single(name),
				lt_amount,
				rpl::single(amount))
			: tr::lng_wallet_profile_balance_none(
				lt_name,
				rpl::single(name));
	}) | rpl::flatten_latest();
}

void PaintButtonOutline(QPainter &p, QRect button) {
	const auto outline = st::walletCardViewButtonOutline;
	const auto rect = button.marginsAdded(
		{ outline, outline, outline, outline });
	const auto radius = rect.height() / 2.;
	auto hq = PainterHighQualityEnabler(p);
	p.setPen(Qt::NoPen);
	p.setBrush(st::windowBg);
	p.drawRoundedRect(rect, radius, radius);
}

// The outline travels with the button through the transition frames.
[[nodiscard]] QImage OutlinedButton(const QImage &button) {
	const auto outline = st::walletCardViewButtonOutline;
	const auto size = ImageSize(button);
	const auto ratio = style::DevicePixelRatio();
	auto result = QImage(
		(size + QSize(2 * outline, 2 * outline)) * ratio,
		QImage::Format_ARGB32_Premultiplied);
	result.setDevicePixelRatio(ratio);
	result.fill(Qt::transparent);
	auto p = QPainter(&result);
	const auto inner = QRect(QPoint(outline, outline), size);
	PaintButtonOutline(p, inner);
	p.drawImage(inner, button);
	p.end();
	return result;
}

[[nodiscard]] QColor PlateColor(float64 level) {
	auto result = st::windowBg->c;
	result.setAlphaF(level);
	return result;
}

[[nodiscard]] float64 PlateRadius(QRect plate) {
	const auto &padding = st::walletCardViewLabelPlatePadding;
	const auto x = std::min(padding.left(), padding.right());
	const auto y = std::min(padding.top(), padding.bottom());
	return std::min(plate.height() / 2., float64(x + y + std::min(x, y)));
}

void PaintLabelPlate(QPainter &p, QRect plate, const QColor &color) {
	auto hq = PainterHighQualityEnabler(p);
	p.setPen(Qt::NoPen);
	p.setBrush(color);
	const auto radius = PlateRadius(plate);
	p.drawRoundedRect(plate, radius, radius);
}

[[nodiscard]] std::vector<float64> SortedLuminances(const QImage &image) {
	auto result = std::vector<float64>();
	result.reserve(image.width() * image.height());
	for (auto y = 0; y != image.height(); ++y) {
		const auto line = reinterpret_cast<const QRgb*>(
			image.constScanLine(y));
		for (auto x = 0; x != image.width(); ++x) {
			result.push_back(CardLuminance(line[x]));
		}
	}
	ranges::sort(result);
	result.erase(std::unique(begin(result), end(result)), end(result));
	return result;
}

[[nodiscard]] float64 Contrast(float64 a, float64 b) {
	return (std::max(a, b) + 0.05) / (std::min(a, b) + 0.05);
}

[[nodiscard]] float64 WorstContrast(
		const std::vector<float64> &sorted,
		float64 luminance) {
	auto result = std::numeric_limits<float64>::infinity();
	const auto i = ranges::lower_bound(sorted, luminance);
	if (i != end(sorted)) {
		result = Contrast(*i, luminance);
	}
	if (i != begin(sorted)) {
		result = std::min(result, Contrast(*(i - 1), luminance));
	}
	return result;
}

[[nodiscard]] QColor MixInk(const QColor &base, int target, int step) {
	const auto ratio = step / float64(kInkSteps);
	const auto mix = [&](int channel) {
		return int(base::SafeRound(channel + (target - channel) * ratio));
	};
	return QColor(mix(base.red()), mix(base.green()), mix(base.blue()));
}

[[nodiscard]] std::optional<QColor> FindInk(
		const QColor &base,
		const std::vector<float64> &sorted,
		float64 required) {
	auto result = std::optional<QColor>();
	auto limit = kInkSteps + 1;
	for (const auto target : { 0, 255 }) {
		for (auto step = 0; step < limit; ++step) {
			const auto ink = MixInk(base, target, step);
			const auto luminance = CardLuminance(ink.rgb());
			if (WorstContrast(sorted, luminance) >= required) {
				result = ink;
				limit = step;
				break;
			}
		}
	}
	return result;
}

[[nodiscard]] LabelInk ChooseLabelInk(
		const QColor &base,
		Fn<QImage(float64 plate)> sample) {
	for (const auto plate : kPlateLevels) {
		const auto sorted = SortedLuminances(sample(plate));
		for (const auto required : { kPreferredContrast, kRequiredContrast }) {
			if (const auto ink = FindInk(base, sorted, required)) {
				return { *ink, plate };
			}
		}
	}
	const auto full = kPlateLevels.back();
	const auto background = CardLuminance(PlateColor(full).rgb());
	const auto black = QColor(0, 0, 0);
	const auto white = QColor(255, 255, 255);
	const auto darker = (Contrast(background, CardLuminance(black.rgb()))
		>= Contrast(background, CardLuminance(white.rgb())));
	return { darker ? black : white, full };
}

CardView::CardView(
	not_null<Window::SessionController*> controller,
	not_null<UserData*> user,
	not_null<Info::Profile::BalancePill*> origin)
: _controller(controller)
, _user(user)
, _origin(origin.get())
, _content(Ui::CreateChild<Ui::RpWidget>(this))
, _backdrop(this, [=] { return captureScene(); }, OverlayAlpha)
, _data(*user->walletCard())
, _animation([=](crl::time now) { return tick(now); }) {
	ControllersWithView().emplace(_controller);
	_content->hide();
	setupContent();

	_backdrop.changes(
	) | rpl::on_next([=] {
		refreshLabelInk();
	}, lifetime());

	style::PaletteChanged(
	) | rpl::on_next([=] {
		_backdrop.invalidate();
		refreshButtonColors();
		refreshLabelInk();
		queueRefresh();
	}, lifetime());

	_user->session().changes().peerFlagsValue(
		_user,
		Data::PeerUpdate::Flag::WalletCard
	) | rpl::on_next([=] {
		if (const auto card = _user->walletCard()) {
			_data = *card;
		} else {
			closeView();
		}
	}, lifetime());

	_user->session().data().sessionDataAboutToBeCleared(
	) | rpl::on_next([=] {
		remove();
	}, lifetime());

	anim::Disables(
	) | rpl::filter([](bool disabled) {
		return disabled;
	}) | rpl::on_next([=] {
		finishAtOnce();
	}, lifetime());
}

CardView::~CardView() {
	ControllersWithView().remove(_controller);
}

void CardView::parentResized() {
	resizeToWidth(parentWidget()->width());
	layoutContent();
	queueRefresh();
	if (!_busy && (!_backdrop.started() || _phase == Phase::Preparing)) {
		prepare();
	}
}

void CardView::showFinished() {
	if (base::take(_captureDeferred)) {
		_backdrop.invalidate();
		queueRefresh();
	}
	if (_busy) {
		_refreshQueued = true;
		return;
	}
	prepare();
	if (_phase == Phase::Preparing && _backdrop.started()) {
		startShowing();
	}
}

bool CardView::closeByOutsideClick() const {
	return false;
}

bool CardView::closeByBackButton() {
	closeView();
	return true;
}

int CardView::resizeGetHeight(int newWidth) {
	return parentWidget()->height();
}

void CardView::paintEvent(QPaintEvent *e) {
	if (_busy || _removing) {
		return;
	}
	const auto alive = QPointer<CardView>(this);
	if (_phase == Phase::Shown && !_backdrop.started()) {
		prepare();
	}
	if (_phase != Phase::Shown) {
		acquireSlot();
		if (!alive || _removing) {
			return;
		}
		if (_closeQueued && !_started && _phase != Phase::Shown) {
			_removeQueued = true;
			_closeQueued = false;
		}
		if (_finishQueued && _phase == Phase::Closing) {
			_removeQueued = true;
		}
		if (_removeQueued) {
			auto p = QPainter(this);
			p.drawImage(rect(), _scene.sharp);
			queueDeferred();
			return;
		} else if (base::take(_finishQueued)) {
			finishShowing();
		} else if (_phase == Phase::Preparing && _backdrop.started()) {
			startShowing();
		}
	}
	if (_phase == Phase::Preparing || !exposed()) {
		return;
	}
	_busy = true;
	const auto weak = QPointer<CardView>(this);
	const auto guard = gsl::finally([=] {
		if (weak) {
			weak->finishGuard();
		}
	});
	if (_phase == Phase::Shown) {
		auto p = QPainter(this);
		_backdrop.paint(p);
		return;
	}
	const auto first = !_started;
	if (first) {
		_elapsed = 0;
	}
	if (!holding() && !_removeQueued && !_finishQueued) {
		// Hidden card children, like the look's emoji, repaint unannounced.
		auto card = Ui::GrabWidgetToImage(_card);
		if (!weak) {
			return;
		}
		_parts.card = std::move(card);
	}
	if (_closeQueued && first) {
		_removeQueued = true;
		_closeQueued = false;
	}
	if (_removeQueued || _finishQueued) {
		auto p = QPainter(this);
		if (_removeQueued || _phase == Phase::Closing) {
			p.drawImage(rect(), _scene.sharp);
		} else {
			if (_label) {
				_label->setOpacity(1.);
			}
			_backdrop.paint(p);
			Ui::RenderWidget(p, _content, _content->pos());
		}
		return;
	}
	const auto frame = _closeQueued
		? _painted
		: _motion.frame(_elapsed, places());
	const auto live = foreground();
	const auto hold = holding() || _closeQueued;
	auto shown = hold ? _displayed : live;
	if (!hold && frame.rebase < 1.) {
		const auto mix = [&](QRect from, QRect to) {
			return QRect(
				from.topLeft() + (to.topLeft() - from.topLeft()) * frame.rebase,
				to.size());
		};
		shown.placement.button = mix(
			_rebasePlacement.button,
			shown.placement.button);
		shown.placement.label = mix(
			_rebasePlacement.label,
			shown.placement.label);
		shown.placement.cardWidth = anim::interpolate(
			_rebasePlacement.cardWidth,
			shown.placement.cardWidth,
			frame.rebase);
	}
	applyLabel(shown);
	{
		auto p = QPainter(this);
		paintTransition(p, frame, shown);
	}
	if (!weak) {
		return;
	}
	_painted = frame;
	_displayed = std::move(shown);
	if (first) {
		// WHY: the clock starts at the first painted frame, so that frame
		// is the start pose however long the capture and its paint took.
		_started = crl::now();
		_animation.start();
	}
	applyLabel(live);
	if (frame.finished) {
		_animation.stop();
		if (_phase == Phase::Opening) {
			_finishQueued = true;
		} else {
			_removeQueued = true;
		}
	}
}

void CardView::keyPressEvent(QKeyEvent *e) {
	if (e->key() == Qt::Key_Escape) {
		e->accept();
		closeView();
	} else {
		Ui::LayerWidget::keyPressEvent(e);
	}
}

void CardView::mousePressEvent(QMouseEvent *e) {
	e->accept();
	const auto position = _content->mapFromParent(e->pos());
	if (_phase != Phase::Shown
		|| _card->geometry().contains(position)
		|| (!_button->isHidden()
			&& _button->geometry().contains(position))) {
		return;
	}
	Ui::PostponeCall(this, [=] {
		closeView();
	});
}

void CardView::closeHook() {
	_removing = true;
	_animation.stop();
}

Ui::LayerStackWidget *CardView::stack() const {
	return dynamic_cast<Ui::LayerStackWidget*>(parentWidget());
}

bool CardView::covered() const {
	const auto stack = this->stack();
	return !stack || (stack->topShownLayer() != this);
}

bool CardView::onlyLayer(not_null<Ui::LayerStackWidget*> stack) const {
	if (stack->topShownLayer() != this || isHidden()) {
		return false;
	}
	return ranges::none_of(stack->children(), [&](QObject *child) {
		const auto layer = dynamic_cast<Ui::LayerWidget*>(child);
		return layer && (layer != this) && layer->isHidden();
	});
}

Ui::LayerWidget *CardView::underlyingLayer() const {
	const auto origin = _origin.data();
	if (origin && (origin->window() == window())) {
		const auto layer = EnclosingLayer(origin);
		if (layer != this) {
			return layer;
		}
	}
	const auto stack = this->stack();
	if (!stack) {
		return nullptr;
	}
	for (const auto child : stack->children()) {
		const auto layer = dynamic_cast<Ui::LayerWidget*>(child);
		if (layer && (layer != this) && !layer->isHidden()) {
			return layer;
		}
	}
	return nullptr;
}

QImage CardView::captureScene() {
	const auto weak = QPointer<CardView>(this);
	if (_busy || !exposed()) {
		_captureDeferred = true;
		_refreshQueued = true;
		return QImage();
	}
	acquireSlot(true);
	return (weak && exposed()) ? _scene.sharp : QImage();
}

QImage CardView::captureSharp(Scene &scene, uint64 &revision) {
	const auto weak = QPointer<CardView>(this);
	return Ui::CaptureUnderLayer(this, underlyingLayer(), [&] {
		const auto origin = _origin.data();
		if (UsableSlot(this, origin)) {
			scene.empty = origin->grabEmpty();
		}
		if (!weak) {
			return;
		}
		scene.origin = observeOrigin();
		scene.slot = UsableSlot(this, _origin.data());
		scene.rest = _card->geometry().translated(_content->pos());
		revision = _sceneRevision;
		_refreshQueued = false;
	});
}

OriginObservation CardView::observeOrigin() const {
	const auto origin = _origin.data();
	const auto underlying = underlyingLayer();
	auto result = OriginObservation{
		.origin = origin,
		.parent = origin ? origin->parentWidget() : nullptr,
		.window = origin ? origin->window() : nullptr,
		.underlying = underlying,
		.size = size(),
		.ratio = style::DevicePixelRatio(),
		.appearance = origin ? origin->appearanceRevision() : 0,
		.present = (origin != nullptr),
		.sameWindow = origin && (origin->window() == window()),
		.visible = origin && origin->isVisible(),
		.actionable = origin && origin->actionable(),
		.special = underlying
			&& underlying->parentWidget() == parentWidget()
			&& !underlying->isHidden(),
	};
	if (!result.sameWindow) {
		return result;
	}
	result.geometry = QRect(
		mapFrom(window(), origin->mapTo(window(), QPoint())),
		origin->size());
	auto region = QRegion(origin->rect());
	for (auto widget = static_cast<QWidget*>(origin);;) {
		region &= widget->rect();
		if (!widget->mask().isEmpty()) {
			region &= widget->mask();
		}
		if (widget->isWindow()) {
			result.clipped = region.translated(mapFrom(widget, QPoint()));
			break;
		}
		region.translate(widget->pos());
		widget = widget->parentWidget();
	}
	return result;
}

bool CardView::exposed() const {
	return !covered()
		&& !size().isEmpty()
		&& window()->isVisible()
		&& !window()->isMinimized();
}

Foreground CardView::foreground() const {
	const auto origin = _content->pos();
	return {
		.parts = _parts,
		.places = places(),
		.placement = {
			.button = _button->geometry().translated(origin),
			.label = _label ? _label->geometry().translated(origin) : QRect(),
			.cardWidth = _card->width(),
		},
		.text = _labelText,
		.ink = _labelInk.color,
		.plate = PlateColor(_labelInk.plate),
	};
}

CardViewPlaces CardView::places() const {
	return {
		.rest = _scene.rest,
		.slot = _slotLost ? std::nullopt : _scene.slot,
	};
}

Parts CardView::grabParts() const {
	return {
		.card = Ui::GrabWidgetToImage(_card),
		.button = (_button->isHidden()
			? QImage()
			: OutlinedButton(Ui::GrabWidgetToImage(_button))),
	};
}

bool CardView::holding() const {
	return (_phase == Phase::Closing) && !_started;
}

ButtonMode CardView::computeButtonMode() const {
	const auto session = &_user->session();
	if (&_controller->session() != session
		|| session->supportMode()
		|| !session->appConfig().walletAvailable()) {
		return ButtonMode::None;
	} else if (_user->isSelf()) {
		return ButtonMode::OpenWallet;
	}
	const auto presence = session->wallet().presenceCurrent();
	if (presence == Presence::Missing
		|| presence == Presence::Provisioning) {
		return ButtonMode::HowItWorks;
	}
	return ((session->data().userLoaded(peerToUser(_user->id)) == _user)
			&& CanOfferSendMoney(_user))
		? ButtonMode::SendMoney
		: ButtonMode::None;
}

QRect CardView::labelPlate() const {
	return _label->geometry().marginsAdded(
		st::walletCardViewLabelPlatePadding);
}

QImage CardView::sampleLabelBackground(float64 plate) const {
	const auto origin = _content->pos();
	const auto area = _label->geometry().translated(origin).intersected(
		rect());
	if (area.isEmpty()) {
		return QImage();
	}
	const auto ratio = style::DevicePixelRatio();
	auto result = QImage(
		area.size() * ratio,
		QImage::Format_ARGB32_Premultiplied);
	result.setDevicePixelRatio(ratio);
	result.fill(Qt::transparent);
	auto p = QPainter(&result);
	p.translate(-area.topLeft());
	_backdrop.paint(p);
	if (plate > 0.) {
		PaintLabelPlate(p, labelPlate().translated(origin), PlateColor(plate));
	}
	p.end();
	return result;
}

LabelInk CardView::computeLabelInk() const {
	auto base = st::windowSubTextFg->c;
	base.setAlpha(255);
	return ChooseLabelInk(base, [&](float64 plate) {
		return sampleLabelBackground(plate);
	});
}

void CardView::setupContent() {
	setupCard();
	setupButton();
	setupLabel();

	_content->paintRequest(
	) | rpl::on_next([=] {
		auto p = QPainter(_content);
		if (_labelInk.plate > 0.) {
			PaintLabelPlate(p, labelPlate(), PlateColor(_labelInk.plate));
		}
		if (!_button->isHidden()) {
			PaintButtonOutline(p, _button->geometry());
		}
	}, _content->lifetime());

	rpl::merge(
		_data.changes() | rpl::to_empty,
		FiatRateValue(&_user->session()) | rpl::skip(1) | rpl::to_empty,
		Info::Profile::NameValue(_user) | rpl::skip(1) | rpl::to_empty
	) | rpl::on_next([=] {
		queueParts();
	}, lifetime());
}

void CardView::setupCard() {
	const auto session = &_user->session();
	const auto balance = [=] {
		return _data.value(
		) | rpl::map([](const Data::WalletCard &card) {
			return card.balance;
		}) | rpl::distinct_until_changed();
	};
	_look = _data.value() | rpl::map(ServedLook);
	_card = MakeWalletCard(
		_content,
		_controller->uiShow(),
		std::make_shared<bool>(false),
		{
			.name = Info::Profile::NameValue(
				_user
			) | rpl::map([](QString name) {
				return tr::marked(std::move(name));
			}),
			.address = _data.value(
			) | rpl::map([](const Data::WalletCard &card) {
				return ServedAddress(card.address);
			}) | rpl::distinct_until_changed(),
			.balanceNano = balance(),
			.fiat = rpl::combine(
				balance(),
				FiatRateValue(session)
			) | rpl::map([](int64 nano, const FiatRate &rate) {
				return ServedFiat(nano, rate);
			}),
			.look = _look.value(),
		}).release();

	const auto overlay = Ui::CreateChild<Ui::RpWidget>(_card);
	overlay->setAttribute(Qt::WA_TransparentForMouseEvents);
	overlay->show();
	overlay->raise();
	_stickers = std::make_unique<CardStickers>(
		session,
		_data.value(
		) | rpl::map([](const Data::WalletCard &card) {
			return card.stickers;
		}) | rpl::distinct_until_changed(),
		[=] {
			overlay->update();
			queueParts();
		});
	_card->sizeValue(
	) | rpl::on_next([=](QSize size) {
		overlay->setGeometry(QRect(QPoint(), size));
	}, overlay->lifetime());
	overlay->paintRequest(
	) | rpl::on_next([=] {
		auto p = QPainter(overlay);
		PaintRestingStickers(p, _stickers.get(), overlay->rect());
	}, overlay->lifetime());
}

void CardView::setupButton() {
	_button = Ui::CreateChild<Ui::RoundButton>(
		_content.get(),
		rpl::single(QString()),
		st::walletSendButton);
	_button->setTextTransform(Ui::RoundButtonTextTransform::NoTransform);
	_buttonMode = computeButtonMode();
	_button->setVisible(_buttonMode.current() != ButtonMode::None);
	_button->setClickedCallback([=] {
		Ui::PostponeCall(this, [=] {
			activate();
		});
	});

	_look.value() | rpl::on_next([=] {
		refreshButtonColors();
	}, _button->lifetime());

	const auto session = &_user->session();
	auto &wallet = session->wallet();
	using Flag = Data::PeerUpdate::Flag;
	rpl::merge(
		session->appConfig().value(),
		wallet.stateKnownValue() | rpl::to_empty,
		wallet.presenceValue() | rpl::to_empty,
		wallet.userAddresses().unavailableValue() | rpl::to_empty,
		_user->flagsValue() | rpl::to_empty,
		session->changes().peerUpdates(
			_user,
			Flag::FullInfo | Flag::Name | Flag::SupportInfo | Flag::IsBot
		) | rpl::to_empty
	) | rpl::on_next([=] {
		refreshButton();
	}, _button->lifetime());

	_buttonMode.value(
	) | rpl::map(
		ButtonText
	) | rpl::flatten_latest(
	) | rpl::on_next([=](TextWithEntities text) {
		_button->setText(rpl::single(std::move(text)));
		contentResized();
	}, _button->lifetime());
}

void CardView::setupLabel() {
	_label = Ui::CreateChild<Ui::FlatLabel>(
		_content.get(),
		st::walletCardViewLabel);
	_label->setAttribute(Qt::WA_TransparentForMouseEvents);

	LabelText(
		_user,
		_data.value(
		) | rpl::map([](const Data::WalletCard &card) {
			return card.balance;
		}) | rpl::distinct_until_changed()
	) | rpl::on_next([=](const QString &text) {
		_labelText = text;
		_labelAppliedText = text;
		_label->setText(text);
		contentResized();
	}, _label->lifetime());
}

void CardView::prepare() {
	if (!_backdrop.started()
		|| _phase == Phase::Preparing
		|| (_phase == Phase::Opening && !_started)) {
		acquireSlot();
	}
}

void CardView::layoutContent() {
	_content->setGeometry(rect());
	placeContent();
	refreshLabelInk();
}

void CardView::placeContent() {
	_placing = true;
	const auto outer = _content->size();
	const auto skip = st::walletCardViewSkip;
	const auto cardWidth = _card->naturalWidth();
	const auto cardHeight = _card->height();
	const auto buttonShown = !_button->isHidden();
	const auto buttonSize = _button->size();
	if (buttonShown) {
		const auto limit = outer.width() - 2 * skip;
		_button->setFullWidth(0);
		if (limit > 0 && _button->width() > limit) {
			_button->setFullWidth(limit);
		}
	}
	const auto below = buttonShown ? (skip + _button->height()) : 0;
	const auto plateTop = (_labelInk.plate > 0.)
		? st::walletCardViewLabelPlatePadding.top()
		: 0;
	const auto above = [&] {
		return _label ? (_label->height() + skip + plateTop) : 0;
	};
	const auto till = outer.height() - skip - below - cardHeight;
	if (_label) {
		// Other users' text breaks its own lines.
		const auto labelWidth = _user->isSelf()
			? st::walletCardViewLabelWidth
			: std::min(_label->textMaxWidth(), cardWidth);
		_label->resizeToWidth(labelWidth);
		if (skip + above() > till) {
			_label->resizeToWidth(std::max(cardWidth, labelWidth));
		}
	}
	const auto from = skip + above();
	const auto cardTop = (from <= till)
		? std::clamp((outer.height() - cardHeight) / 2, from, till)
		: ((outer.height() - (above() + cardHeight + below)) / 2
			+ above());
	_card->setGeometry(
		(outer.width() - cardWidth) / 2,
		cardTop,
		cardWidth,
		cardHeight);
	if (_label) {
		_label->move(
			(outer.width() - _label->width()) / 2,
			cardTop - skip - _label->height());
	}
	if (buttonShown) {
		_button->move(
			(outer.width() - _button->width()) / 2,
			cardTop + cardHeight + skip);
	}
	_content->update();
	_placing = false;
	if (_button->size() != buttonSize) {
		queueParts();
	}
}

void CardView::contentResized() {
	if (!_placing) {
		layoutContent();
		queueParts();
	}
}

void CardView::refreshButton() {
	const auto mode = computeButtonMode();
	if (_buttonMode.current() == mode) {
		return;
	}
	_button->setVisible(mode != ButtonMode::None);
	_buttonMode = mode;
}

void CardView::refreshButtonColors() {
	const auto look = _look.current();
	if (look.brand()) {
		_button->setBrushOverride(std::nullopt);
		_button->setRippleOverride(std::nullopt);
		_button->setTextFgOverride(std::nullopt);
	} else {
		const auto tint = ComputeCardTint(
			look.center,
			look.edge,
			{ st::windowBg->c, st::windowBgOver->c });
		_button->setBrushOverride(
			QBrush(CardTintAt(tint, kButtonFillLevel)));
		_button->setRippleOverride(CardTintAt(tint, kButtonRippleLevel));
		_button->setTextFgOverride(QColor(255, 255, 255));
	}
	queueParts();
}

void CardView::refreshLabelInk() {
	if (!_label || !_backdrop.started()) {
		return;
	}
	const auto busy = std::exchange(_busy, true);
	const auto weak = QPointer<CardView>(this);
	const auto guard = gsl::finally([=] {
		if (!busy && weak) {
			weak->finishGuard();
		}
	});
	const auto was = _labelInk.plate;
	auto ink = computeLabelInk();
	if ((ink.plate > 0.) != (was > 0.)) {
		_labelInk.plate = ink.plate;
		placeContent();
		ink = computeLabelInk();
	}
	_labelInk = ink;
	_labelAppliedInk = ink.color;
	_label->setTextColorOverride(ink.color);
	if (ink.plate != was) {
		_content->update();
	}
}

void CardView::acceptScene(Scene scene, uint64 revision) {
	const auto old = places();
	_scene = std::move(scene);
	_acceptedRevision = revision;
	if (!_backdrop.started()
		|| _phase == Phase::Preparing
		|| (_phase == Phase::Opening && !_started)) {
		_backdrop.start(_scene.sharp);
		refreshLabelInk();
		_scene.rest = _card->geometry().translated(_content->pos());
		if (_phase != Phase::Shown) {
			_motion.open(places());
		}
	} else if (_phase != Phase::Shown) {
		const auto target = places();
		if (_motion.usesSlot() && !ValidCardViewSlot(target)) {
			loseSlot();
		} else if ((old.slot != target.slot || old.rest != target.rest)
			&& _motion.retarget(_painted, _elapsed, target)) {
			_rebasePlacement = _displayed.placement;
		}
	}
	update();
}

void CardView::acquireSlot(bool force) {
	if (_busy) {
		_refreshQueued = true;
		return;
	} else if (!exposed()) {
		_captureDeferred = true;
		return;
	}
	if (!force) {
		checkOrigin();
	}
	if (!force
		&& _sceneRevision == _acceptedRevision
		&& !_scene.sharp.isNull()) {
		_refreshQueued = false;
		return;
	}
	_busy = true;
	const auto weak = QPointer<CardView>(this);
	const auto guard = gsl::finally([=] {
		if (weak) {
			weak->finishGuard();
		}
	});
	_captureDeferred = false;
	auto scene = Scene();
	auto revision = _sceneRevision;
	scene.sharp = captureSharp(scene, revision);
	if (!weak || _removing) {
		return;
	}
	if (!exposed()) {
		_captureDeferred = true;
		return;
	}
	Assert(!scene.sharp.isNull());
	acceptScene(std::move(scene), revision);
}

void CardView::checkOrigin() {
	if (observeOrigin() != _scene.origin
		|| _card->geometry().translated(_content->pos()) != _scene.rest) {
		queueRefresh();
	}
}

void CardView::loseSlot() {
	if (_motion.usesSlot()) {
		_slotLost = true;
		_motion.loseSlot(_painted, _elapsed, places());
		_rebasePlacement = _displayed.placement;
	}
}

void CardView::paintTransition(
		QPainter &p,
		const CardViewFrame &frame,
		const Foreground &shown) {
	if (!_scene.sharp.isNull()) {
		p.drawImage(rect(), _scene.sharp);
	}
	if (frame.patch) {
		paintSlot(p, frame.lift);
	}
	_backdrop.paintDim(p, frame.dim);
	if (frame.blur > 0.) {
		p.setOpacity(frame.blur);
		_backdrop.paint(p);
	}
	paintLabel(p, frame.label, shown);
	p.setOpacity(1.);
	paintButton(p, frame.button, shown);
	paintCard(p, frame, shown.parts.card);
}

void CardView::paintSlot(QPainter &p, float64 lift) {
	const auto origin = _origin.data();
	if (!_scene.slot || UsableSlot(this, origin) != _scene.slot) {
		return;
	}
	const auto slot = *_scene.slot;
	p.drawImage(slot.topLeft(), _scene.empty);
	if (lift < 1.) {
		const auto ratio = style::DevicePixelRatio();
		const auto shift = std::round(lift * slot.height() * ratio) / ratio;
		p.save();
		p.setClipRect(slot);
		p.translate(slot.x(), slot.y() - shift);
		origin->paintContentAlone(p);
		p.restore();
	}
}

void CardView::applyLabel(const Foreground &shown) {
	if (!_label) {
		return;
	}
	if (_labelAppliedText != shown.text) {
		_labelAppliedText = shown.text;
		_label->setText(shown.text);
	}
	const auto geometry = shown.placement.label.translated(-_content->pos());
	if (_label->width() != geometry.width()) {
		_label->resizeToWidth(geometry.width());
	}
	if (_label->geometry() != geometry) {
		_label->setGeometry(geometry);
	}
	if (_labelAppliedInk != shown.ink) {
		_labelAppliedInk = shown.ink;
		_label->setTextColorOverride(shown.ink);
	}
}

void CardView::paintLabel(
		QPainter &p,
		float64 opacity,
		const Foreground &shown) {
	if (!_label || opacity <= 0.) {
		return;
	}
	if (shown.plate.alpha() > 0) {
		p.setOpacity(opacity);
		PaintLabelPlate(
			p,
			shown.placement.label.marginsAdded(st::walletCardViewLabelPlatePadding),
			shown.plate);
	}
	p.setOpacity(1.);
	_label->setOpacity(opacity);
	p.save();
	p.translate(shown.placement.label.topLeft());
	Ui::RenderWidget(p, _label, QPoint());
	p.restore();
}

void CardView::paintButton(
		QPainter &p,
		float64 progress,
		const Foreground &shown) const {
	const auto opacity = FlightEaseOut(progress);
	if (shown.parts.button.isNull() || opacity <= 0.) {
		return;
	}
	const auto rest = shown.placement.button;
	p.save();
	if (opacity < 1.) {
		const auto center = QRectF(rest).center();
		const auto rise = kButtonRise * shown.placement.cardWidth * (1. - opacity);
		const auto scale = kButtonScale + (1. - kButtonScale) * opacity;
		p.setRenderHint(QPainter::SmoothPixmapTransform);
		p.setOpacity(opacity);
		p.translate(center.x(), center.y() + rise);
		p.scale(scale, scale);
		p.translate(-center.x(), -center.y());
	}
	const auto outline = st::walletCardViewButtonOutline;
	p.drawImage(
		rest.topLeft() - QPoint(outline, outline),
		shown.parts.button);
	p.restore();
}

void CardView::paintCard(
		QPainter &p,
		const CardViewFrame &frame,
		const QImage &image) const {
	if (image.isNull() || frame.card.opacity <= 0.) {
		return;
	}
	const auto transform = CardViewTransform(ImageSize(image), frame.card);
	if (!transform) {
		return;
	}
	p.save();
	p.setOpacity(frame.card.opacity);
	if (frame.clip > 0.) {
		const auto slot = frame.slot.toAlignedRect();
		const auto bottom = slot.y() + slot.height();
		const auto bounds = transform->mapRect(
			QRectF(QPointF(), QSizeF(ImageSize(image)))
		).toAlignedRect().intersected(rect());
		const auto from = (frame.clip < 1.)
			? bounds.y()
			: std::max(bounds.y(), slot.y());
		const auto till = std::min(bounds.y() + bounds.height(), bottom);
		if (till > from) {
			const auto band = QRect(
				bounds.x(),
				from,
				bounds.width(),
				till - from);
			p.drawImage(
				band.topLeft(),
				SlotBand(image, *transform, band, frame.slot, frame.clip));
		}
		p.setClipRect(
			QRect(0, bottom, width(), std::max(height() - bottom, 0)));
	}
	PaintCardImage(p, image, *transform);
	p.restore();
}

bool CardView::tick(crl::time now) {
	if (_phase != Phase::Opening && _phase != Phase::Closing) {
		return false;
	} else if (_busy) {
		return true;
	}
	_elapsed = std::max(now - _started, crl::time());
	checkOrigin();
	update();
	if (anim::Disabled()) {
		finishAtOnce();
		return false;
	} else if (_motion.frame(_elapsed, places()).finished) {
		if (!exposed()) {
			if (_phase == Phase::Opening) {
				finishShowing();
			} else {
				_removeQueued = true;
				queueDeferred();
			}
		}
		return false;
	}
	return true;
}

void CardView::startShowing() {
	if (_busy) {
		_refreshQueued = true;
		return;
	} else if (anim::Disabled()) {
		finishShowing();
		return;
	}
	_phase = Phase::Opening;
	refreshParts();
	update();
}

void CardView::finishShowing() {
	if (_busy) {
		_finishQueued = true;
		return;
	}
	_phase = Phase::Shown;
	_started = 0;
	_animation.stop();
	_scene = Scene();
	_parts = Parts();
	_displayed = Foreground();
	_refreshQueued = false;
	_partsQueued = false;
	if (_label) {
		_label->setOpacity(1.);
	}
	_content->show();
	update();
}

void CardView::finishAtOnce() {
	if (_busy) {
		_finishQueued = true;
		return;
	}
	if (_phase == Phase::Closing) {
		_animation.stop();
		_removeQueued = true;
		queueDeferred();
	} else if (_phase == Phase::Opening || _phase == Phase::Preparing) {
		finishShowing();
	}
}

void CardView::finishGuard() {
	_busy = false;
	queueDeferred();
}

void CardView::queueDeferred() {
	if (_busy || _removing || _deferredQueued) {
		return;
	}
	const auto work = _removeQueued
		|| _finishQueued
		|| _closeQueued
		|| (_refreshQueued && exposed())
		|| (_partsQueued && !holding());
	if (!work) {
		return;
	}
	_deferredQueued = true;
	Ui::PostponeCall(this, [=] {
		_deferredQueued = false;
		if (_busy || _removing) {
			return;
		} else if (base::take(_removeQueued)) {
			remove(base::take(_removeDone));
			return;
		} else if (base::take(_finishQueued)) {
			if (_phase == Phase::Closing) {
				remove();
				return;
			}
			finishShowing();
		}
		if (base::take(_closeQueued)) {
			closeView();
			return;
		}
		if (_captureDeferred && exposed()) {
			_captureDeferred = false;
			_backdrop.invalidate();
		}
		releaseHeld();
	});
}

void CardView::releaseHeld() {
	if (_refreshQueued && exposed()) {
		refreshTransition();
	}
	if (!holding() && base::take(_partsQueued)) {
		refreshParts();
	}
}

void CardView::refreshParts() {
	if (_busy || holding()) {
		_partsQueued = true;
		return;
	} else if (_phase == Phase::Shown || _removing) {
		return;
	}
	_busy = true;
	const auto weak = QPointer<CardView>(this);
	const auto guard = gsl::finally([=] {
		if (weak) {
			weak->finishGuard();
		}
	});
	auto parts = grabParts();
	if (!weak) {
		return;
	}
	_parts = std::move(parts);
	update();
}

void CardView::queueParts() {
	if (_phase == Phase::Shown || _removing) {
		return;
	}
	_partsQueued = true;
	queueDeferred();
}

void CardView::queueRefresh() {
	++_sceneRevision;
	if (_phase == Phase::Shown || _removing) {
		return;
	}
	if (!_refreshQueued
		&& (_started || _phase == Phase::Closing)) {
		_backdrop.invalidate();
	}
	_refreshQueued = true;
	queueDeferred();
}

void CardView::refreshTransition() {
	if (_phase == Phase::Shown || _removing) {
		_refreshQueued = false;
		return;
	}
	acquireSlot();
	if (_phase == Phase::Preparing && _backdrop.started()) {
		startShowing();
	}
}

void CardView::activate() {
	if (_phase != Phase::Shown || _removing) {
		return;
	}
	const auto mode = computeButtonMode();
	if (mode == ButtonMode::None || mode != _buttonMode.current()) {
		refreshButton();
		return;
	}
	const auto user = _user;
	const auto userId = peerToUser(user->id);
	const auto weakController = base::make_weak(_controller);
	const auto weakSession = base::make_weak(&user->session());
	remove([=] {
		const auto controller = weakController.get();
		const auto session = weakSession.get();
		if (!controller
			|| !session
			|| &controller->session() != session
			|| &user->session() != session) {
			return;
		}
		switch (mode) {
		case ButtonMode::OpenWallet:
			ShowWallet(session);
			break;
		case ButtonMode::HowItWorks:
			ShowWalletHowItWorks(controller->uiShow());
			break;
		case ButtonMode::SendMoney:
			if (session->data().userLoaded(userId) == user) {
				OpenSendMoney(controller, user);
			}
			break;
		case ButtonMode::None:
			break;
		}
	});
}

void CardView::closeView() {
	if (_phase == Phase::Closing || _removing) {
		return;
	} else if (_busy) {
		_closeQueued = true;
		return;
	}
	if (base::take(_finishQueued)) {
		finishShowing();
	}
	const auto shown = (_phase == Phase::Shown);
	const auto painted = (_phase == Phase::Opening) && (_started != 0);
	if (!exposed() || anim::Disabled() || (!shown && !painted)) {
		remove();
		return;
	}
	const auto alive = QPointer<CardView>(this);
	if (shown) {
		checkOrigin();
		acquireSlot(true);
		if (!alive || _removeQueued) {
			return;
		}
		_busy = true;
		const auto weak = QPointer<CardView>(this);
		const auto guard = gsl::finally([=] {
			if (weak) {
				weak->finishGuard();
			}
		});
		auto parts = grabParts();
		if (!weak) {
			return;
		}
		_parts = std::move(parts);
		_content->hide();
		_scene.rest = _card->geometry().translated(_content->pos());
		_painted = CardViewRestFrame(places());
		_displayed = foreground();
	}
	const auto from = _painted;
	auto old = _displayed.places;
	if (_slotLost) {
		old.slot = std::nullopt;
	}
	_motion.close(from, old);
	_rebasePlacement = _displayed.placement;
	_phase = Phase::Closing;
	_started = 0;
	_elapsed = 0;
	_animation.stop();
	const auto target = places();
	if (_motion.usesSlot() && !ValidCardViewSlot(target)) {
		loseSlot();
	} else if (old.slot != target.slot || old.rest != target.rest) {
		if (_motion.retarget(from, 0, target)) {
			_rebasePlacement = _displayed.placement;
		}
	}
	acquireSlot();
	if (!alive) {
		return;
	}
	if (anim::Disabled()) {
		finishAtOnce();
	}
	update();
}

void CardView::remove(Fn<void()> done) {
	if (_busy) {
		_removeQueued = true;
		_removeDone = std::move(done);
		_animation.stop();
		return;
	}
	const auto stack = this->stack();
	if (!stack || _removing) {
		return;
	}
	_removing = true;
	if (onlyLayer(stack)) {
		stack->hideLayers(anim::type::instant);
	} else {
		closeLayer();
	}
	if (done) {
		done();
	}
}

} // namespace

void ShowCardView(
		not_null<Window::SessionController*> controller,
		not_null<UserData*> user,
		not_null<Info::Profile::BalancePill*> origin) {
	if (&user->session() != &controller->session()
		|| !user->walletCard()
		|| ControllersWithView().contains(controller)) {
		return;
	}
	auto view = std::make_unique<CardView>(controller, user, origin);
	controller->uiShow()->showLayer(
		std::move(view),
		Ui::LayerOption::KeepOther,
		anim::type::instant);
}

} // namespace Wallet
