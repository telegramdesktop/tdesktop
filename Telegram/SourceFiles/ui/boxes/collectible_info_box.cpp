/*
This file is part of Telegram Desktop,
the official desktop application for the Telegram messaging service.

For license and copyright information please follow this link:
https://github.com/telegramdesktop/tdesktop/blob/master/LEGAL
*/
#include "ui/boxes/collectible_info_box.h"

#include "base/unixtime.h"
#include "core/file_utilities.h"
#include "lang/lang_keys.h"
#include "lottie/lottie_icon.h"
#include "info/channel_statistics/earn/earn_format.h"
#include "ui/layers/generic_box.h"
#include "ui/text/format_values.h"
#include "ui/text/text_utilities.h"
#include "ui/widgets/buttons.h"
#include "ui/widgets/labels.h"
#include "ui/dynamic_image.h"
#include "ui/painter.h"
#include "settings/settings_common.h"
#include "styles/style_boxes.h"
#include "styles/style_layers.h"

#include <QtCore/QRegularExpression>
#include <QtGui/QGuiApplication>

namespace Ui {
namespace {

constexpr auto kTonMultiplier = uint64(1000000000);

[[nodiscard]] QString FormatEntity(CollectibleType type, QString entity) {
	switch (type) {
	case CollectibleType::Phone: {
		static const auto kNonDigits = QRegularExpression(u"[^\\d]"_q);
		entity.replace(kNonDigits, QString());
	} return Ui::FormatPhone(entity);
	case CollectibleType::Username:
		return entity.startsWith('@') ? entity : ('@' + entity);
	}
	Unexpected("CollectibleType in FormatEntity.");
}

[[nodiscard]] QString FormatDate(TimeId date) {
	return langDateTime(base::unixtime::parse(date));
}

[[nodiscard]] TextWithEntities FormatPrice(
		const CollectibleInfo &info,
		const CollectibleDetails &details) {
	auto minor = Info::ChannelEarn::MinorPart(info.cryptoAmount);
	if (minor.size() == 1 && minor.at(0) == '.') {
		minor += '0';
	}
	auto price = (info.cryptoCurrency == u"TON"_q)
		? base::duplicate(
			details.tonEmoji
		).append(
			Info::ChannelEarn::MajorPart(info.cryptoAmount)
		).append(minor)
		: TextWithEntities{ ('{'
			+ info.cryptoCurrency + ':' + QString::number(info.cryptoAmount)
			+ '}') };
	const auto fiat = Ui::FillAmountAndCurrency(info.amount, info.currency);
	return Ui::Text::Wrapped(
		price,
		EntityType::Bold
	).append(u" ("_q + fiat + ')');
}

[[nodiscard]] object_ptr<Ui::RpWidget> MakeOwnerCell(
		not_null<QWidget*> parent,
		const CollectibleInfo &info) {
	const auto st = &st::defaultMultiSelectItem;
	const auto size = st->height;
	auto result = object_ptr<Ui::FixedHeightWidget>(parent.get(), size);
	const auto raw = result.data();

	const auto name = info.ownerName;
	const auto userpic = info.ownerUserpic;
	const auto nameWidth = st->style.font->width(name);
	const auto added = size + st->padding.left() + st->padding.right();
	const auto subscribed = std::make_shared<bool>(false);
	raw->paintRequest() | rpl::start_with_next([=] {
		const auto use = std::min(nameWidth + added, raw->width());
		const auto x = (raw->width() - use) / 2;
		if (const auto available = use - added; available > 0) {
			auto p = QPainter(raw);
			auto hq = PainterHighQualityEnabler(p);
			p.setPen(Qt::NoPen);
			p.setBrush(st->textBg);
			p.drawRoundedRect(x, 0, use, size, size / 2., size / 2.);

			if (!*subscribed) {
				*subscribed = true;
				userpic->subscribeToUpdates([=] { raw->update(); });
			}
			p.drawImage(QRect(x, 0, size, size), userpic->image(size));

			const auto textx = x + size + st->padding.left();
			const auto texty = st->padding.top() + st->style.font->ascent;
			const auto text = (use == nameWidth + added)
				? name
				: st->style.font->elided(name, available);
			p.setPen(st->textFg);
			p.setFont(st->style.font);
			p.drawText(textx, texty, text);
		}
	}, raw->lifetime());

	return result;
}

} // namespace

CollectibleType DetectCollectibleType(const QString &entity) {
	return entity.startsWith('+')
		? CollectibleType::Phone
		: CollectibleType::Username;
}

void CollectibleInfoBox(
		not_null<Ui::GenericBox*> box,
		CollectibleInfo info,
		CollectibleDetails details) {
	box->setWidth(st::boxWideWidth);
	box->setStyle(st::collectibleBox);

	const auto type = DetectCollectibleType(info.entity);

	const auto icon = box->addRow(
		object_ptr<Ui::FixedHeightWidget>(box, st::collectibleIconDiameter),
		st::collectibleIconPadding);
	icon->paintRequest(
	) | rpl::start_with_next([=](QRect clip) {
		const auto size = icon->height();
		const auto inner = QRect(
			(icon->width() - size) / 2,
			0,
			size,
			size);
		if (!inner.intersects(clip)) {
			return;
		}
		auto p = QPainter(icon);
		auto hq = PainterHighQualityEnabler(p);
		p.setBrush(st::defaultActiveButton.textBg);
		p.setPen(Qt::NoPen);
		p.drawEllipse(inner);
	}, icon->lifetime());
	const auto lottieSize = st::collectibleIcon;
	auto lottie = Settings::CreateLottieIcon(
		icon,
		{
			.name = (type == CollectibleType::Phone
				? u"collectible_phone"_q
				: u"collectible_username"_q),
			.color = &st::defaultActiveButton.textFg,
			.sizeOverride = { lottieSize, lottieSize },
		},
		QMargins());
	box->showFinishes(
	) | rpl::start_with_next([animate = std::move(lottie.animate)] {
		animate(anim::repeat::once);
	}, box->lifetime());
	const auto animation = lottie.widget.release();
	icon->sizeValue() | rpl::start_with_next([=](QSize size) {
		const auto skip = (type == CollectibleType::Phone)
			? style::ConvertScale(2)
			: 0;
		animation->move(
			(size.width() - animation->width()) / 2,
			skip + (size.height() - animation->height()) / 2);
	}, animation->lifetime());

	const auto formatted = FormatEntity(type, info.entity);
	const auto header = (type == CollectibleType::Phone)
		? tr::lng_collectible_phone_title(
			tr::now,
			lt_phone,
			Ui::Text::Link(formatted),
			Ui::Text::WithEntities)
		: tr::lng_collectible_username_title(
			tr::now,
			lt_username,
			Ui::Text::Link(formatted),
			Ui::Text::WithEntities);
	const auto copyCallback = [box, type, formatted, text = info.copyText] {
		QGuiApplication::clipboard()->setText(
			text.isEmpty() ? formatted : text);
		box->uiShow()->showToast((type == CollectibleType::Phone)
			? tr::lng_text_copied(tr::now)
			: tr::lng_username_copied(tr::now));
	};
	box->addRow(
		object_ptr<Ui::FlatLabel>(
			box,
			rpl::single(header),
			st::collectibleHeader),
		st::collectibleHeaderPadding
	)->setClickHandlerFilter([copyCallback](const auto &...) {
		copyCallback();
		return false;
	});

	box->addRow(MakeOwnerCell(box, info), st::collectibleOwnerPadding);

	const auto text = ((type == CollectibleType::Phone)
		? tr::lng_collectible_phone_info
		: tr::lng_collectible_username_info)(
			tr::now,
			lt_date,
			TextWithEntities{ FormatDate(info.date) },
			lt_price,
			FormatPrice(info, details),
			Ui::Text::RichLangValue);
	const auto label = box->addRow(
		object_ptr<Ui::FlatLabel>(box, st::collectibleInfo),
		st::collectibleInfoPadding);
	label->setAttribute(Qt::WA_TransparentForMouseEvents);
	label->setMarkedText(text, details.tonEmojiContext());

	const auto more = box->addRow(
		object_ptr<Ui::RoundButton>(
			box,
			tr::lng_collectible_learn_more(),
			st::collectibleMore),
		st::collectibleMorePadding);
	more->setTextTransform(Ui::RoundButton::TextTransform::NoTransform);
	more->setClickedCallback([url = info.url] {
		File::OpenUrl(url);
	});

	const auto phrase = (type == CollectibleType::Phone)
		? tr::lng_collectible_phone_copy
		: tr::lng_collectible_username_copy;
	auto owned = object_ptr<Ui::RoundButton>(
		box,
		phrase(),
		st::collectibleCopy);
	const auto copy = owned.data();
	copy->setTextTransform(Ui::RoundButton::TextTransform::NoTransform);
	copy->setClickedCallback(copyCallback);
	box->addButton(std::move(owned));

	box->setNoContentMargin(true);
	const auto buttonsParent = box->verticalLayout().get();
	const auto close = Ui::CreateChild<Ui::IconButton>(
		buttonsParent,
		st::boxTitleClose);
	close->setClickedCallback([=] {
		box->closeBox();
	});
	box->widthValue(
	) | rpl::start_with_next([=](int width) {
		close->moveToRight(0, 0);
	}, box->lifetime());

	box->widthValue() | rpl::start_with_next([=](int width) {
		more->setFullWidth(width
			- st::collectibleMorePadding.left()
			- st::collectibleMorePadding.right());
		copy->setFullWidth(width
			- st::collectibleBox.buttonPadding.left()
			- st::collectibleBox.buttonPadding.right());
	}, box->lifetime());
}

} // namespace Ui
