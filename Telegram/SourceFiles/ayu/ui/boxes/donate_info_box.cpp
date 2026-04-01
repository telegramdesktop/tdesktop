// This is the source code of AyuGram for Desktop.
//
// We do not and cannot prevent the use of our code,
// but be respectful and credit the original author.
//
// Copyright @Radolyn, 2026
#include "ayu/ui/boxes/donate_info_box.h"

#include "lang_auto.h"
#include "ayu/utils/official_resources.h"
#include "core/ui_integration.h"
#include "info/profile/info_profile_icon.h"
#include "lang/lang_text_entity.h"
#include "styles/style_ayu_styles.h"
#include "styles/style_boxes.h"
#include "styles/style_layers.h"
#include "styles/style_menu_icons.h"
#include "styles/style_settings.h"
#include "styles/style_widgets.h"
#include "ui/painter.h"
#include "ui/rect.h"
#include "ui/rp_widget.h"
#include "ui/vertical_list.h"
#include "ui/layers/generic_box.h"
#include "ui/text/text_utilities.h"
#include "ui/widgets/labels.h"
#include "window/window_session_controller.h"

namespace Ui {
namespace {

QImage MakeSupportLogo() {
	const auto s = Size(st::supportLogoSize);
	auto image = QImage(
		s * style::DevicePixelRatio(),
		QImage::Format_ARGB32_Premultiplied);
	image.setDevicePixelRatio(style::DevicePixelRatio());
	image.fill(Qt::transparent);
	{
		auto p = QPainter(&image);
		p.fillRect(Rect(s), st::windowBgOver);
	}
	return image;
}

object_ptr<Ui::RpWidget> CreateTopLogoWidget(
	not_null<Ui::RpWidget*> parent) {
	auto w = object_ptr<Ui::RpWidget>(parent);
	const auto raw = w.data();

	const auto logo = MakeSupportLogo();

	raw->paintRequest(
	) | rpl::on_next(
		[=](QRect)
		{
			QPainter p(raw);
			PainterHighQualityEnabler hq(p);

			const auto original = logo.size() / style::DevicePixelRatio();
			const auto maxWidth = raw->width() - rect::m::sum::h(st::boxRowPadding);
			const auto maxHeight = raw->height();
			if (original.isEmpty() || maxWidth <= 0 || maxHeight <= 0) {
				return;
			}
			const auto scale = std::min(
				double(maxWidth) / double(original.width()),
				double(maxHeight) / double(original.height()));
			const auto target = QSize(
				int(original.width() * scale),
				int(original.height() * scale));
			const auto x = (raw->width() - target.width()) / 2;
			const auto y = (raw->height() - target.height()) / 2;
			const auto rect = QRect(QPoint(x, y), target);
			p.drawImage(rect, logo);
		},
		raw->lifetime());

	return w;
}

object_ptr<Ui::RpWidget> InfoRow(
	not_null<Ui::RpWidget*> parent,
	const QString &title,
	const QString &text,
	not_null<const style::icon*> icon) {
	auto row = object_ptr<Ui::VerticalLayout>(parent);
	const auto raw = row.data();

	raw->add(
		object_ptr<Ui::FlatLabel>(
			raw,
			rpl::single(tr::bold(title)),
			st::defaultFlatLabel),
		st::settingsPremiumRowTitlePadding);

	raw->add(
		object_ptr<Ui::FlatLabel>(
			raw,
			rpl::single(text),
			st::boxDividerLabel),
		st::settingsPremiumRowAboutPadding);

	object_ptr<Info::Profile::FloatingIcon>(
		raw,
		*icon,
		st::starrefInfoIconPosition);

	return row;
}

} // namespace

void FillDonateInfoBox(not_null<Ui::GenericBox*> box, not_null<Window::SessionController*> controller) {
	Q_UNUSED(controller);
	box->setStyle(st::giveawayGiftCodeBox);
	box->setNoContentMargin(true);
	box->setWidth(int(st::aboutWidth * 1.1));
	box->verticalLayout()->resizeToWidth(box->width());

	box->addTopButton(st::boxTitleClose, [=] { box->closeBox(); });

	Ui::AddSkip(box->verticalLayout());
	Ui::AddSkip(box->verticalLayout());

	const auto logoWidget = box->verticalLayout()->add(
		CreateTopLogoWidget(box->verticalLayout()));
	logoWidget->resize(st::supportLogoSize, st::supportLogoSize);

	Ui::AddSkip(box->verticalLayout());

	box->verticalLayout()->add(
		object_ptr<Ui::FlatLabel>(
			box->verticalLayout(),
			tr::ayu_SupportBoxHeader(tr::bold),
			st::boxTitle),
		st::boxRowPadding,
		style::al_top);

	box->verticalLayout()->add(
		object_ptr<Ui::FlatLabel>(
			box->verticalLayout(),
			tr::ayu_SupportBoxInfo(),
			st::starrefCenteredText),
		st::boxRowPadding);

	Ui::AddSkip(box->verticalLayout());
	Ui::AddSkip(box->verticalLayout());

	for (const auto &entry : TeleForge::OfficialResources::kEntries) {
		const auto title = [&] {
			using Type = TeleForge::OfficialResources::Type;
			if (entry.type == Type::Channel) {
				return tr::ayu_SupportBoxMakeDonationHeader(tr::now);
			} else if (entry.type == Type::Chat) {
				return tr::ayu_SupportBoxSendProofHeader(tr::now);
			}
			return tr::ayu_SupportBoxReceiveBadgeHeader(tr::now);
		}();
		const auto text = QString::fromLatin1(entry.label);
		box->verticalLayout()->add(InfoRow(
			box->verticalLayout(),
			title,
			text,
			entry.type == TeleForge::OfficialResources::Type::Channel
				? &st::menuIconChannel
				: (entry.type == TeleForge::OfficialResources::Type::Chat
					? &st::menuIconChats
					: &st::menuIconIpAddress)));
	}

	Ui::AddSkip(box->verticalLayout());

	const auto closeButton = box->addButton(tr::lng_close(), [=] { box->closeBox(); });
	const auto buttonWidth = box->width()
		- rect::m::sum::h(st::starrefFooterBox.buttonPadding);
	closeButton->widthValue() | rpl::filter([=]
	{
		return (closeButton->widthNoMargins() != buttonWidth);
	}) | rpl::on_next([=]
							  {
								  closeButton->resizeToWidth(buttonWidth);
							  },
							  closeButton->lifetime());
}

} // namespace Ui
