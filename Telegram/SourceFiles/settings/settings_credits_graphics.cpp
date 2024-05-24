/*
This file is part of Telegram Desktop,
the official desktop application for the Telegram messaging service.

For license and copyright information please follow this link:
https://github.com/telegramdesktop/tdesktop/blob/master/LEGAL
*/
#include "settings/settings_credits_graphics.h"

#include "api/api_credits.h"
#include "boxes/gift_premium_box.h"
#include "core/click_handler_types.h"
#include "data/data_file_origin.h"
#include "data/data_photo_media.h"
#include "data/data_session.h"
#include "data/data_user.h"
#include "info/settings/info_settings_widget.h" // SectionCustomTopBarData.
#include "lang/lang_keys.h"
#include "main/main_session.h"
#include "settings/settings_common_session.h"
#include "statistics/widgets/chart_header_widget.h"
#include "ui/boxes/boost_box.h" // Ui::StartFireworks.
#include "ui/controls/userpic_button.h"
#include "ui/effects/credits_graphics.h"
#include "ui/effects/premium_graphics.h"
#include "ui/effects/premium_top_bar.h"
#include "ui/image/image_prepare.h"
#include "ui/layers/generic_box.h"
#include "ui/painter.h"
#include "ui/rect.h"
#include "ui/text/text_utilities.h"
#include "ui/vertical_list.h"
#include "ui/widgets/labels.h"
#include "ui/widgets/tooltip.h"
#include "ui/wrap/fade_wrap.h"
#include "ui/wrap/padding_wrap.h"
#include "window/window_session_controller.h"
#include "styles/style_credits.h"
#include "styles/style_giveaway.h"
#include "styles/style_info.h"
#include "styles/style_layers.h"
#include "styles/style_premium.h"
#include "styles/style_settings.h"

#include <xxhash.h> // XXH64.

#include <QtSvg/QSvgRenderer>

