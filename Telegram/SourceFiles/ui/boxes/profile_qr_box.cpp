/*
This file is part of Telegram Desktop,
the official desktop application for the Telegram messaging service.

For license and copyright information please follow this link:
https://github.com/telegramdesktop/tdesktop/blob/master/LEGAL
*/
#include "ui/boxes/profile_qr_box.h"

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
#include "ui/effects/animations.h"
#include "ui/image/image_prepare.h"
#include "ui/layers/generic_box.h"
#include "ui/painter.h"
#include "ui/rect.h"
#include "ui/ui_utility.h"
#include "ui/vertical_list.h"
#include "ui/widgets/box_content_divider.h"
#include "ui/widgets/buttons.h"
#include "ui/wrap/vertical_layout.h"
#include "window/window_controller.h"
#include "window/window_session_controller.h"
#include "styles/style_boxes.h"
#include "styles/style_giveaway.h"
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

[[nodiscard]] QImage TelegramQr(const Qr::Data &data, int pixel, int max) {
	Expects(data.size > 0);

	constexpr auto kCenterRatio = 0.175;

	if (max > 0 && data.size * pixel > max) {
		pixel = std::max(max / data.size, 1);
	}
	auto qr = Qr::Generate(
		data,
		pixel * style::DevicePixelRatio(),
		Qt::transparent,
		Qt::white);
	{
		auto p = QPainter(&qr);
		auto hq = PainterHighQualityEnabler(p);
		auto svg = QSvgRenderer(u":/gui/plane_white.svg"_q);
		const auto size = qr.rect().size();
		const auto centerRect = Rect(size)
			- Margins((size.width() - (size.width() * kCenterRatio)) / 2);
		p.setPen(Qt::NoPen);
		p.setBrush(Qt::white);
		p.setCompositionMode(QPainter::CompositionMode_Clear);
		p.drawEllipse(centerRect);
		p.setCompositionMode(QPainter::CompositionMode_SourceOver);
		svg.render(&p, centerRect);
	}
	return qr;
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
		int photoSize) {
	const auto usualSize = 41;
	const auto pixel = std::clamp(qrMaxSize / usualSize, 1, qrPixel);
	const auto size = (qrImage.size() / style::DevicePixelRatio());
	auto hq = PainterHighQualityEnabler(p);
	p.setPen(Qt::NoPen);
	p.setBrush(Qt::white);
	const auto roundedRect = qrRect
		+ backgroundMargins
		+ QMargins(0, photoSize / 2, 0, textMaxHeight);
	p.drawRoundedRect(roundedRect, radius, radius);
	if (!qrImage.isNull() && !backgroundColors.empty()) {
		constexpr auto kDuration = crl::time(10000);
		const auto angle = (crl::now() % kDuration)
			/ float64(kDuration) * 360.0;
		const auto gradientRotation = int(angle / 45.) * 45;
		const auto gradientRotationAdd = angle - gradientRotation;

		const auto center = QPointF(rect::center(qrRect));
		const auto radius = std::sqrt(std::pow(qrRect.width() / 2., 2)
			+ std::pow(qrRect.height() / 2., 2));
		auto back = Images::GenerateGradient(
			qrRect.size(),
			backgroundColors,
			gradientRotation,
			1. - (gradientRotationAdd / 45.));
		p.drawImage(qrRect, back);
		const auto coloredSize = QSize(back.width(), textMaxHeight);
		auto colored = QImage(
			coloredSize * style::DevicePixelRatio(),
			QImage::Format_ARGB32_Premultiplied);
		colored.setDevicePixelRatio(style::DevicePixelRatio());
		colored.fill(Qt::transparent);
		{
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
		p.drawImage(qrRect, qrImage);
		p.drawImage(
			qrRect.x(),
			rect::bottom(qrRect)
				+ ((rect::bottom(roundedRect) - rect::bottom(qrRect))
					- textMaxHeight) / 2,
			colored);
	}
}

[[nodiscard]] not_null<Ui::RpWidget*> PrepareQrWidget(
		not_null<Ui::VerticalLayout*> container,
		not_null<Ui::RpWidget*> topWidget,
		const style::font &font,
		rpl::producer<bool> userpicToggled,
		rpl::producer<TextWithEntities> username,
		rpl::producer<QString> links,
		rpl::producer<Colors> bgs) {
	const auto divider = container->add(
		object_ptr<Ui::BoxContentDivider>(container));
	struct State final {
		explicit State(Fn<void()> callback) : updating(callback) {
			updating.start();
		}

		Ui::Animations::Basic updating;
		QImage qrImage;
		Colors backgroundColors;
		QString text;
		QMargins backgroundMargins;
		int textWidth = 0;
		int textMaxHeight = 0;
		int photoSize = 0;
	};
	const auto result = Ui::CreateChild<Ui::RpWidget>(divider);
	topWidget->setParent(result);
	topWidget->setAttribute(Qt::WA_TransparentForMouseEvents);
	const auto state = result->lifetime().make_state<State>(
		[=] { result->update(); });
	const auto qrMaxSize = st::boxWideWidth
		- rect::m::sum::h(st::boxRowPadding)
		- rect::m::sum::h(st::profileQrBackgroundMargins);
	rpl::combine(
		std::move(userpicToggled),
		std::move(username),
		std::move(bgs),
		std::move(links),
		rpl::single(rpl::empty) | rpl::then(style::PaletteChanged())
	) | rpl::start_with_next([=](
			bool userpicToggled,
			const TextWithEntities &username,
			const Colors &backgroundColors,
			const QString &link,
			const auto &) {
		state->backgroundMargins = userpicToggled
			? st::profileQrBackgroundMargins
			: NoPhotoBackgroundMargins();
		state->photoSize = userpicToggled
			? st::defaultUserpicButton.photoSize
			: 0;
		state->backgroundColors = backgroundColors;
		state->text = username.text.toUpper();
		state->textWidth = font->width(state->text);
		state->qrImage = TelegramQr(
			Qr::Encode(link.toUtf8(), Qr::Redundancy::Default),
			st::introQrPixel,
			qrMaxSize);
		const auto qrWidth = state->qrImage.width()
			/ style::DevicePixelRatio();
		const auto lines = int(state->textWidth / qrWidth) + 1;
		state->textMaxHeight = font->height * lines;
		result->resize(
			qrMaxSize + rect::m::sum::h(state->backgroundMargins),
			qrMaxSize
				+ rect::m::sum::v(state->backgroundMargins)
				+ state->backgroundMargins.bottom()
				+ state->textMaxHeight
				+ state->photoSize);

		divider->resize(container->width(), result->height());
		result->moveToLeft((container->width() - result->width()) / 2, 0);
		topWidget->setVisible(userpicToggled);
		topWidget->moveToLeft(
			(result->width() - topWidget->width()) / 2,
			-std::numeric_limits<int>::min());
		topWidget->raise();
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
			st::profileQrBackgroundMargins.top() + state->photoSize / 2);
		Paint(
			p,
			font,
			state->text,
			state->backgroundColors,
			state->backgroundMargins,
			state->qrImage,
			qrRect,
			qrMaxSize,
			st::introQrPixel,
			st::profileQrBackgroundRadius,
			state->textMaxHeight,
			state->photoSize);
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

} // namespace

void FillProfileQrBox(
		not_null<Ui::GenericBox*> box,
		not_null<PeerData*> peer) {
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
		rpl::variable<Colors> bgs;
		Ui::Animations::Simple animation;
		rpl::variable<int> chosen = 0;

		style::font font;
	};
	const auto state = box->lifetime().make_state<State>();
	const auto createFont = [=](int scale) {
		return style::font(
			style::ConvertScale(30, scale),
			st::profileQrFont->flags(),
			st::profileQrFont->family());
	};
	state->font = createFont(style::Scale());

	const auto userpic = Ui::CreateChild<Ui::UserpicButton>(
		box,
		peer,
		st::defaultUserpicButton);
	const auto qr = PrepareQrWidget(
		box->verticalLayout(),
		userpic,
		state->font,
		state->userpicToggled.value(),
		Info::Profile::UsernameValue(peer->asUser()),
		Info::Profile::LinkValue(peer) | rpl::map([](const auto &link) {
			return link.url;
		}),
		state->bgs.value());

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
		colorsCollection.push_back(Colors{
			st::premiumButtonBg1->c,
			st::premiumButtonBg1->c,
			st::premiumButtonBg2->c,
			st::premiumButtonBg3->c,
		});
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
			const auto widgetState = widget->lifetime().make_state<State>();
			widget->setClickedCallback([=] {
				state->chosen = counter;
				widget->update();
				state->animation.stop();
				state->animation.start([=](float64 value) {
					const auto was = state->bgs.current();
					const auto now = colors;
					if (was.size() == now.size(); was.size() == kMaxColors) {
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
					p.drawRoundedRect(
						widget->rect() - Margins(pen.width()),
						st::roundRadiusLarge + activewidth * 4.2,
						st::roundRadiusLarge + activewidth * 4.2);
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
		tr::lng_profile_changed_photo_link());
	const auto userpicToggle = box->verticalLayout()->add(
		object_ptr<Ui::SettingsButton>(
			box->verticalLayout(),
			(peer->isUser()
				? tr::lng_mediaview_profile_photo
				: (peer->isChannel() && !peer->isMegagroup())
				? tr::lng_mediaview_channel_photo
				: tr::lng_mediaview_group_photo)(),
			st::settingsButtonNoIcon));
	userpicToggle->toggleOn(state->userpicToggled.value());
	userpicToggle->setClickedCallback([=] {
		state->userpicToggled = !state->userpicToggled.current();
	});

	auto buttonText = rpl::conditional(
		state->saveButtonBusy.value() | rpl::map(rpl::mappers::_1),
		rpl::single(QString()),
		tr::lng_chat_link_copy());
	const auto show = controller->uiShow();
	state->saveButton = box->addButton(std::move(buttonText), [=] {
		const auto buttonWidth = state->saveButton
			? state->saveButton->width()
			: 0;
		state->saveButtonBusy = true;
		if (state->saveButton) {
			state->saveButton->resizeToWidth(buttonWidth);
		}

		const auto userpicToggled = state->userpicToggled.current();
		const auto scale = style::kScaleDefault * 3;
		const auto divider = std::max(1, style::Scale())
			/ style::kScaleDefault;
		const auto profileQrBackgroundRadius = style::ConvertScale(
			st::profileQrBackgroundRadius / divider,
			scale);
		const auto introQrPixel = style::ConvertScale(
			st::introQrPixel / divider,
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
			? style::ConvertScale(st::defaultUserpicButton.photoSize, scale)
			: 0;

		const auto font = createFont(scale);
		const auto username = rpl::variable<TextWithEntities>(
			Info::Profile::UsernameValue(
				peer->asUser())).current().text.toUpper();
		const auto link = rpl::variable<QString>(
			Info::Profile::LinkValue(peer) | rpl::map([](const auto &l) {
				return l.url;
			}));
		const auto textWidth = font->width(username);
		const auto top = Ui::GrabWidget(
			userpic,
			{},
			Qt::transparent);
		const auto weak = Ui::MakeWeak(box);

		crl::async([=] {
			const auto qrImage = TelegramQr(
				Qr::Encode(
					link.current().toUtf8(),
					Qr::Redundancy::Default),
				introQrPixel,
				qrMaxSize);
			const auto qrWidth = qrImage.width() / style::DevicePixelRatio();
			const auto lines = int(textWidth / qrWidth) + 1;
			const auto textMaxHeight = font->height * lines;

			const auto resultSize = QSize(
				qrMaxSize + rect::m::sum::h(backgroundMargins),
				qrMaxSize
					+ rect::m::sum::v(backgroundMargins)
					+ textMaxHeight
					+ (photoSize
						? (backgroundMargins.bottom() + photoSize)
						: 0));

			const auto qrImageSize = qrImage.size()
				/ style::DevicePixelRatio();
			const auto qrRect = Rect(
				(resultSize.width() - qrImageSize.width()) / 2,
				backgroundMargins.top() + photoSize / 2,
				qrImageSize);

			auto image = QImage(
				resultSize * style::DevicePixelRatio(),
				QImage::Format_ARGB32_Premultiplied);
			image.fill(Qt::transparent);
			image.setDevicePixelRatio(style::DevicePixelRatio());
			{
				auto p = QPainter(&image);
				if (userpicToggled) {
					p.translate(0, photoSize / 2 + backgroundMargins.top());
				}
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
					photoSize);

				if (userpicToggled) {
					p.drawPixmap(
						(resultSize.width() - photoSize) / 2,
						-photoSize / 2,
						top.scaled(
							Size(photoSize * style::DevicePixelRatio()),
							Qt::IgnoreAspectRatio,
							Qt::SmoothTransformation));
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
