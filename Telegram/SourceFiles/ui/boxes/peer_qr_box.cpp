/*
This file is part of Telegram Desktop,
the official desktop application for the Telegram messaging service.

For license and copyright information please follow this link:
https://github.com/telegramdesktop/tdesktop/blob/master/LEGAL
*/
#include "ui/boxes/peer_qr_box.h"

#include "core/application.h"
#include "data/data_cloud_themes.h"
#include "data/data_peer.h"
#include "data/data_session.h"
#include "data/data_user.h"
#include "info/channel_statistics/boosts/giveaway/boost_badge.h" // InfiniteRadialAnimationWidget.
#include "info/profile/info_profile_values.h"
#include "lang/lang_keys.h"
#include "main/main_session.h"
#include "qr/qr_generate.h"
#include "ui/controls/userpic_button.h"
#include "ui/dynamic_image.h"
#include "ui/dynamic_thumbnails.h"
#include "ui/effects/animations.h"
#include "ui/image/image_prepare.h"
#include "ui/layers/generic_box.h"
#include "ui/painter.h"
#include "ui/rect.h"
#include "ui/ui_utility.h"
#include "ui/vertical_list.h"
#include "ui/widgets/box_content_divider.h"
#include "ui/widgets/buttons.h"
#include "ui/widgets/continuous_sliders.h"
#include "ui/wrap/vertical_layout.h"
#include "window/window_controller.h"
#include "window/window_session_controller.h"
#include "styles/style_boxes.h"
#include "styles/style_giveaway.h"
#include "styles/style_credits.h"
#include "styles/style_intro.h"
#include "styles/style_layers.h"
#include "styles/style_settings.h"
#include "styles/style_widgets.h"
#include "styles/style_window.h"

#include <QtCore/QMimeData>
#include <QtGui/QGuiApplication>
#include <QtSvg/QSvgRenderer>

