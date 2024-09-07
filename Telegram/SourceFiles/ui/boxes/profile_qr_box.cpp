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

[[nodiscard]] QImage TelegramQr(const Qr::Data &data, int pixel, int max) {
	Expects(data.size > 0);

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
		const auto centerWidth = st::profileQrCenterSize
			* style::DevicePixelRatio();
		const auto centerRect = Rect(size)
			- Margins((size.width() - centerWidth) / 2);
		p.setPen(Qt::NoPen);
		p.setBrush(Qt::white);
		p.setCompositionMode(QPainter::CompositionMode_Clear);
		p.drawEllipse(centerRect);
		p.setCompositionMode(QPainter::CompositionMode_SourceOver);
		svg.render(&p, centerRect);
	}
	return qr;
}

[[nodiscard]] not_null<Ui::RpWidget*> PrepareQrWidget(
		not_null<Ui::VerticalLayout*> container,
		not_null<Ui::RpWidget*> topWidget,
		rpl::producer<TextWithEntities> username,
		rpl::producer<QString> links,
		rpl::producer<std::vector<QColor>> bgs) {
	const auto divider = container->add(
		object_ptr<Ui::BoxContentDivider>(container));
	struct State final {
		explicit State(Fn<void()> callback) : updating(callback) {
			updating.start();
		}

		Ui::Animations::Basic updating;
		QImage qr;
		std::vector<QColor> bgs;
		rpl::variable<TextWithEntities> username;
		int textMaxHeight = 0;
		rpl::variable<QString> link;
	};
	auto palettes = rpl::single(rpl::empty) | rpl::then(
		style::PaletteChanged()
	);
	const auto result = Ui::CreateChild<Ui::RpWidget>(divider);
	topWidget->setParent(result);
	topWidget->setAttribute(Qt::WA_TransparentForMouseEvents);
	const auto state = result->lifetime().make_state<State>(
		[=] { result->update(); });
	state->username = rpl::variable<TextWithEntities>(std::move(username));
	state->link = rpl::variable<QString>(std::move(links));
	std::move(
		bgs
	) | rpl::start_with_next([=](const std::vector<QColor> &bgs) {
		state->bgs = bgs;
	}, container->lifetime());
	const auto font = st::mainMenuResetScaleFont;
	const auto backSkip = st::profileQrBackgroundSkip;
	const auto qrMaxSize = st::boxWideWidth
		- rect::m::sum::h(st::boxRowPadding)
		- 2 * backSkip;
	rpl::combine(
		state->username.value() | rpl::map([=](const TextWithEntities &u) {
			return font->width(u.text);
		}),
		rpl::combine(
			state->link.value() | rpl::map([](const QString &code) {
				return Qr::Encode(code.toUtf8(), Qr::Redundancy::Default);
			}),
			rpl::duplicate(palettes)
		) | rpl::map([=](const Qr::Data &code, const auto &) {
			return TelegramQr(code, st::introQrPixel, qrMaxSize);
		})
	) | rpl::start_with_next([=](int usernameW, QImage &&image) {
		state->qr = std::move(image);
		const auto qrWidth = state->qr.size().width()
			/ style::DevicePixelRatio();
		const auto lines = int(usernameW / qrWidth) + 1;
		state->textMaxHeight = font->height * lines;
		const auto heightSkip = (font->height * 3);
		result->resize(
			qrMaxSize + 2 * backSkip,
			qrMaxSize + 2 * backSkip + state->textMaxHeight + heightSkip);
	}, result->lifetime());
	result->paintRequest(
	) | rpl::start_with_next([=](QRect clip) {
		auto p = QPainter(result);
		const auto usualSize = 41;
		const auto pixel = std::clamp(
			qrMaxSize / usualSize,
			1,
			st::introQrPixel);
		const auto size = (state->qr.size() / style::DevicePixelRatio());
		const auto radius = st::profileQrBackgroundRadius;
		const auto qr = QRect(
			(result->width() - size.width()) / 2,
			backSkip * 3,
			size.width(),
			size.height());
		auto hq = PainterHighQualityEnabler(p);
		p.setPen(Qt::NoPen);
		p.setBrush(Qt::white);
		p.drawRoundedRect(
			qr
				+ QMargins(
					backSkip,
					backSkip + backSkip / 2,
					backSkip,
					backSkip + state->textMaxHeight),
			radius,
			radius);
		if (!state->qr.isNull() && !state->bgs.empty()) {
			constexpr auto kDuration = crl::time(10000);
			const auto angle = (crl::now() % kDuration)
				/ float64(kDuration) * 360.0;
			const auto gradientRotation = int(angle / 45.) * 45;
			const auto gradientRotationAdd = angle - gradientRotation;

			const auto center = QPointF(rect::center(qr));
			const auto radius = std::sqrt(std::pow(qr.width() / 2., 2)
				+ std::pow(qr.height() / 2., 2));
			auto back = Images::GenerateGradient(
				qr.size(),
				state->bgs,
				gradientRotation,
				1. - (gradientRotationAdd / 45.));
			p.drawImage(qr, back);
			const auto coloredSize = QSize(
				back.width(),
				state->textMaxHeight);
			auto colored = QImage(
				coloredSize * style::DevicePixelRatio(),
				QImage::Format_ARGB32_Premultiplied);
			colored.setDevicePixelRatio(style::DevicePixelRatio());
			colored.fill(Qt::transparent);
			{
				// '@' + QString(32, 'W');
				auto p = QPainter(&colored);
				auto hq = PainterHighQualityEnabler(p);
				p.setPen(Qt::red);
				p.setFont(font);
				auto option = QTextOption(style::al_center);
				option.setWrapMode(QTextOption::WrapAnywhere);
				p.drawText(
					Rect(coloredSize),
					state->username.current().text.toUpper(),
					option);
				p.setCompositionMode(QPainter::CompositionMode_SourceIn);
				p.drawImage(0, -back.height() + state->textMaxHeight, back);
			}
			p.drawImage(qr, state->qr);
			p.drawImage(qr.x(), qr.y() + qr.height() + backSkip / 2, colored);
		}
	}, result->lifetime());
	result->sizeValue(
	) | rpl::start_with_next([=](const QSize &size) {
		divider->resize(container->width(), size.height());
		result->moveToLeft((container->width() - size.width()) / 2, 0);

		const auto qrHeight = state->qr.height() / style::DevicePixelRatio();
		topWidget->moveToLeft(
			(result->width() - topWidget->width()) / 2,
			(backSkip
				+ st::profileQrBackgroundSkip / 2
				- topWidget->height() / 2));
		topWidget->raise();
	}, divider->lifetime());
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
		rpl::variable<std::vector<QColor>> bgs;
		Ui::Animations::Simple animation;
		rpl::variable<int> chosen = 0;
	};
	const auto state = box->lifetime().make_state<State>();
	const auto qr = PrepareQrWidget(
		box->verticalLayout(),
		Ui::CreateChild<Ui::UserpicButton>(
			box,
			peer,
			st::defaultUserpicButton),
		Info::Profile::UsernameValue(peer->asUser()),
		Info::Profile::LinkValue(peer) | rpl::map([](const auto &link) {
			return link.url;
		}),
		state->bgs.value());
	const auto themesContainer = box->addRow(
		object_ptr<Ui::VerticalLayout>(box));

	const auto activewidth = int(
		(st::defaultInputField.borderActive + st::lineWidth) * 0.9);
	const auto size = st::chatThemePreviewSize.width();

	const auto fill = [=](const std::vector<Data::CloudTheme> &cloudThemes) {
		while (themesContainer->count()) {
			delete themesContainer->widgetAt(0);
		}
		Ui::AddSkip(themesContainer);
		Ui::AddSkip(themesContainer);
		Ui::AddSkip(themesContainer);
		Ui::AddSkip(themesContainer);
		struct State {
			Colors colors;
			QImage image;
		};
		constexpr auto kMaxInRow = 4;
		auto row = (Ui::RpWidget*)(nullptr);
		auto counter = 0;
		const auto spacing = (0
			+ (box->width() - rect::m::sum::h(st::boxRowPadding))
			- (kMaxInRow * size)) / (kMaxInRow + 1);

		for (const auto &cloudTheme : cloudThemes) {
			const auto it = cloudTheme.settings.find(
				Data::CloudThemeType::Light);
			if (it == end(cloudTheme.settings)) {
				continue;
			}
			const auto colors = it->second.paper
				? it->second.paper->backgroundColors()
				: std::vector<QColor>();
			if (colors.size() != 4) {
				continue;
			}
			if (state->bgs.current().empty()) {
				state->bgs = colors;
			}

			if (counter % kMaxInRow == 0) {
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
					if (was.size() == now.size(); was.size() == 4) {
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
			if ((++counter) >= kMaxInRow) {
				Ui::AddSkip(themesContainer);
			}
		}
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

	const auto show = controller->uiShow();
	const auto button = box->addButton(tr::lng_chat_link_copy(), [=] {
		auto mime = std::make_unique<QMimeData>();
		mime->setImageData(Ui::GrabWidget(qr, {}, Qt::transparent));
		QGuiApplication::clipboard()->setMimeData(mime.release());
		show->showToast(tr::lng_group_invite_qr_copied(tr::now));
	});
	const auto buttonWidth = box->width()
		- rect::m::sum::h(st::giveawayGiftCodeBox.buttonPadding);
	button->widthValue() | rpl::filter([=] {
		return (button->widthNoMargins() != buttonWidth);
	}) | rpl::start_with_next([=] {
		button->resizeToWidth(buttonWidth);
	}, button->lifetime());
	box->addTopButton(st::boxTitleClose, [=] { box->closeBox(); });
}

} // namespace Ui
