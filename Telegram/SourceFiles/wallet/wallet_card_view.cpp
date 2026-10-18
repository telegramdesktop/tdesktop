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
#include "wallet/wallet_card_look.h"
#include "wallet/wallet_card_stickers.h"
#include "wallet/wallet_card_tint.h"
#include "wallet/wallet_content.h"
#include "wallet/wallet_fiat.h"
#include "wallet/wallet_panel.h"
#include "wallet/wallet_session.h"
#include "wallet/wallet_user_addresses.h"
#include "window/window_session_controller.h"

#include "styles/style_wallet.h"

namespace Wallet {
namespace {

constexpr auto kFadeDuration = crl::time(200);
constexpr auto kButtonFillLevel = 0.5;
constexpr auto kButtonRippleLevel = 1.;
constexpr auto kInkSteps = 255;
constexpr auto kPreferredContrast = 4.6;
constexpr auto kRequiredContrast = 4.5;
constexpr auto kPlateLevels = std::array{ 0., 0.25, 0.5, 0.75, 1. };

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

class CardView final : public Ui::LayerWidget {
public:
	CardView(
		not_null<Window::SessionController*> controller,
		not_null<UserData*> user,
		not_null<QWidget*> origin);
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
	[[nodiscard]] float64 opacity() const;
	[[nodiscard]] QImage captureScene();
	[[nodiscard]] QImage compose() const;
	[[nodiscard]] bool canActivate() const;
	[[nodiscard]] QRect labelPlate() const;
	[[nodiscard]] QImage sampleLabelBackground(float64 plate) const;
	[[nodiscard]] LabelInk computeLabelInk() const;

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
	void startShowing();
	void finishShowing();
	void refreshComposed();
	void queueComposed();
	void queueRefresh();
	void refreshTransition();
	void activate();
	void closeView(Fn<void()> route = nullptr);
	void remove(Fn<void()> done = nullptr);

	const not_null<Window::SessionController*> _controller;
	const not_null<UserData*> _user;
	const QPointer<QWidget> _origin;
	const not_null<Ui::RpWidget*> _content;
	Ui::LayerBackdrop _backdrop;
	Ui::Animations::Simple _fade;
	Fn<void()> _route;
	rpl::variable<Data::WalletCard> _data;
	rpl::variable<CardLook> _look;
	Ui::RpWidget *_card = nullptr;
	Ui::RoundButton *_button = nullptr;
	Ui::FlatLabel *_label = nullptr;
	QImage _sharp;
	QImage _composed;
	LabelInk _labelInk;
	Phase _phase = Phase::Preparing;
	bool _captureDeferred = false;
	bool _refreshQueued = false;
	bool _composeQueued = false;
	bool _placing = false;
	bool _removing = false;
	std::unique_ptr<CardStickers> _stickers;

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
	not_null<QWidget*> origin)
: _controller(controller)
, _user(user)
, _origin(origin.get())
, _content(Ui::CreateChild<Ui::RpWidget>(this))
, _backdrop(this, [=] { return captureScene(); }, OverlayAlpha)
, _data(*user->walletCard()) {
	ControllersWithView().emplace(_controller);
	_content->hide();
	setupContent();

	_backdrop.changes(
	) | rpl::on_next([=] {
		refreshLabelInk();
		refreshComposed();
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
	}
	if (!_sharp.isNull()) {
		p.drawImage(rect(), _sharp);
	}
	const auto opacity = this->opacity();
	if (!_composed.isNull() && opacity > 0.) {
		p.setOpacity(opacity);
		p.drawImage(rect(), _composed);
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

float64 CardView::opacity() const {
	return (_phase == Phase::Preparing)
		? 0.
		: (_phase == Phase::Shown)
		? 1.
		: _fade.value((_phase == Phase::Opening) ? 1. : 0.);
}

QImage CardView::captureScene() {
	if (covered()) {
		_captureDeferred = true;
		return QImage();
	}
	auto result = Ui::CaptureUnderLayer(this, underlyingLayer());
	if (result.isNull()) {
		if (_backdrop.started()) {
			crl::on_main(this, [=] {
				_backdrop.invalidate();
			});
		}
	} else if (_phase != Phase::Shown) {
		_sharp = result;
	}
	return result;
}

QImage CardView::compose() const {
	const auto ratio = style::DevicePixelRatio();
	auto result = QImage(
		size() * ratio,
		QImage::Format_ARGB32_Premultiplied);
	result.setDevicePixelRatio(ratio);
	auto p = QPainter(&result);
	_backdrop.paint(p);
	Ui::RenderWidget(p, _content, _content->pos());
	p.end();
	return result;
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
		queueComposed();
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
			queueComposed();
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
		queueComposed();
	}
}

void CardView::refreshButton() {
	const auto shown = canActivate();
	if (_button->isHidden() == !shown) {
		return;
	}
	_button->setVisible(shown);
	layoutContent();
	queueComposed();
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
	queueComposed();
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

void CardView::startShowing() {
	if (anim::Disabled()) {
		finishShowing();
		return;
	}
	_phase = Phase::Opening;
	_composed = compose();
	_fade.start([=] {
		update();
		if (!_fade.animating() && _phase == Phase::Opening) {
			finishShowing();
		}
	}, 0., 1., kFadeDuration);
}

void CardView::finishShowing() {
	_phase = Phase::Shown;
	_sharp = QImage();
	_composed = QImage();
	_content->show();
	update();
}

void CardView::refreshComposed() {
	if (!_composed.isNull()) {
		_composed = compose();
		update();
	}
}

void CardView::queueComposed() {
	if (_composed.isNull() || _composeQueued) {
		return;
	}
	_composeQueued = true;
	crl::on_main(this, [=] {
		_composeQueued = false;
		refreshComposed();
	});
}

void CardView::queueRefresh() {
	if (_phase == Phase::Shown || _refreshQueued) {
		return;
	}
	_refreshQueued = true;
	crl::on_main(this, [=] {
		_refreshQueued = false;
		refreshTransition();
	});
}

void CardView::refreshTransition() {
	if (_phase == Phase::Shown || _removing) {
		return;
	}
	auto sharp = covered()
		? QImage()
		: Ui::CaptureUnderLayer(this, underlyingLayer());
	if (!sharp.isNull()) {
		_sharp = std::move(sharp);
	}
	refreshComposed();
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
	closeView([=] {
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

void CardView::closeView(Fn<void()> route) {
	if (_phase == Phase::Closing || _removing) {
		return;
	}
	const auto from = opacity();
	_phase = Phase::Closing;
	if (covered() || anim::Disabled() || from <= 0.) {
		remove(std::move(route));
		return;
	}
	auto sharp = Ui::CaptureUnderLayer(this, underlyingLayer());
	if (sharp.isNull()) {
		remove(std::move(route));
		return;
	}
	_route = std::move(route);
	_sharp = std::move(sharp);
	if (_composed.isNull()) {
		_composed = compose();
	}
	_content->hide();
	_fade.start([=] {
		update();
		if (!_fade.animating()) {
			remove(base::take(_route));
		}
	}, from, 0., crl::time(kFadeDuration * from));
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
		not_null<QWidget*> origin) {
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
