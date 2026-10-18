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
#include "ui/effects/layer_backdrop.h"
#include "ui/layers/layer_widget.h"
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

struct LabelInk {
	QColor color;
	float64 plate = 0.;
};

struct Scene {
	QImage sharp;
	QImage empty;
	std::optional<QRect> slot;
};

struct Parts {
	QImage card;
	QImage button;
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
	[[nodiscard]] QImage captureSharp();
	[[nodiscard]] CardViewPlaces places() const;
	[[nodiscard]] Parts grabParts() const;
	[[nodiscard]] bool holding() const;
	[[nodiscard]] bool canActivate() const;
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
	void acceptScene(QImage sharp);
	void acquireSlot();
	bool checkOrigin();
	void loseSlot();
	void paintTransition(QPainter &p, const CardViewFrame &frame);
	void paintSlot(QPainter &p, float64 lift);
	void paintLabel(QPainter &p, float64 opacity);
	void paintButton(QPainter &p, float64 progress) const;
	void paintCard(QPainter &p, const CardViewFrame &frame) const;
	void startShowing();
	void finishShowing();
	void finishAtOnce();
	void releaseHeld();
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
	Ui::RpWidget *_card = nullptr;
	Ui::RoundButton *_button = nullptr;
	Ui::FlatLabel *_label = nullptr;
	Scene _scene;
	Parts _parts;
	CardViewMotion _motion;
	CardViewFrame _painted;
	LabelInk _labelInk;
	crl::time _started = 0;
	crl::time _elapsed = 0;
	Phase _phase = Phase::Preparing;
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
	p.setOpacity(fold.opacity);
	p.setTransform(fold.transform, true);
	stickers->paint(p, fold.rest);
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

void PaintLabelPlate(QPainter &p, QRect plate, float64 level) {
	auto hq = PainterHighQualityEnabler(p);
	p.setPen(Qt::NoPen);
	p.setBrush(PlateColor(level));
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
	const auto started = _backdrop.started();
	if (!started) {
		prepare();
	}
	layoutContent();
	if (started) {
		queueRefresh();
	}
}

void CardView::showFinished() {
	if (base::take(_captureDeferred)) {
		_backdrop.invalidate();
	}
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
	auto p = QPainter(this);
	if (_phase == Phase::Shown) {
		_backdrop.paint(p);
		return;
	} else if (_phase == Phase::Preparing
		|| (_phase == Phase::Opening && !_started && !checkOrigin())) {
		if (!_scene.sharp.isNull()) {
			p.drawImage(rect(), _scene.sharp);
		}
		return;
	} else if (!_started) {
		// WHY: the clock starts at the first painted frame, so that frame
		// is the start pose however long the capture and its paint took.
		_started = crl::now();
		_elapsed = 0;
		_animation.start();
		if (_phase == Phase::Closing) {
			releaseHeld();
		}
	}
	checkOrigin();
	_painted = _motion.frame(_elapsed, places());
	paintTransition(p, _painted);
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
		return EnclosingLayer(origin);
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
	auto result = captureSharp();
	if (result.isNull() || _phase == Phase::Shown) {
		return result;
	} else if (holding()) {
		queueRefresh();
	} else {
		acceptScene(result);
	}
	return result;
}

QImage CardView::captureSharp() {
	if (covered()) {
		_captureDeferred = true;
		return QImage();
	}
	auto result = Ui::CaptureUnderLayer(this, underlyingLayer());
	if (result.isNull() && _backdrop.started()) {
		crl::on_main(this, [=] {
			_backdrop.invalidate();
		});
	}
	return result;
}

CardViewPlaces CardView::places() const {
	return {
		.rest = _card->geometry().translated(_content->pos()),
		.slot = _scene.slot,
	};
}

Parts CardView::grabParts() const {
	return {
		.card = Ui::GrabWidgetToImage(_card),
		.button = (_button->isHidden()
			? QImage()
			: Ui::GrabWidgetToImage(_button)),
	};
}

bool CardView::holding() const {
	return (_phase == Phase::Closing) && !_started;
}

bool CardView::canActivate() const {
	const auto session = &_user->session();
	if (&_controller->session() != session) {
		return false;
	} else if (_user->isSelf()) {
		return !session->supportMode()
			&& session->appConfig().walletAvailable();
	}
	return (session->data().userLoaded(peerToUser(_user->id)) == _user)
		&& CanOfferSendMoney(_user);
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
		PaintLabelPlate(p, labelPlate().translated(origin), plate);
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
	if (_user->isSelf()) {
		setupLabel();
	}

	_content->paintRequest(
	) | rpl::on_next([=] {
		if (_label && _labelInk.plate > 0.) {
			auto p = QPainter(_content);
			PaintLabelPlate(p, labelPlate(), _labelInk.plate);
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
	_button->setVisible(canActivate());
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
		wallet.userAddresses().unavailableValue() | rpl::to_empty,
		_user->flagsValue() | rpl::to_empty,
		session->changes().peerUpdates(
			_user,
			Flag::FullInfo | Flag::Name | Flag::SupportInfo | Flag::IsBot
		) | rpl::to_empty
	) | rpl::on_next([=] {
		refreshButton();
	}, _button->lifetime());

	(_user->isSelf()
		? tr::lng_wallet_details_open_wallet()
		: tr::lng_wallet_profile_send_money()
	) | rpl::on_next([=](const QString &text) {
		_button->setText(rpl::single(text));
		contentResized();
	}, _button->lifetime());
}

void CardView::setupLabel() {
	_label = Ui::CreateChild<Ui::FlatLabel>(
		_content.get(),
		st::walletCardViewLabel);
	_label->setAttribute(Qt::WA_TransparentForMouseEvents);

	tr::lng_wallet_profile_balance_public(
	) | rpl::on_next([=](const QString &text) {
		_label->setText(text);
		contentResized();
	}, _label->lifetime());
}

void CardView::prepare() {
	_backdrop.start();
	if (!_backdrop.started()) {
		crl::on_main(this, [=] {
			remove();
		});
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
		_label->resizeToWidth(st::walletCardViewLabelWidth);
		if (skip + above() > till) {
			_label->resizeToWidth(
				std::max(cardWidth, st::walletCardViewLabelWidth));
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
}

void CardView::contentResized() {
	if (!_placing) {
		layoutContent();
		queueParts();
	}
}

void CardView::refreshButton() {
	const auto shown = canActivate();
	if (_button->isHidden() == !shown) {
		return;
	}
	_button->setVisible(shown);
	layoutContent();
	queueParts();
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
	const auto was = _labelInk.plate;
	auto ink = computeLabelInk();
	if ((ink.plate > 0.) != (was > 0.)) {
		_labelInk.plate = ink.plate;
		placeContent();
		ink = computeLabelInk();
	}
	_labelInk = ink;
	_label->setTextColorOverride(ink.color);
	if (ink.plate != was) {
		_content->update();
	}
}

void CardView::acceptScene(QImage sharp) {
	const auto origin = _origin.data();
	const auto slot = UsableSlot(this, origin);
	_scene = {
		.sharp = std::move(sharp),
		.empty = slot ? origin->grabEmpty() : QImage(),
		.slot = slot,
	};
	if (_phase == Phase::Opening && !_started) {
		_motion.open(places());
	} else if (!ValidCardViewSlot(places())) {
		loseSlot();
	}
	update();
}

void CardView::acquireSlot() {
	if (UsableSlot(this, _origin.data()) == _scene.slot) {
		return;
	}
	auto sharp = captureSharp();
	if (sharp.isNull()) {
		_scene.slot = std::nullopt;
	} else {
		acceptScene(std::move(sharp));
	}
}

bool CardView::checkOrigin() {
	const auto live = UsableSlot(this, _origin.data());
	if (live == _scene.slot) {
		return true;
	} else if (!live) {
		loseSlot();
		_scene.slot = std::nullopt;
		if (_phase == Phase::Opening && !_started) {
			_motion.open(places());
		}
	}
	if (covered()) {
		_captureDeferred = true;
	} else {
		queueRefresh();
	}
	return !_scene.slot;
}

void CardView::loseSlot() {
	if (_started && _motion.usesSlot()) {
		_motion.loseSlot(_painted, _elapsed);
	}
}

void CardView::paintTransition(
		QPainter &p,
		const CardViewFrame &frame) {
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
	paintLabel(p, frame.label);
	p.setOpacity(1.);
	paintButton(p, frame.button);
	paintCard(p, frame);
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

void CardView::paintLabel(QPainter &p, float64 opacity) {
	if (!_label || opacity <= 0.) {
		return;
	}
	const auto origin = _content->pos();
	if (_labelInk.plate > 0.) {
		p.setOpacity(opacity);
		PaintLabelPlate(p, labelPlate().translated(origin), _labelInk.plate);
	}
	p.setOpacity(1.);
	_label->setOpacity(opacity);
	Ui::RenderWidget(p, _label, _label->pos() + origin);
}

void CardView::paintButton(QPainter &p, float64 progress) const {
	const auto shown = FlightEaseOut(progress);
	if (_parts.button.isNull() || shown <= 0.) {
		return;
	}
	const auto rest = _button->geometry().translated(_content->pos());
	p.save();
	if (shown < 1.) {
		const auto center = QRectF(rest).center();
		const auto rise = kButtonRise * _card->width() * (1. - shown);
		const auto scale = kButtonScale + (1. - kButtonScale) * shown;
		p.setRenderHint(QPainter::SmoothPixmapTransform);
		p.setOpacity(shown);
		p.translate(center.x(), center.y() + rise);
		p.scale(scale, scale);
		p.translate(-center.x(), -center.y());
	}
	p.drawImage(rest.topLeft(), _parts.button);
	p.restore();
}

void CardView::paintCard(QPainter &p, const CardViewFrame &frame) const {
	const auto &image = _parts.card;
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
	}
	_elapsed = std::max(now - _started, crl::time());
	checkOrigin();
	update();
	const auto frame = _motion.frame(_elapsed, places());
	if (anim::Disabled() || frame.finished) {
		if (_phase == Phase::Opening) {
			finishShowing();
		} else {
			Ui::PostponeCall(this, [=] {
				remove();
			});
		}
		return false;
	} else if (frame.card.opacity > 0.) {
		// Hidden card children, like the look's emoji, repaint unannounced.
		_parts.card = Ui::GrabWidgetToImage(_card);
	}
	return true;
}

void CardView::startShowing() {
	if (anim::Disabled()) {
		finishShowing();
		return;
	}
	acquireSlot();
	_parts = grabParts();
	_motion.open(places());
	_phase = Phase::Opening;
	update();
}

void CardView::finishShowing() {
	_phase = Phase::Shown;
	_started = 0;
	_animation.stop();
	_scene = Scene();
	_parts = Parts();
	if (_label) {
		_label->setOpacity(1.);
	}
	_content->show();
	update();
}

void CardView::finishAtOnce() {
	if (_phase == Phase::Closing) {
		_animation.stop();
		Ui::PostponeCall(this, [=] {
			remove();
		});
	} else if (_phase == Phase::Opening
		|| (_phase == Phase::Preparing && _backdrop.started())) {
		finishShowing();
	}
}

void CardView::releaseHeld() {
	if (base::take(_refreshQueued)) {
		_partsQueued = false;
		queueRefresh();
	} else if (base::take(_partsQueued)) {
		queueParts();
	}
}

void CardView::refreshParts() {
	if (!_parts.card.isNull()) {
		_parts = grabParts();
		update();
	}
}

void CardView::queueParts() {
	if (_parts.card.isNull() || _partsQueued) {
		return;
	}
	_partsQueued = true;
	crl::on_main(this, [=] {
		if (!holding() && base::take(_partsQueued)) {
			refreshParts();
		}
	});
}

void CardView::queueRefresh() {
	if (_phase == Phase::Shown || _refreshQueued) {
		return;
	}
	_refreshQueued = true;
	crl::on_main(this, [=] {
		if (!holding() && base::take(_refreshQueued)) {
			refreshTransition();
		}
	});
}

void CardView::refreshTransition() {
	if (_phase == Phase::Shown || _removing) {
		return;
	}
	auto sharp = captureSharp();
	if (!sharp.isNull()) {
		acceptScene(std::move(sharp));
	}
	refreshParts();
}

void CardView::activate() {
	if (_phase != Phase::Shown || _removing) {
		return;
	} else if (!canActivate()) {
		refreshButton();
		return;
	}
	const auto user = _user;
	const auto userId = peerToUser(user->id);
	const auto own = user->isSelf();
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
		} else if (own) {
			ShowWallet(session);
		} else if (session->data().userLoaded(userId) == user) {
			OpenSendMoney(controller, user);
		}
	});
}

void CardView::closeView() {
	if (_phase == Phase::Closing || _removing) {
		return;
	}
	const auto shown = (_phase == Phase::Shown);
	const auto painted = (_phase == Phase::Opening) && (_started != 0);
	_phase = Phase::Closing;
	_started = 0;
	_animation.stop();
	if (covered() || anim::Disabled() || (!shown && !painted)) {
		remove();
		return;
	}
	auto from = _painted;
	auto target = places();
	if (shown) {
		auto sharp = Ui::CaptureUnderLayer(this, underlyingLayer());
		if (sharp.isNull()) {
			remove();
			return;
		}
		acceptScene(std::move(sharp));
		_parts = grabParts();
		_content->hide();
		target = places();
		from = CardViewRestFrame(target);
		_painted = from;
	} else if (!UsableSlot(this, _origin.data())) {
		target.slot = std::nullopt;
	}
	_motion.close(from, target);
	update();
}

void CardView::remove(Fn<void()> done) {
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
