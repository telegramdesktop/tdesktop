/*
This file is part of Telegram Desktop,
the official desktop application for the Telegram messaging service.

For license and copyright information please follow this link:
https://github.com/telegramdesktop/tdesktop/blob/master/LEGAL
*/
#include "boxes/gift_premium_box.h"

#include "apiwrap.h"
#include "base/weak_ptr.h"
#include "core/click_handler_types.h" // ClickHandlerContext.
#include "core/local_url_handlers.h" // TryConvertUrlToLocal.
#include "data/data_changes.h"
#include "data/data_peer_values.h" // Data::PeerPremiumValue.
#include "data/data_session.h"
#include "data/data_user.h"
#include "lang/lang_keys.h"
#include "main/main_session.h"
#include "settings/settings_premium.h"
#include "ui/basic_click_handlers.h" // UrlClickHandler::Open.
#include "ui/effects/premium_graphics.h"
#include "ui/effects/premium_stars.h"
#include "ui/layers/generic_box.h"
#include "ui/special_buttons.h"
#include "ui/text/format_values.h"
#include "ui/text/text_utilities.h"
#include "ui/widgets/checkbox.h"
#include "ui/widgets/gradient_round_button.h"
#include "ui/wrap/padding_wrap.h"
#include "window/window_session_controller.h"
#include "styles/style_boxes.h"
#include "styles/style_layers.h"
#include "styles/style_chat_helpers.h"
#include "styles/style_info.h"
#include "styles/style_premium.h"

namespace {

constexpr auto kDiscountDivider = 5.;

struct GiftOption final {
	QString url;
	Ui::Premium::GiftInfo info;
};
using GiftOptions = std::vector<GiftOption>;

GiftOptions GiftOptionFromTL(const MTPDuserFull &data) {
	auto result = GiftOptions();
	const auto gifts = data.vpremium_gifts();
	if (!gifts) {
		return result;
	}
	const auto monthlyAmount = [&] {
		const auto &min = ranges::min_element(
			gifts->v,
			ranges::less(),
			[](const MTPPremiumGiftOption &o) { return o.data().vamount().v; }
		)->data();
		return min.vamount().v / float64(min.vmonths().v);
	}();
	result.reserve(gifts->v.size());
	for (const auto &gift : gifts->v) {
		const auto &option = gift.data();
		const auto botUrl = qs(option.vbot_url());
		const auto months = option.vmonths().v;
		const auto amount = option.vamount().v;
		const auto currency = qs(option.vcurrency());
		const auto discount = [&] {
			const auto percent = monthlyAmount * months / float64(amount)
				- 1.;
			return std::round(percent * 100. / kDiscountDivider)
				* kDiscountDivider;
		}();
		auto info = Ui::Premium::GiftInfo{
			.duration = Ui::FormatTTL(months * 86400 * 31),
			.discount = discount
				? QString::fromUtf8("\xe2\x88\x92%1%").arg(discount)
				: QString(),
			.perMonth = tr::lng_premium_gift_per(
				tr::now,
				lt_cost,
				Ui::FillAmountAndCurrency(
					amount / float64(months),
					currency)),
			.total = Ui::FillAmountAndCurrency(amount, currency),
		};
		result.push_back({ .url = botUrl, .info = std::move(info) });
	}
	return result;
}

class ColoredMiniStars final {
public:
	ColoredMiniStars(not_null<Ui::RpWidget*> parent);

