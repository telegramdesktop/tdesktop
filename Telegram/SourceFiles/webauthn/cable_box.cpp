/*
This file is part of Telegram Desktop,
the official desktop application for the Telegram messaging service.

For license and copyright information please follow this link:
https://github.com/telegramdesktop/tdesktop/blob/master/LEGAL
*/
#include "webauthn/cable_box.h"

#include "core/application.h"
#include "window/window_controller.h"
#include "lang/lang_keys.h"
#include "ui/layers/generic_box.h"
#include "ui/layers/show.h"
#include "ui/rp_widget.h"
#include "ui/widgets/buttons.h"
#include "ui/widgets/labels.h"
#include "ui/wrap/vertical_layout.h"
#include "ui/painter.h"
#include "lottie/lottie_icon.h"
#include "qr/qr_generate.h"
#include "base/unique_qptr.h"
#include "styles/style_intro.h"
#include "styles/style_layers.h"
#include "styles/style_settings.h"
#include "styles/style_widgets.h"

#include <QtGui/QPainter>
#include <QtSvg/QSvgRenderer>

#include <rpl/flatten_latest.h>
#include <rpl/map.h>

namespace Platform::WebAuthn::Cable {
namespace {

[[nodiscard]] Window::Controller *ActiveController() {
	if (const auto active = Core::App().activeWindow()) {
		return active;
	}
	return Core::App().activePrimaryWindow();
}

[[nodiscard]] std::shared_ptr<Ui::Show> ActiveShow() {
	const auto controller = ActiveController();
	return controller ? controller->uiShow() : nullptr;
}

[[nodiscard]] QImage QrCenterImage() {
	const auto ratio = style::DevicePixelRatio();
	const auto side = st::passkeyCableQrCenter.width() * ratio;
	auto result = QImage(side, side, QImage::Format_ARGB32_Premultiplied);
	result.fill(Qt::transparent);
	auto p = QPainter(&result);
	auto hq = PainterHighQualityEnabler(p);
	p.setPen(Qt::NoPen);
	p.setBrush(Qt::white);
	const auto radius = side / 5.;
	p.drawRoundedRect(0, 0, side, side, radius, radius);
	auto svg = QSvgRenderer(u":/gui/passkey_qr_center.svg"_q);
	if (svg.isValid()) {
		const auto target = (side * 94) / 100;
		const auto shift = (side - target) / 2.;
		svg.render(&p, QRectF(shift, shift, target, target));
	}
	return result;
}

[[nodiscard]] QImage RenderQr(const QString &text) {
	const auto data = Qr::Encode(text, Qr::Redundancy::Quartile);
	const auto ratio = style::DevicePixelRatio();
	const auto pixel = std::max(
		st::passkeyCableQrSize / std::max(data.size, 1),
		1);
	auto image = Qr::Generate(data, pixel * ratio, Qt::black, Qt::white);

	auto center = QrCenterImage();
	image = Qr::ReplaceCenter(std::move(image), center);
	image.setDevicePixelRatio(ratio);
	return image;
}

} // namespace

void ShowCableToast(const QString &text) {
	if (const auto controller = ActiveController()) {
		controller->showToast(text);
	}
}

bool ShowCableBox(BoxContent &&content) {
	const auto show = ActiveShow();
	if (!show) {
		return false;
	}
	const auto state = content.state;
	const auto isRegister = content.isRegister;
	const auto bluetooth = content.bluetoothAvailable;
	const auto qrImage = bluetooth
		? std::make_shared<QImage>(RenderQr(content.qrText))
		: std::shared_ptr<QImage>();
	const auto securityKeyChosen = content.securityKeyChosen;
	const auto cancelled = content.cancelled;
	show->show(Box([=](not_null<Ui::GenericBox*> box) {
		box->setStyle(st::passkeyCableBox);
		box->setTitle(state->sheet.value(
		) | rpl::map([=](Sheet sheet) -> rpl::producer<QString> {
			switch (sheet) {
			case Sheet::Qr:
				return isRegister
					? tr::lng_passkey_cable_register_title()
					: tr::lng_passkey_cable_login_title();
			case Sheet::Connecting:
				return tr::lng_passkey_cable_connecting();
			case Sheet::Continue:
				return tr::lng_passkey_cable_continue();
			case Sheet::Error:
				return tr::lng_passkey_cable_error_title();
			}
			Unexpected("Sheet value in cable box title.");
		}) | rpl::flatten_latest());

		state->closeRequests.events() | rpl::on_next([=] {
			box->closeBox();
		}, box->lifetime());

		const auto current = box->lifetime().make_state<
			base::unique_qptr<Ui::VerticalLayout>>();
		const auto rebuild = [=](Sheet sheet) {
			current->reset();
			const auto outer = box->verticalLayout();
			const auto content = outer->add(
				object_ptr<Ui::VerticalLayout>(outer));
			current->reset(content);

			const auto addDescription = [&](rpl::producer<QString> text) {
				content->add(
					object_ptr<Ui::FlatLabel>(
						content,
						std::move(text),
						st::boxLabel),
					st::boxRowPadding);
			};
			const auto addPill = [&](
					rpl::producer<QString> text,
					Fn<void()> callback) {
				const auto button = content->add(
					object_ptr<Ui::RoundButton>(
						content,
						std::move(text),
						st::defaultLightButton),
					style::margins(
						st::boxRowPadding.left(),
						2 * st::boxLittleSkip,
						st::boxRowPadding.right(),
						2 * st::boxLittleSkip),
					style::al_justify);
				button->setFullRadius(true);
				button->setClickedCallback(std::move(callback));
			};
			box->clearButtons();
			{
				auto bottom = object_ptr<Ui::RoundButton>(
					box,
					((sheet == Sheet::Error)
						? tr::lng_close()
						: tr::lng_cancel()),
					st::defaultLightButton);
				bottom->setFullRadius(true);
				bottom->resizeToWidth(st::boxWidth
					- st::passkeyCableBox.buttonPadding.left()
					- st::passkeyCableBox.buttonPadding.right());
				bottom->setClickedCallback([=] {
					box->closeBox();
				});
				box->addButton(std::move(bottom));
			}

			switch (sheet) {
			case Sheet::Qr: {
				if (bluetooth) {
					addDescription(isRegister
						? tr::lng_passkey_cable_register_about()
						: tr::lng_passkey_cable_login_about());

					const auto size = qrImage->width()
						/ style::DevicePixelRatio();
					const auto skip = st::introQrBackgroundSkip;
					const auto radius = st::introQrBackgroundRadius;
					const auto full = size + 2 * skip;
					const auto qr = content->add(
						object_ptr<Ui::RpWidget>(content),
						style::margins(
							0,
							st::boxLittleSkip,
							0,
							st::boxLittleSkip));
					qr->resize(full, full);
					qr->paintRequest(
					) | rpl::on_next([=] {
						auto p = QPainter(qr);
						auto hq = PainterHighQualityEnabler(p);
						const auto x = (qr->width() - full) / 2;
						p.setPen(Qt::NoPen);
						p.setBrush(Qt::white);
						p.drawRoundedRect(
							QRect(x, 0, full, full),
							radius,
							radius);
						p.drawImage(x + skip, skip, *qrImage);
					}, qr->lifetime());

					content->add(
						object_ptr<Ui::FlatLabel>(
							content,
							tr::lng_passkey_cable_bluetooth(),
							st::passkeyCableHintLabel),
						st::boxRowPadding,
						style::al_top);
				} else {
					addDescription(tr::lng_passkey_cable_no_bluetooth());
				}

				addPill(tr::lng_passkey_method_key(), [=] {
					if (securityKeyChosen) {
						securityKeyChosen();
					}
				});
				content->add(
					object_ptr<Ui::FlatLabel>(
						content,
						tr::lng_passkey_cable_key_about(),
						st::passkeyCableHintLabel),
					style::margins(
						st::boxRowPadding.left(),
						0,
						st::boxRowPadding.right(),
						st::boxLittleSkip),
					style::al_top);
			} break;

			case Sheet::Connecting:
			case Sheet::Continue: {
				const auto side = (st::settingsCloudPasswordIconSize * 7)
					/ 10;
				const auto padding = st::settingLocalPasscodeIconPadding;
				auto owned = Lottie::MakeIcon({
					.name = u"passkeys"_q,
					.sizeOverride = { side, side },
					.limitFps = true,
				});
				const auto icon = owned.get();
				const auto widget = content->add(
					object_ptr<Ui::RpWidget>(content),
					style::margins(),
					style::al_top);
				widget->resize(
					side,
					padding.top() + side + padding.bottom());
				widget->lifetime().add([kept = std::move(owned)] {});
				widget->paintRequest(
				) | rpl::on_next([=] {
					if (!icon->valid()) {
						widget->update();
						return;
					}
					auto p = QPainter(widget);
					icon->paint(
						p,
						(widget->width() - side) / 2,
						padding.top());
					const auto last = icon->framesCount() - 1;
					if (anim::Disabled()) {
						if (last > 0 && icon->frameIndex() != last) {
							icon->jumpTo(
								last,
								[=] { widget->update(); });
						}
					} else if (!icon->animating()) {
						icon->animate(
							[=] { widget->update(); },
							0,
							last);
					}
				}, widget->lifetime());

				addDescription(state->sheet.value(
				) | rpl::map([](Sheet sheet) -> rpl::producer<QString> {
					return (sheet == Sheet::Continue)
						? tr::lng_passkey_cable_follow()
						: rpl::single(QString());
				}) | rpl::flatten_latest());
			} break;

			case Sheet::Error: {
				addDescription(state->error.value());
			} break;
			}

			outer->update();
		};

		const auto sameContent = [](Sheet a, Sheet b) {
			const auto progress = [](Sheet sheet) {
				return (sheet == Sheet::Connecting)
					|| (sheet == Sheet::Continue);
			};
			return (a == b) || (progress(a) && progress(b));
		};
		const auto shown = box->lifetime().make_state<std::optional<Sheet>>();
		state->sheet.value() | rpl::on_next([=](Sheet sheet) {
			if (*shown && sameContent(**shown, sheet)) {
				*shown = sheet;
				return;
			}
			*shown = sheet;
			rebuild(sheet);
		}, box->lifetime());

		box->boxClosing() | rpl::on_next([=] {
			if (cancelled) {
				crl::on_main(cancelled);
			}
		}, box->lifetime());
	}));
	return true;
}

} // namespace Platform::WebAuthn::Cable