namespace Ui {
namespace {

using Colors = std::vector<QColor>;

[[nodiscard]] QMargins NoPhotoBackgroundMargins() {
	return QMargins(
		st::profileQrBackgroundMargins.left(),
		st::profileQrBackgroundMargins.left(),
		st::profileQrBackgroundMargins.right(),
		st::profileQrBackgroundMargins.bottom());
}

[[nodiscard]] style::font CreateFont(int size, int scale) {
	return style::font(
		style::ConvertScale(size, scale),
		st::profileQrFont->flags(),
		st::profileQrFont->family());
}

[[nodiscard]] QImage TelegramQr(
		const Qr::Data &data,
		int pixel,
		int max,
		bool hasWhiteBackground) {
	Expects(data.size > 0);

	constexpr auto kCenterRatio = 0.175;

	if (max > 0 && data.size * pixel > max) {
		pixel = std::max(max / data.size, 1);
	}
	auto qr = Qr::Generate(
		data,
		pixel * style::DevicePixelRatio(),
		hasWhiteBackground ? Qt::transparent : Qt::black,
		hasWhiteBackground ? Qt::white : Qt::transparent);
	{
		auto p = QPainter(&qr);
		auto hq = PainterHighQualityEnabler(p);
		auto svg = QSvgRenderer(u":/gui/plane_white.svg"_q);
		const auto size = qr.rect().size();
		const auto centerRect = Rect(size)
			- Margins((size.width() - (size.width() * kCenterRatio)) / 2);
		p.setPen(Qt::NoPen);
		p.setBrush(Qt::white);
		if (hasWhiteBackground) {
			p.setCompositionMode(QPainter::CompositionMode_Clear);
			p.drawEllipse(centerRect);
			p.setCompositionMode(QPainter::CompositionMode_SourceOver);
			svg.render(&p, centerRect);
		} else {
			p.drawEllipse(centerRect);
			p.setCompositionMode(QPainter::CompositionMode_Clear);
			svg.render(&p, centerRect);
		}
	}
	return qr;
}

[[nodiscard]] QMargins RoundedMargins(
		const QMargins &backgroundMargins,
		int photoSize,
		int textMaxHeight) {
	return (textMaxHeight
		? (backgroundMargins + QMargins(0, photoSize / 2, 0, textMaxHeight))
		: photoSize
		? backgroundMargins + QMargins(0, photoSize / 2, 0, photoSize / 2)
		: Margins(backgroundMargins.left()));
}

void Paint(
		QPainter &p,
		const style::font &font,
		const QString &text,
		const Colors &backgroundColors,
		const QMargins &backgroundMargins,
		const QImage &qrImage,
		const QRect &qrRect,
		int qrMaxSize,
		int qrPixel,
		int radius,
		int textMaxHeight,
		int photoSize,
		bool hasWhiteBackground) {
	auto hq = PainterHighQualityEnabler(p);
	p.setPen(Qt::NoPen);
	p.setBrush(hasWhiteBackground ? Qt::white : Qt::transparent);
	const auto roundedRect = qrRect
		+ RoundedMargins(backgroundMargins, photoSize, textMaxHeight);
	p.drawRoundedRect(roundedRect, radius, radius);
	if (!qrImage.isNull() && !backgroundColors.empty()) {
		constexpr auto kDuration = crl::time(10000);
		const auto angle = (crl::now() % kDuration)
			/ float64(kDuration) * 360.0;
		const auto gradientRotation = int(angle / 45.) * 45;
		const auto gradientRotationAdd = angle - gradientRotation;

		const auto textAdditionalWidth = backgroundMargins.left();
		auto back = Images::GenerateGradient(
			qrRect.size() + QSize(textAdditionalWidth, 0),
			backgroundColors,
			gradientRotation,
			1. - (gradientRotationAdd / 45.));
		if (hasWhiteBackground) {
			p.drawImage(qrRect, back);
		}
		const auto coloredSize = QSize(back.width(), textMaxHeight);
		auto colored = QImage(
			coloredSize * style::DevicePixelRatio(),
			QImage::Format_ARGB32_Premultiplied);
		colored.setDevicePixelRatio(style::DevicePixelRatio());
		colored.fill(Qt::transparent);
		if (textMaxHeight) {
			// '@' + QString(32, 'W');
			auto p = QPainter(&colored);
			auto hq = PainterHighQualityEnabler(p);
			p.setPen(Qt::black);
			p.setFont(font);
			auto option = QTextOption(style::al_center);
			option.setWrapMode(QTextOption::WrapAnywhere);
			p.drawText(Rect(coloredSize), text, option);
			p.setCompositionMode(QPainter::CompositionMode_SourceIn);
			p.drawImage(0, -back.height() + textMaxHeight, back);
		}
		if (!hasWhiteBackground) {
			auto copy = qrImage;
			{
				auto p = QPainter(&copy);
				p.setCompositionMode(QPainter::CompositionMode_SourceIn);
				p.drawImage(Rect(copy.size()), back);
			}
			p.drawImage(qrRect, copy);
		} else {
			p.drawImage(qrRect, qrImage);
		}
		if (textMaxHeight) {
			p.drawImage(
				qrRect.x() - textAdditionalWidth / 2,
				rect::bottom(qrRect)
					+ ((rect::bottom(roundedRect) - rect::bottom(qrRect))
						- textMaxHeight) / 2,
				colored);
		}
	}
}

not_null<Ui::RpWidget*> PrepareQrWidget(
		not_null<Ui::VerticalLayout*> container,
		not_null<Ui::RpWidget*> topWidget,
		rpl::producer<int> fontSizeValue,
		rpl::producer<bool> userpicToggled,
		rpl::producer<bool> backgroundToggled,
		rpl::producer<QString> username,
		rpl::producer<QString> links,
		rpl::producer<Colors> bgs,
		rpl::producer<QString> about) {
	const auto divider = container->add(
		object_ptr<Ui::BoxContentDivider>(container));
	struct State final {
		explicit State(Fn<void()> callback) : updating(callback) {
			updating.start();
		}

		Ui::Animations::Basic updating;
		style::font font;
		QImage qrImage;
		Colors backgroundColors;
		QString text;
		QMargins backgroundMargins;
		int textWidth = 0;
		int textMaxHeight = 0;
		int photoSize = 0;
		bool backgroundToggled = false;
	};
	const auto result = Ui::CreateChild<Ui::RpWidget>(divider);
	topWidget->setParent(result);
	topWidget->setAttribute(Qt::WA_TransparentForMouseEvents);
	const auto state = result->lifetime().make_state<State>(
		[=] { result->update(); });
	const auto qrMaxSize = st::boxWideWidth
		- rect::m::sum::h(st::boxRowPadding)
		- rect::m::sum::h(st::profileQrBackgroundMargins);
	const auto aboutLabel = Ui::CreateChild<Ui::FlatLabel>(
		divider,
		st::creditsBoxAboutDivider);
	rpl::combine(
		std::move(fontSizeValue),
		std::move(userpicToggled),
		std::move(backgroundToggled),
		std::move(username),
		std::move(bgs),
		std::move(links),
		std::move(about),
		rpl::single(rpl::empty) | rpl::then(style::PaletteChanged())
	) | rpl::start_with_next([=](
			int fontSize,
			bool userpicToggled,
			bool backgroundToggled,
			const QString &username,
			const Colors &backgroundColors,
			const QString &link,
			const QString &about,
			const auto &) {
		state->font = CreateFont(fontSize, style::Scale());
		state->backgroundToggled = backgroundToggled;
		state->backgroundMargins = userpicToggled
			? st::profileQrBackgroundMargins
			: NoPhotoBackgroundMargins();
		state->photoSize = userpicToggled
			? st::defaultUserpicButton.photoSize
			: 0;
		state->backgroundColors = backgroundColors;
		state->text = username.toUpper();
		state->textWidth = state->font->width(state->text);
		{
			const auto remainder = qrMaxSize % st::introQrPixel;
			const auto downTo = remainder
				? qrMaxSize - remainder
				: qrMaxSize;
			state->qrImage = TelegramQr(
				Qr::Encode(link.toUtf8(), Qr::Redundancy::Default),
				st::introQrPixel,
				downTo,
				backgroundToggled).scaled(
					Size(qrMaxSize * style::DevicePixelRatio()),
					Qt::IgnoreAspectRatio,
					Qt::SmoothTransformation);
		}
		const auto resultWidth = qrMaxSize
			+ rect::m::sum::h(state->backgroundMargins);
		{
			aboutLabel->setText(about);
			aboutLabel->resizeToWidth(resultWidth);
		}
		const auto textMaxWidth = state->backgroundMargins.left()
			+ (state->qrImage.width() / style::DevicePixelRatio());
		const auto lines = int(state->textWidth / textMaxWidth) + 1;
		state->textMaxHeight = state->textWidth
			? (state->font->height * lines)
			: 0;
		const auto whiteMargins = RoundedMargins(
			state->backgroundMargins,
			state->photoSize,
			state->textMaxHeight);
		result->resize(
			qrMaxSize + rect::m::sum::h(whiteMargins),
			qrMaxSize
				+ rect::m::sum::v(whiteMargins) // White.
				+ rect::m::sum::v(st::profileQrBackgroundPadding) // Gray.
				+ state->photoSize / 2
				+ aboutLabel->height());

		divider->resize(container->width(), result->height());
		result->moveToLeft((container->width() - result->width()) / 2, 0);
		topWidget->setVisible(userpicToggled);
		topWidget->moveToLeft(0, -std::numeric_limits<int>::min());
		topWidget->raise();

		aboutLabel->raise();
		aboutLabel->moveToLeft(
			result->x(),
			divider->height()
				- aboutLabel->height()
				- st::defaultBoxDividerLabelPadding.top());
	}, container->lifetime());
	result->paintRequest(
	) | rpl::start_with_next([=](QRect clip) {
		auto p = QPainter(result);
		const auto size = (state->qrImage.size() / style::DevicePixelRatio());
		const auto qrRect = Rect(
			(result->width() - size.width()) / 2,
			state->backgroundMargins.top() + state->photoSize / 2,
			size);
		p.translate(
			0,
			st::profileQrBackgroundPadding.top() + state->photoSize / 2);
		Paint(
			p,
			state->font,
			state->text,
			state->backgroundColors,
			state->backgroundMargins,
			state->qrImage,
			qrRect,
			qrMaxSize,
			st::introQrPixel,
			st::profileQrBackgroundRadius,
			state->textMaxHeight,
			state->photoSize,
			state->backgroundToggled);
		if (!state->photoSize) {
			return;
		}
		const auto photoSize = state->photoSize;
		const auto top = Ui::GrabWidget(
			topWidget,
			QRect(),
			Qt::transparent).scaled(
				Size(photoSize * style::DevicePixelRatio()),
				Qt::IgnoreAspectRatio,
				Qt::SmoothTransformation);
		p.drawPixmap((result->width() - photoSize) / 2, -photoSize / 2, top);
	}, result->lifetime());
	return result;
}

[[nodiscard]] Fn<void(int)> AddDotsToSlider(
		not_null<Ui::ContinuousSlider*> slider,
		const style::MediaSlider &st,
		int count) {
	const auto lineWidth = st::lineWidth;
	const auto smallSize = Size(st.seekSize.height() - st.width);
	auto smallDots = std::vector<not_null<Ui::RpWidget*>>();
	smallDots.reserve(count - 1);
	const auto paintSmall = [=](QPainter &p, const QBrush &brush) {
		auto hq = PainterHighQualityEnabler(p);
		auto pen = st::boxBg->p;
		pen.setWidth(st.width);
		p.setPen(pen);
		p.setBrush(brush);
		p.drawEllipse(Rect(smallSize) - Margins(lineWidth));
	};
	for (auto i = 0; i < count - 1; i++) {
		smallDots.push_back(
			Ui::CreateChild<Ui::RpWidget>(slider->parentWidget()));
		const auto dot = smallDots.back();
		dot->resize(smallSize);
		dot->setAttribute(Qt::WA_TransparentForMouseEvents);
		dot->paintRequest() | rpl::start_with_next([=] {
			auto p = QPainter(dot);
			const auto fg = (slider->value() > (i / float64(count - 1)))
				? st.activeFg
				: st.inactiveFg;
			paintSmall(p, fg);
		}, dot->lifetime());
	}
	const auto bigDot = Ui::CreateChild<Ui::RpWidget>(slider->parentWidget());
	bigDot->resize(st.seekSize);
	bigDot->setAttribute(Qt::WA_TransparentForMouseEvents);
	bigDot->paintRequest() | rpl::start_with_next([=] {
		auto p = QPainter(bigDot);
		auto hq = PainterHighQualityEnabler(p);
		auto pen = st::boxBg->p;
		pen.setWidth(st.width);
		p.setPen(pen);
		p.setBrush(st.activeFg);
		p.drawEllipse(Rect(st.seekSize) - Margins(lineWidth));
	}, bigDot->lifetime());

	return [=](int index) {
		const auto g = slider->geometry();
		const auto bigTop = g.y() + (g.height() - bigDot->height()) / 2;
		const auto smallTop = g.y()
			+ (g.height() - smallSize.height()) / 2;
		for (auto i = 0; i < count; ++i) {
			if (index == i) {
				const auto x = ((g.width() - bigDot->width()) * i)
					/ float64(count - 1);
				bigDot->move(g.x() + std::round(x), bigTop);
			} else {
				const auto k = (i < index) ? i : i - 1;
				const auto w = smallDots[k]->width();
				smallDots[k]->move(
					g.x() + ((g.width() - w) * i) / (count - 1),
					smallTop);
			}
		}
	};
}

} // namespace

void FillPeerQrBox(
		not_null<Ui::GenericBox*> box,
		PeerData *peer,
		std::optional<QString> customLink,
		rpl::producer<QString> about) {
	const auto window = Core::App().findWindow(box);
	const auto controller = window ? window->sessionController() : nullptr;
	if (!controller) {
		return;
	}
	box->setStyle(st::giveawayGiftCodeBox);
	box->setNoContentMargin(true);
	box->setWidth(st::aboutWidth);
	box->setTitle(tr::lng_group_invite_context_qr());
	box->verticalLayout()->resizeToWidth(box->width());
	struct State {
		Ui::RpWidget* saveButton = nullptr;
		rpl::variable<bool> saveButtonBusy = false;
		rpl::variable<bool> userpicToggled = true;
		rpl::variable<bool> backgroundToggled = true;
		rpl::variable<Colors> bgs;
		Ui::Animations::Simple animation;
		rpl::variable<int> chosen = 0;
		rpl::variable<int> scaleValue = 0;
		rpl::variable<int> fontSizeValue = 28;
	};
	const auto state = box->lifetime().make_state<State>();
	state->userpicToggled = !(customLink || !peer);

	const auto usernameValue = [=] {
		return (customLink || !peer)
			? (rpl::single(QString()) | rpl::type_erased())
			: Info::Profile::UsernameValue(peer, true) | rpl::map(
				[](const auto &username) { return username.text; });
	};
	const auto linkValue = [=] {
		return customLink
			? rpl::single(*customLink)
			: peer
			? Info::Profile::LinkValue(peer, true) | rpl::map(
				[](const auto &link) { return link.text; })
			: (rpl::single(QString()) | rpl::type_erased());
	};

	const auto userpic = Ui::CreateChild<Ui::RpWidget>(box);
	const auto userpicSize = st::defaultUserpicButton.photoSize;
	userpic->resize(Size(userpicSize));
	const auto userpicMedia = Ui::MakeUserpicThumbnail(peer
		? peer
		: controller->session().user().get());
	userpicMedia->subscribeToUpdates([=] { userpic->update(); });
	userpic->paintRequest() | rpl::start_with_next([=] {
		auto p = QPainter(userpic);
		p.drawImage(0, 0, userpicMedia->image(userpicSize));
	}, userpic->lifetime());

	userpic->setVisible(peer != nullptr);
	PrepareQrWidget(
		box->verticalLayout(),
		userpic,
		state->fontSizeValue.value(),
		state->userpicToggled.value(),
		state->backgroundToggled.value(),
		usernameValue(),
		linkValue(),
		state->bgs.value(),
		about ? std::move(about) : rpl::single(QString()));

	Ui::AddSkip(box->verticalLayout());
	Ui::AddSubsectionTitle(
		box->verticalLayout(),
		tr::lng_userpic_builder_color_subtitle());

	const auto themesContainer = box->addRow(
		object_ptr<Ui::VerticalLayout>(box));

	const auto activewidth = int(
		(st::defaultInputField.borderActive + st::lineWidth) * 0.9);
	const auto size = st::chatThemePreviewSize.width();

	const auto fill = [=](const std::vector<Data::CloudTheme> &cloudThemes) {
		while (themesContainer->count()) {
			delete themesContainer->widgetAt(0);
		}

		struct State {
			Colors colors;
			QImage image;
		};
		constexpr auto kMaxInRow = 4;
		constexpr auto kMaxColors = 4;
		auto row = (Ui::RpWidget*)(nullptr);
		auto counter = 0;
		const auto spacing = (0
			+ (box->width() - rect::m::sum::h(st::boxRowPadding))
			- (kMaxInRow * size)) / (kMaxInRow + 1);

		auto colorsCollection = ranges::views::all(
			cloudThemes
		) | ranges::views::transform([](const auto &cloudTheme) -> Colors {
			const auto it = cloudTheme.settings.find(
				Data::CloudThemeType::Light);
			if (it == end(cloudTheme.settings)) {
				return Colors();
			}
			const auto colors = it->second.paper
				? it->second.paper->backgroundColors()
				: Colors();
			if (colors.size() != kMaxColors) {
				return Colors();
			}
			return colors;
		}) | ranges::views::filter([](const Colors &colors) {
			return !colors.empty();
		}) | ranges::to_vector;
		Expects(!colorsCollection.empty());
		colorsCollection[0] = Colors{
			st::premiumButtonBg1->c,
			st::premiumButtonBg1->c,
			st::premiumButtonBg2->c,
			st::premiumButtonBg3->c,
		};
		// colorsCollection.push_back(Colors{
		// 	st::creditsBg1->c,
		// 	st::creditsBg2->c,
		// 	st::creditsBg1->c,
		// 	st::creditsBg2->c,
		// });

		for (const auto &colors : colorsCollection) {
			if (state->bgs.current().empty()) {
				state->bgs = colors;
			}

			if (counter % kMaxInRow == 0) {
				Ui::AddSkip(themesContainer);
				row = themesContainer->add(
					object_ptr<Ui::RpWidget>(themesContainer));
				row->resize(size, size);
			}
			const auto widget = Ui::CreateChild<Ui::AbstractButton>(row);
			widget->setClickedCallback([=] {
				state->chosen = counter;
				widget->update();
				state->animation.stop();
				state->animation.start([=](float64 value) {
					const auto was = state->bgs.current();
					const auto &now = colors;
					if (was.size() == now.size()
						&& was.size() == kMaxColors) {
						state->bgs = Colors({
							anim::color(was[0], now[0], value),
							anim::color(was[1], now[1], value),
							anim::color(was[2], now[2], value),
							anim::color(was[3], now[3], value),
						});
					}
				},
				0.,
				1.,
				st::shakeDuration);
			});
			state->chosen.value() | rpl::combine_previous(
			) | rpl::filter([=](int i, int k) {
				return i == counter || k == counter;
			}) | rpl::start_with_next([=] {
				widget->update();
			}, widget->lifetime());
			widget->resize(size, size);
			widget->moveToLeft(
				spacing + ((counter % kMaxInRow) * (size + spacing)),
				0);
			widget->show();
			const auto back = [&] {
				auto result = Images::Round(
					Images::GenerateGradient(
						Size(size - activewidth * 5),
						colors,
						0,
						0),
					ImageRoundRadius::Large);
				auto colored = result;
				colored.fill(Qt::transparent);
				{
					auto p = QPainter(&colored);
					auto hq = PainterHighQualityEnabler(p);
					st::profileQrIcon.paintInCenter(p, result.rect());
					p.setCompositionMode(QPainter::CompositionMode_SourceIn);
					p.drawImage(0, 0, result);
				}
				auto temp = result;
				temp.fill(Qt::transparent);
				{
					auto p = QPainter(&temp);
					auto hq = PainterHighQualityEnabler(p);
					p.setPen(st::premiumButtonFg);
					p.setBrush(st::premiumButtonFg);
					const auto size = st::profileQrIcon.width() * 1.5;
					const auto margins = Margins((result.width() - size) / 2);
					const auto inner = result.rect() - margins;
					p.drawRoundedRect(
						inner,
						st::roundRadiusLarge,
						st::roundRadiusLarge);
					p.drawImage(0, 0, colored);
				}
				{
					auto p = QPainter(&result);
					p.drawImage(0, 0, temp);
				}
				return result;
			}();
			widget->paintRequest() | rpl::start_with_next([=] {
				auto p = QPainter(widget);
				const auto rect = widget->rect() - Margins(activewidth * 2.5);
				p.drawImage(rect.x(), rect.y(), back);
				if (state->chosen.current() == counter) {
					auto hq = PainterHighQualityEnabler(p);
					auto pen = st::activeLineFg->p;
					pen.setWidth(st::defaultInputField.borderActive);
					p.setPen(pen);
					const auto r = st::roundRadiusLarge
						+ activewidth * 2.1 * style::DevicePixelRatio();
					p.drawRoundedRect(
						widget->rect() - Margins(pen.width()),
						r,
						r);
				}
			}, widget->lifetime());
			counter++;
		}
		Ui::AddSkip(themesContainer);
		Ui::AddSkip(themesContainer);
		themesContainer->resizeToWidth(box->width());
	};

	const auto themes = &controller->session().data().cloudThemes();
	const auto &list = themes->chatThemes();
	if (!list.empty()) {
		fill(list);
	} else {
		themes->refreshChatThemes();
		themes->chatThemesUpdated(
		) | rpl::take(1) | rpl::start_with_next([=] {
			fill(themes->chatThemes());
		}, box->lifetime());
	}

	Ui::AddSkip(box->verticalLayout());
	Ui::AddDivider(box->verticalLayout());
	Ui::AddSkip(box->verticalLayout());
	Ui::AddSubsectionTitle(
		box->verticalLayout(),
		tr::lng_qr_box_quality());
	Ui::AddSkip(box->verticalLayout());
	constexpr auto kMaxQualities = 3;
	{
		const auto seekSize = st::settingsScale.seekSize.height();
		const auto &labelSt = st::defaultFlatLabel;
		const auto labels = box->verticalLayout()->add(
			Ui::CreateSkipWidget(
				box,
				labelSt.style.font->height + labelSt.style.font->descent),
			st::boxRowPadding);
		const auto left = Ui::CreateChild<Ui::FlatLabel>(
			labels,
			tr::lng_qr_box_quality1(),
			labelSt);
		const auto middle = Ui::CreateChild<Ui::FlatLabel>(
			labels,
			tr::lng_qr_box_quality2(),
			labelSt);
		const auto right = Ui::CreateChild<Ui::FlatLabel>(
			labels,
			tr::lng_qr_box_quality3(),
			labelSt);
		labels->sizeValue(
		) | rpl::start_with_next([=](const QSize &size) {
			left->moveToLeft(0, 0);
			middle->moveToLeft((size.width() - middle->width()) / 2, 0);
			right->moveToRight(0, 0);
		}, labels->lifetime());

		const auto slider = box->verticalLayout()->add(
			object_ptr<Ui::MediaSliderWheelless>(
				box->verticalLayout(),
				st::settingsScale),
			st::boxRowPadding);
		slider->resize(slider->width(), seekSize);
		const auto active = st::windowActiveTextFg->c;
		const auto inactive = st::windowSubTextFg->c;
		const auto colorize = [=](int index) {
			if (index == 0) {
				left->setTextColorOverride(active);
				middle->setTextColorOverride(inactive);
				right->setTextColorOverride(inactive);
			} else if (index == 1) {
				left->setTextColorOverride(inactive);
				middle->setTextColorOverride(active);
				right->setTextColorOverride(inactive);
			} else if (index == 2) {
				left->setTextColorOverride(inactive);
				middle->setTextColorOverride(inactive);
				right->setTextColorOverride(active);
			}
		};
		const auto updateGeometry = AddDotsToSlider(
			slider,
			st::settingsScale,
			kMaxQualities);
		slider->geometryValue(
		) | rpl::start_with_next([=](const QRect &rect) {
			updateGeometry(int(slider->value() * (kMaxQualities - 1)));
		}, box->lifetime());

		box->setShowFinishedCallback([=] {
			colorize(0);
			updateGeometry(0);
		});
		slider->setPseudoDiscrete(
			kMaxQualities,
			[=](int index) { return index; },
			0,
			[=](int scale) {
				state->scaleValue = scale;
				colorize(scale);
				updateGeometry(scale);
			},
			[](int) {});
	}
	{
		Ui::AddSkip(box->verticalLayout());
		Ui::AddSkip(box->verticalLayout());
		Ui::AddSubsectionTitle(
			box->verticalLayout(),
			tr::lng_qr_box_font_size());
		Ui::AddSkip(box->verticalLayout());
		const auto seekSize = st::settingsScale.seekSize.height();

		const auto slider = box->verticalLayout()->add(
			object_ptr<Ui::MediaSliderWheelless>(
				box->verticalLayout(),
				st::settingsScale),
			st::boxRowPadding);
		slider->resize(slider->width(), seekSize);
		const auto kSizeAmount = 8;
		const auto kMinSize = 20;
		const auto kMaxSize = 36;
		const auto kStep = (kMaxSize - kMinSize) / (kSizeAmount - 1);
		const auto updateGeometry = AddDotsToSlider(
			slider,
			st::settingsScale,
			kSizeAmount);
		const auto fontSizeToIndex = [=](int fontSize) {
			return (fontSize - kMinSize) / kStep;
		};
		const auto indexToFontSize = [=](int index) {
			return kMinSize + index * kStep;
		};
		slider->geometryValue(
		) | rpl::start_with_next([=](const QRect &rect) {
			updateGeometry(fontSizeToIndex(state->fontSizeValue.current()));
		}, box->lifetime());

		box->setShowFinishedCallback([=] {
			updateGeometry(fontSizeToIndex(state->fontSizeValue.current()));
		});
		slider->setPseudoDiscrete(
			kSizeAmount,
			[=](int index) { return indexToFontSize(index); },
			state->fontSizeValue.current(),
			[=](int fontSize) {
				state->fontSizeValue = fontSize;
				updateGeometry(fontSizeToIndex(fontSize));
			},
			[](int) {});
	}
	Ui::AddSkip(box->verticalLayout());
	Ui::AddSkip(box->verticalLayout());
	if (peer) {
		const auto userpicToggle = box->verticalLayout()->add(
			object_ptr<Ui::SettingsButton>(
				box->verticalLayout(),
				(peer->isUser()
					? tr::lng_mediaview_profile_photo
					: (peer->isChannel() && !peer->isMegagroup())
					? tr::lng_mediaview_channel_photo
					: tr::lng_mediaview_group_photo)(),
				st::settingsButtonNoIcon));
		userpicToggle->toggleOn(state->userpicToggled.value(), true);
		userpicToggle->setClickedCallback([=] {
			state->userpicToggled = !state->userpicToggled.current();
		});
	}
	{
		const auto backgroundToggle = box->verticalLayout()->add(
			object_ptr<Ui::SettingsButton>(
				box->verticalLayout(),
				tr::lng_qr_box_transparent_background(),
				st::settingsButtonNoIcon));
		backgroundToggle->toggleOn(
			state->backgroundToggled.value() | rpl::map(!rpl::mappers::_1),
			true);
		backgroundToggle->setClickedCallback([=] {
			state->backgroundToggled = !state->backgroundToggled.current();
		});
	}
	Ui::AddSkip(box->verticalLayout());
	Ui::AddSkip(box->verticalLayout());

	auto buttonText = rpl::conditional(
		state->saveButtonBusy.value() | rpl::map(rpl::mappers::_1),
		rpl::single(QString()),
		tr::lng_chat_link_copy());
	const auto show = controller->uiShow();
	state->saveButton = box->addButton(std::move(buttonText), [=] {
		if (state->saveButtonBusy.current()) {
			return;
		}
		const auto buttonWidth = state->saveButton
			? state->saveButton->width()
			: 0;
		state->saveButtonBusy = true;
		if (state->saveButton) {
			state->saveButton->resizeToWidth(buttonWidth);
		}

		const auto userpicToggled = state->userpicToggled.current();
		const auto backgroundToggled = state->backgroundToggled.current();
		const auto scale = style::kScaleDefault
			* (kMaxQualities + int(state->scaleValue.current() * 2));
		const auto divider = std::max(1, style::Scale())
			/ style::kScaleDefault;
		const auto profileQrBackgroundRadius = style::ConvertScale(
			st::profileQrBackgroundRadius / divider,
			scale);
		const auto introQrPixel = style::ConvertScale(
			st::introQrPixel / divider,
			scale);
		const auto lineWidth = style::ConvertScale(
			st::lineWidth / divider,
			scale);
		const auto boxWideWidth = style::ConvertScale(
			st::boxWideWidth / divider,
			scale);
		const auto createMargins = [&](const style::margins &margins) {
			return QMargins(
				style::ConvertScale(margins.left() / divider, scale),
				style::ConvertScale(margins.top() / divider, scale),
				style::ConvertScale(margins.right() / divider, scale),
				style::ConvertScale(margins.bottom() / divider, scale));
		};
		const auto boxRowPadding = createMargins(st::boxRowPadding);
		const auto backgroundMargins = userpicToggled
			? createMargins(st::profileQrBackgroundMargins)
			: createMargins(NoPhotoBackgroundMargins());
		const auto qrMaxSize = boxWideWidth
			- rect::m::sum::h(boxRowPadding)
			- rect::m::sum::h(backgroundMargins);
		const auto photoSize = userpicToggled
			? style::ConvertScale(
				st::defaultUserpicButton.photoSize / divider,
				scale)
			: 0;

		const auto font = CreateFont(state->fontSizeValue.current(), scale);
		const auto username = rpl::variable<QString>(
			usernameValue()).current().toUpper();
		const auto link = rpl::variable<QString>(linkValue());
		const auto textWidth = font->width(username);
		const auto top = photoSize
			? userpicMedia->image(photoSize)
			: QImage();
		const auto weak = Ui::MakeWeak(box);

		crl::async([=] {
			const auto qrImage = TelegramQr(
				Qr::Encode(
					link.current().toUtf8(),
					Qr::Redundancy::Default),
				introQrPixel,
				qrMaxSize,
				backgroundToggled);
			const auto textMaxWidth = backgroundMargins.left()
				+ (qrImage.width() / style::DevicePixelRatio());
			const auto lines = int(textWidth / textMaxWidth) + 1;
			const auto textMaxHeight = textWidth ? font->height * lines : 0;

			const auto whiteMargins = RoundedMargins(
				backgroundMargins,
				photoSize,
				textMaxHeight);
			const auto resultSize = QSize(
				qrMaxSize + rect::m::sum::h(whiteMargins),
				qrMaxSize + rect::m::sum::v(whiteMargins) + photoSize / 2);

			const auto qrImageSize = qrImage.size()
				/ style::DevicePixelRatio();
			const auto qrRect = Rect(
				(resultSize.width() - qrImageSize.width()) / 2,
				whiteMargins.top() + photoSize / 2,
				qrImageSize);

			auto image = QImage(
				resultSize * style::DevicePixelRatio(),
				QImage::Format_ARGB32_Premultiplied);
			image.fill(Qt::transparent);
			image.setDevicePixelRatio(style::DevicePixelRatio());
			{
				auto p = QPainter(&image);
				p.translate(0, lineWidth); // Bad.
				Paint(
					p,
					font,
					username,
					state->bgs.current(),
					backgroundMargins,
					qrImage,
					qrRect,
					qrMaxSize,
					introQrPixel,
					profileQrBackgroundRadius,
					textMaxHeight,
					photoSize,
					backgroundToggled);

				if (userpicToggled) {
					p.drawImage((resultSize.width() - photoSize) / 2, 0, top);
				}
			}
			crl::on_main(weak, [=] {
				state->saveButtonBusy = false;
				auto mime = std::make_unique<QMimeData>();
				mime->setImageData(std::move(image));
				QGuiApplication::clipboard()->setMimeData(mime.release());
				show->showToast(tr::lng_group_invite_qr_copied(tr::now));
			});
		});
	});

	if (const auto saveButton = state->saveButton) {
		using namespace Info::Statistics;
		const auto loadingAnimation = InfiniteRadialAnimationWidget(
			saveButton,
			saveButton->height() / 2);
		AddChildToWidgetCenter(saveButton, loadingAnimation);
		loadingAnimation->showOn(state->saveButtonBusy.value());
	}

	const auto buttonWidth = box->width()
		- rect::m::sum::h(st::giveawayGiftCodeBox.buttonPadding);
	state->saveButton->widthValue() | rpl::filter([=] {
		return (state->saveButton->widthNoMargins() != buttonWidth);
	}) | rpl::start_with_next([=] {
		state->saveButton->resizeToWidth(buttonWidth);
	}, state->saveButton->lifetime());
	box->addTopButton(st::boxTitleClose, [=] { box->closeBox(); });
}

} // namespace Ui