	void setSize(const QSize &size);
	void setPosition(QPoint position);
	void paint(Painter &p);

private:
	Ui::Premium::MiniStars _ministars;
	QRectF _ministarsRect;
	QImage _frame;
	QImage _mask;
	QSize _size;
	QPoint _position;

};

ColoredMiniStars::ColoredMiniStars(not_null<Ui::RpWidget*> parent)
: _ministars([=](const QRect &r) {
	parent->update(r.translated(_position));
}, true) {
}

void ColoredMiniStars::setSize(const QSize &size) {
	_frame = QImage(
		size * style::DevicePixelRatio(),
		QImage::Format_ARGB32_Premultiplied);
	_frame.setDevicePixelRatio(style::DevicePixelRatio());

	_mask = _frame;
	_mask.fill(Qt::transparent);
	{
		Painter p(&_mask);
		auto gradient = QLinearGradient(0, 0, size.width(), 0);
		gradient.setStops(Ui::Premium::GiftGradientStops());
		p.setPen(Qt::NoPen);
		p.setBrush(gradient);
		p.drawRect(0, 0, size.width(), size.height());
	}

	_size = size;

	{
		const auto s = _size / Ui::Premium::MiniStars::kSizeFactor;
		const auto margins = QMarginsF(
			s.width() / 2.,
			s.height() / 2.,
			s.width() / 2.,
			s.height() / 2.);
		_ministarsRect = QRectF(QPointF(), _size) - margins;
	}
}

void ColoredMiniStars::setPosition(QPoint position) {
	_position = std::move(position);
}

void ColoredMiniStars::paint(Painter &p) {
	_frame.fill(Qt::transparent);
	{
		Painter q(&_frame);
		_ministars.paint(q, _ministarsRect);
		q.setCompositionMode(QPainter::CompositionMode_SourceIn);
		q.drawImage(0, 0, _mask);
	}

	p.drawImage(_position, _frame);
}

void GiftBox(
		not_null<Ui::GenericBox*> box,
		not_null<Window::SessionController*> controller,
		not_null<UserData*> user,
		GiftOptions options) {
	const auto boxWidth = st::boxWideWidth;
	box->setWidth(boxWidth);
	box->setNoContentMargin(true);
	const auto buttonsParent = box->verticalLayout().get();

	struct State {
		rpl::event_stream<QString> buttonText;
	};
	const auto state = box->lifetime().make_state<State>();

	const auto userpicPadding = st::premiumGiftUserpicPadding;
	const auto top = box->addRow(object_ptr<Ui::FixedHeightWidget>(
		buttonsParent,
		userpicPadding.top()
			+ userpicPadding.bottom()
			+ st::defaultUserpicButton.size.height()));

	const auto stars = box->lifetime().make_state<ColoredMiniStars>(top);

	const auto userpic = Ui::CreateChild<Ui::UserpicButton>(
		top,
		user,
		Ui::UserpicButton::Role::Custom,
		st::defaultUserpicButton);
	userpic->setAttribute(Qt::WA_TransparentForMouseEvents);
	top->widthValue(
	) | rpl::start_with_next([=](int width) {
		userpic->moveToLeft(
			(width - userpic->width()) / 2,
			userpicPadding.top());

		const auto center = top->rect().center();
		const auto size = QSize(
			userpic->width() * Ui::Premium::MiniStars::kSizeFactor,
			userpic->height());
		const auto ministarsRect = QRect(
			QPoint(center.x() - size.width(), center.y() - size.height()),
			QPoint(center.x() + size.width(), center.y() + size.height()));
		stars->setPosition(ministarsRect.topLeft());
		stars->setSize(ministarsRect.size());
	}, userpic->lifetime());

	top->paintRequest(
	) | rpl::start_with_next([=](const QRect &r) {
		Painter p(top);

		p.fillRect(r, Qt::transparent);
		stars->paint(p);
	}, top->lifetime());

	const auto close = Ui::CreateChild<Ui::IconButton>(
		buttonsParent,
		st::infoTopBarClose);
	close->setClickedCallback([=] { box->closeBox(); });

	buttonsParent->widthValue(
	) | rpl::start_with_next([=](int width) {
		close->moveToRight(0, 0, width);
	}, close->lifetime());

	// Header.
	const auto &padding = st::premiumGiftAboutPadding;
	const auto available = boxWidth - padding.left() - padding.right();
	const auto &stTitle = st::premiumPreviewAboutTitle;
	auto titleLabel = object_ptr<Ui::FlatLabel>(
		box,
		tr::lng_premium_gift_title(),
		stTitle);
	titleLabel->resizeToWidth(available);
	box->addRow(
		object_ptr<Ui::CenterWrap<Ui::FlatLabel>>(
			box,
			std::move(titleLabel)),
		st::premiumGiftTitlePadding);

	auto textLabel = object_ptr<Ui::FlatLabel>(
		box,
		tr::lng_premium_gift_about(
			lt_user,
			user->session().changes().peerFlagsValue(
				user,
				Data::PeerUpdate::Flag::Name
			) | rpl::map([=] { return TextWithEntities{ user->firstName }; }),
			Ui::Text::RichLangValue),
		st::premiumPreviewAbout);
	textLabel->setTextColorOverride(stTitle.textFg->c);
	textLabel->resizeToWidth(available);
	box->addRow(
		object_ptr<Ui::CenterWrap<Ui::FlatLabel>>(box, std::move(textLabel)),
		padding);

	// List.
	const auto group = std::make_shared<Ui::RadiobuttonGroup>();
	group->setChangedCallback([=](int value) {
		Expects(value < options.size() && value >= 0);
		auto text = tr::lng_premium_gift_button(
			tr::now,
			lt_cost,
			options[value].info.total);
		state->buttonText.fire(std::move(text));
	});
	Ui::Premium::AddGiftOptions(
		buttonsParent,
		group,
		ranges::views::all(
			options
		) | ranges::views::transform([](const GiftOption &option) {
			return option.info;
		}) | ranges::to_vector);

	// Footer.
	auto terms = object_ptr<Ui::FlatLabel>(
		box,
		tr::lng_premium_gift_terms(
			lt_link,
			tr::lng_premium_gift_terms_link(
			) | rpl::map([=](const QString &t) {
				return Ui::Text::Link(t, 1);
			}),
			Ui::Text::WithEntities),
		st::premiumGiftTerms);
	terms->setLink(1, std::make_shared<LambdaClickHandler>([=] {
		box->closeBox();
		Settings::ShowPremium(&user->session(), QString());
	}));
	terms->resizeToWidth(available);
	box->addRow(
		object_ptr<Ui::CenterWrap<Ui::FlatLabel>>(box, std::move(terms)),
		st::premiumGiftTermsPadding);

	// Button.
	const auto &stButton = st::premiumGiftBox;
	box->setStyle(stButton);
	auto raw = Settings::CreateSubscribeButton({
		controller,
		box,
		[] { return QString("gift"); },
		state->buttonText.events(),
		Ui::Premium::GiftGradientStops(),
	});
	auto button = object_ptr<Ui::GradientButton>::fromRaw(raw);
	button->resizeToWidth(boxWidth
		- stButton.buttonPadding.left()
		- stButton.buttonPadding.right());
	button->setClickedCallback([=] {
		const auto value = group->value();
		Assert(value < options.size() && value >= 0);

		const auto local = Core::TryConvertUrlToLocal(options[value].url);
		if (local.isEmpty()) {
			return;
		}
		UrlClickHandler::Open(
			local,
			QVariant::fromValue(ClickHandlerContext{
				.sessionWindow = base::make_weak(controller.get()),
				.botStartAutoSubmit = true,
			}));
	});
	box->setShowFinishedCallback([raw = button.data()]{
		raw->startGlareAnimation();
	});
	box->addButton(std::move(button));

	group->setValue(0);

	Data::PeerPremiumValue(
		user
	) | rpl::skip(1) | rpl::start_with_next([=] {
		box->closeBox();
	}, box->lifetime());
}

} // namespace

GiftPremiumValidator::GiftPremiumValidator(
	not_null<Window::SessionController*> controller)
: _controller(controller)
, _api(&_controller->session().mtp()) {
}

void GiftPremiumValidator::cancel() {
	_requestId = 0;
}

void GiftPremiumValidator::showBox(not_null<UserData*> user) {
	if (_requestId) {
		return;
	}
	_requestId = _api.request(MTPusers_GetFullUser(
		user->inputUser
	)).done([=](const MTPusers_UserFull &result) {
		if (!_requestId) {
			// Canceled.
			return;
		}
		_requestId = 0;
//		_controller->api().processFullPeer(peer, result);
		_controller->session().data().processUsers(result.data().vusers());
		_controller->session().data().processChats(result.data().vchats());

		const auto &fullUser = result.data().vfull_user().data();
		auto options = GiftOptionFromTL(fullUser);
		if (!options.empty()) {
			_controller->show(
				Box(GiftBox, _controller, user, std::move(options)));
		}
	}).fail([=] {
		_requestId = 0;
	}).send();
}