namespace Settings {
namespace {

class Balance final
	: public Ui::RpWidget
	, public Ui::AbstractTooltipShower {
public:
	using Ui::RpWidget::RpWidget;

	void setBalance(uint64 balance) {
		_balance = balance;
		_tooltip = Lang::FormatCountDecimal(balance);
	}

	void enterEventHook(QEnterEvent *e) override {
		if (_balance >= 10'000) {
			Ui::Tooltip::Show(1000, this);
		}
	}

	void leaveEventHook(QEvent *e) override {
		Ui::Tooltip::Hide();
	}

	QString tooltipText() const override {
		return _tooltip;
	}

	QPoint tooltipPos() const override {
		return QCursor::pos();
	}

	bool tooltipWindowActive() const override {
		return Ui::AppInFocus() && Ui::InFocusChain(window());
	}

private:
	QString _tooltip;
	uint64 _balance = 0;

};

} // namespace

QImage GenerateStars(int height, int count) {
	constexpr auto kOutlineWidth = .6;
	constexpr auto kStrokeWidth = 3;
	constexpr auto kShift = 3;

	auto colorized = qs(Ui::Premium::ColorizedSvg(
		Ui::Premium::CreditsIconGradientStops()));
	colorized.replace(
		u"stroke=\"none\""_q,
		u"stroke=\"%1\""_q.arg(st::creditsStroke->c.name()));
	colorized.replace(
		u"stroke-width=\"1\""_q,
		u"stroke-width=\"%1\""_q.arg(kStrokeWidth));
	auto svg = QSvgRenderer(colorized.toUtf8());
	svg.setViewBox(svg.viewBox() + Margins(kStrokeWidth));

	const auto starSize = Size(height - kOutlineWidth * 2);

	auto frame = QImage(
		QSize(
			(height + kShift * (count - 1)) * style::DevicePixelRatio(),
			height * style::DevicePixelRatio()),
		QImage::Format_ARGB32_Premultiplied);
	frame.setDevicePixelRatio(style::DevicePixelRatio());
	frame.fill(Qt::transparent);
	const auto drawSingle = [&](QPainter &q) {
		const auto s = kOutlineWidth;
		q.save();
		q.translate(s, s);
		q.setCompositionMode(QPainter::CompositionMode_Clear);
		svg.render(&q, QRectF(QPointF(s, 0), starSize));
		svg.render(&q, QRectF(QPointF(s, s), starSize));
		svg.render(&q, QRectF(QPointF(0, s), starSize));
		svg.render(&q, QRectF(QPointF(-s, s), starSize));
		svg.render(&q, QRectF(QPointF(-s, 0), starSize));
		svg.render(&q, QRectF(QPointF(-s, -s), starSize));
		svg.render(&q, QRectF(QPointF(0, -s), starSize));
		svg.render(&q, QRectF(QPointF(s, -s), starSize));
		q.setCompositionMode(QPainter::CompositionMode_SourceOver);
		svg.render(&q, Rect(starSize));
		q.restore();
	};
	{
		auto q = QPainter(&frame);
		q.translate(frame.width() / style::DevicePixelRatio() - height, 0);
		for (auto i = count; i > 0; --i) {
			drawSingle(q);
			q.translate(-kShift, 0);
		}
	}
	return frame;
}

not_null<Ui::RpWidget*> AddBalanceWidget(
		not_null<Ui::RpWidget*> parent,
		rpl::producer<uint64> balanceValue,
		bool rightAlign) {
	const auto balance = Ui::CreateChild<Balance>(parent);
	const auto balanceStar = balance->lifetime().make_state<QImage>(
		GenerateStars(st::creditsBalanceStarHeight, 1));
	const auto starSize = balanceStar->size() / style::DevicePixelRatio();
	const auto label = balance->lifetime().make_state<Ui::Text::String>(
		st::defaultTextStyle,
		tr::lng_credits_summary_balance(tr::now));
	const auto count = balance->lifetime().make_state<Ui::Text::String>(
		st::semiboldTextStyle,
		tr::lng_contacts_loading(tr::now));
	const auto diffBetweenStarAndCount = count->style()->font->spacew;
	const auto resize = [=] {
		balance->resize(
			std::max(
				label->maxWidth(),
				count->maxWidth()
					+ starSize.width()
					+ diffBetweenStarAndCount),
			label->style()->font->height + starSize.height());
	};
	std::move(balanceValue) | rpl::start_with_next([=](uint64 value) {
		count->setText(
			st::semiboldTextStyle,
			Lang::FormatCountToShort(value).string);
		balance->setBalance(value);
		resize();
	}, balance->lifetime());
	balance->paintRequest(
	) | rpl::start_with_next([=] {
		auto p = QPainter(balance);

		p.setPen(st::boxTextFg);

		label->draw(p, {
			.position = QPoint(
				rightAlign ? (balance->width() - label->maxWidth()) : 0,
				0),
			.availableWidth = balance->width(),
		});
		count->draw(p, {
			.position = QPoint(
				balance->width() - count->maxWidth(),
				label->minHeight()
					+ (starSize.height() - count->minHeight()) / 2),
			.availableWidth = balance->width(),
		});
		p.drawImage(
			balance->width()
				- count->maxWidth()
				- starSize.width()
				- diffBetweenStarAndCount,
			label->minHeight(),
			*balanceStar);
	}, balance->lifetime());
	return balance;
}

void ReceiptCreditsBox(
		not_null<Ui::GenericBox*> box,
		not_null<Window::SessionController*> controller,
		PeerData *premiumBot,
		const Data::CreditsHistoryEntry &e) {
	box->setStyle(st::giveawayGiftCodeBox);
	box->setNoContentMargin(true);

	const auto star = GenerateStars(st::creditsTopupButton.height, 1);

	const auto content = box->verticalLayout();
	Ui::AddSkip(content);
	Ui::AddSkip(content);
	Ui::AddSkip(content);

	using Type = Data::CreditsHistoryEntry::PeerType;

	const auto &stUser = st::boostReplaceUserpic;
	const auto peer = (e.peerType == Type::PremiumBot)
		? premiumBot
		: e.bareId
		? controller->session().data().peer(PeerId(e.bareId)).get()
		: nullptr;
	const auto photo = e.photoId
		? controller->session().data().photo(e.photoId).get()
		: nullptr;
	if (photo) {
		content->add(object_ptr<Ui::CenterWrap<>>(
			content,
			HistoryEntryPhoto(content, photo, stUser.photoSize)));
	} else if (peer) {
		content->add(object_ptr<Ui::CenterWrap<>>(
			content,
			object_ptr<Ui::UserpicButton>(content, peer, stUser)));
	} else {
		const auto widget = content->add(
			object_ptr<Ui::CenterWrap<>>(
				content,
				object_ptr<Ui::RpWidget>(content)))->entity();
		using Draw = Fn<void(Painter &, int, int, int, int)>;
		const auto draw = widget->lifetime().make_state<Draw>(
			Ui::GenerateCreditsPaintUserpicCallback(e));
		widget->resize(Size(stUser.photoSize));
		widget->paintRequest(
		) | rpl::start_with_next([=] {
			auto p = Painter(widget);
			(*draw)(p, 0, 0, stUser.photoSize, stUser.photoSize);
		}, widget->lifetime());
	}

	Ui::AddSkip(content);
	Ui::AddSkip(content);


	box->addRow(object_ptr<Ui::CenterWrap<>>(
		box,
		object_ptr<Ui::FlatLabel>(
			box,
			rpl::single(
				!e.title.isEmpty()
				? e.title
				: peer
				? peer->name()
				: Ui::GenerateEntryName(e).text),
			st::creditsBoxAboutTitle)));

	Ui::AddSkip(content);

	{
		constexpr auto kMinus = QChar(0x2212);
		auto &lifetime = content->lifetime();
		const auto text = lifetime.make_state<Ui::Text::String>(
			st::semiboldTextStyle,
			(!e.bareId ? QChar('+') : kMinus)
				+ Lang::FormatCountDecimal(e.credits));

		const auto amount = content->add(
			object_ptr<Ui::FixedHeightWidget>(
				content,
				star.height() / style::DevicePixelRatio()));
		const auto font = text->style()->font;
		amount->paintRequest(
		) | rpl::start_with_next([=] {
			auto p = Painter(amount);
			const auto starWidth = star.width()
				/ style::DevicePixelRatio();
			const auto fullWidth = text->maxWidth()
				+ font->spacew * 2
				+ starWidth;
			p.setPen(!e.bareId
				? st::boxTextFgGood
				: st::menuIconAttentionColor);
			const auto x = (amount->width() - fullWidth) / 2;
			text->draw(p, Ui::Text::PaintContext{
				.position = QPoint(
					x,
					(amount->height() - font->height) / 2),
				.outerWidth = amount->width(),
				.availableWidth = amount->width(),
			});;
			p.drawImage(
				x + fullWidth - starWidth,
				0,
				star);
		}, amount->lifetime());
	}

	if (!e.description.isEmpty()) {
		Ui::AddSkip(content);
		box->addRow(object_ptr<Ui::CenterWrap<>>(
			box,
			object_ptr<Ui::FlatLabel>(
				box,
				rpl::single(e.description),
				st::defaultFlatLabel)));
	}

	Ui::AddSkip(content);
	Ui::AddSkip(content);

	AddCreditsHistoryEntryTable(
		controller,
		box->verticalLayout(),
		e);

	const auto button = box->addButton(tr::lng_box_ok(), [=] {
		box->closeBox();
	});
	const auto buttonWidth = st::boxWidth
		- rect::m::sum::h(st::giveawayGiftCodeBox.buttonPadding);
	button->widthValue() | rpl::filter([=] {
		return (button->widthNoMargins() != buttonWidth);
	}) | rpl::start_with_next([=] {
		button->resizeToWidth(buttonWidth);
	}, button->lifetime());
}

object_ptr<Ui::RpWidget> HistoryEntryPhoto(
		not_null<Ui::RpWidget*> parent,
		not_null<PhotoData*> photo,
		int photoSize) {
	struct State {
		std::shared_ptr<Data::PhotoMedia> view;
		Image *image = nullptr;
		rpl::lifetime downloadLifetime;
	};
	const auto state = parent->lifetime().make_state<State>();
	auto owned = object_ptr<Ui::RpWidget>(parent);
	const auto widget = owned.data();
	state->view = photo->createMediaView();
	photo->load(Data::PhotoSize::Thumbnail, {});

	widget->resize(Size(photoSize));

	rpl::single(rpl::empty_value()) | rpl::then(
		photo->owner().session().downloaderTaskFinished()
	) | rpl::start_with_next([=] {
		using Size = Data::PhotoSize;
		if (const auto large = state->view->image(Size::Large)) {
			state->image = large;
		} else if (const auto small = state->view->image(Size::Small)) {
			state->image = small;
		} else if (const auto t = state->view->image(Size::Thumbnail)) {
			state->image = t;
		}
		widget->update();
		if (state->view->loaded()) {
			state->downloadLifetime.destroy();
		}
	}, state->downloadLifetime);

	widget->paintRequest(
	) | rpl::start_with_next([=] {
		auto p = QPainter(widget);
		if (state->image) {
			p.drawPixmap(0, 0, state->image->pix(widget->width(), {
				.options = Images::Option::RoundCircle,
			}));
		}
	}, widget->lifetime());

	return owned;
}

} // namespace Settings
