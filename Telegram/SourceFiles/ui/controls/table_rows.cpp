/*
This file is part of Telegram Desktop,
the official desktop application for the Telegram messaging service.

For license and copyright information please follow this link:
https://github.com/telegramdesktop/tdesktop/blob/master/LEGAL
*/
#include "ui/controls/table_rows.h"

#include "base/event_filter.h"
#include "base/timer_rpl.h"
#include "boxes/peers/prepare_short_info_box.h"
#include "chat_helpers/compose/compose_show.h"
#include "data/data_session.h"
#include "data/data_peer.h"
#include "info/profile/info_profile_badge.h"
#include "info/profile/info_profile_values.h"
#include "lang/lang_keys.h"
#include "main/main_session.h"
#include "ui/controls/userpic_button.h"
#include "ui/widgets/buttons.h"
#include "ui/widgets/labels.h"
#include "ui/widgets/tooltip.h"
#include "ui/wrap/table_layout.h"
#include "ui/empty_userpic.h"
#include "ui/rect.h"
#include "ui/rp_widget.h"
#include "ui/ui_utility.h"
#include "styles/style_boxes.h"
#include "styles/style_credits.h"
#include "styles/style_giveaway.h"
#include "styles/style_info.h"
#include "styles/style_layers.h"

namespace Ui {
namespace {

constexpr auto kTooltipDuration = 6 * crl::time(1000);

} // namespace

void AddTableRow(
		not_null<TableLayout*> table,
		rpl::producer<QString> label,
		object_ptr<RpWidget> value,
		style::margins valueMargins) {
	table->addRow(
		(label
			? object_ptr<FlatLabel>(
				table,
				std::move(label),
				table->st().defaultLabel)
			: object_ptr<FlatLabel>(nullptr)),
		std::move(value),
		st::giveawayGiftCodeLabelMargin,
		valueMargins);
}

not_null<FlatLabel*> AddTableRow(
		not_null<TableLayout*> table,
		rpl::producer<QString> label,
		rpl::producer<TextWithEntities> value,
		const Text::MarkedContext &context) {
	auto widget = object_ptr<FlatLabel>(
		table,
		std::move(value),
		table->st().defaultValue,
		st::defaultPopupMenu,
		context);
	const auto result = widget.data();
	AddTableRow(table, std::move(label), std::move(widget));
	return result;
}

void AddTableRow(
		not_null<TableLayout*> table,
		rpl::producer<QString> label,
		std::shared_ptr<ChatHelpers::Show> show,
		PeerId id) {
	if (!id) {
		return;
	}
	AddTableRow(
		table,
		std::move(label),
		MakePeerTableValue(table, show, id),
		st::giveawayGiftCodePeerMargin);
}

object_ptr<RpWidget> MakeValueWithSmallButton(
		not_null<TableLayout*> table,
		not_null<RpWidget*> value,
		rpl::producer<QString> buttonText,
		Fn<void(not_null<RpWidget*> button)> handler,
		int topSkip) {
	class MarginedWidget final : public RpWidget {
	public:
		using RpWidget::RpWidget;
		QMargins getMargins() const override {
			return { 0, 0, 0, st::giveawayGiftCodePeerMargin.bottom() };
		}
	};
	auto result = object_ptr<MarginedWidget>(table);
	const auto raw = result.data();

	value->setParent(raw);
	value->show();

	const auto button = CreateChild<RoundButton>(
		raw,
		std::move(buttonText),
		table->st().smallButton);
	button->setTextTransform(RoundButton::TextTransform::NoTransform);
	if (handler) {
		button->setClickedCallback([button, handler = std::move(handler)] {
			handler(button);
		});
	} else {
		button->setAttribute(Qt::WA_TransparentForMouseEvents);
	}
	rpl::combine(
		raw->widthValue(),
		button->widthValue(),
		value->naturalWidthValue()
	) | rpl::on_next([=](int width, int buttonWidth, int) {
		const auto buttonSkip = st::normalFont->spacew + buttonWidth;
		value->resizeToNaturalWidth(width - buttonSkip);
		value->moveToLeft(0, 0, width);
		button->moveToLeft(
			rect::right(value) + st::normalFont->spacew,
			(topSkip
				+ (table->st().defaultValue.style.font->ascent
					- table->st().smallButton.style.font->ascent)),
			width);
	}, value->lifetime());

	value->heightValue() | rpl::on_next([=](int height) {
		const auto bottom = st::giveawayGiftCodePeerMargin.bottom();
		raw->resize(raw->width(), height + bottom);
	}, raw->lifetime());

	return result;
}

object_ptr<RpWidget> MakePeerTableValue(
		not_null<TableLayout*> table,
		std::shared_ptr<ChatHelpers::Show> show,
		PeerId id,
		rpl::producer<QString> button,
		Fn<void()> handler) {
	auto result = object_ptr<AbstractButton>(table);
	const auto raw = result.data();

	const auto &st = st::giveawayGiftCodeUserpic;
	raw->resize(raw->width(), st.photoSize);

	const auto peer = show->session().data().peer(id);
	const auto userpic = CreateChild<UserpicButton>(raw, peer, st);
	const auto label = CreateChild<FlatLabel>(
		raw,
		(button && handler) ? peer->shortName() : peer->name(),
		table->st().defaultValue);

	raw->widthValue() | rpl::on_next([=](int width) {
		const auto position = st::giveawayGiftCodeNamePosition;
		label->resizeToNaturalWidth(width - position.x());
		label->moveToLeft(position.x(), position.y(), width);
		const auto top = (raw->height() - userpic->height()) / 2;
		userpic->moveToLeft(0, top, width);
	}, label->lifetime());

	label->naturalWidthValue() | rpl::on_next([=](int width) {
		raw->setNaturalWidth(st::giveawayGiftCodeNamePosition.x() + width);
	}, label->lifetime());
	userpic->setAttribute(Qt::WA_TransparentForMouseEvents);
	label->setAttribute(Qt::WA_TransparentForMouseEvents);
	rpl::single(
		rpl::empty_value()
	) | rpl::then(style::PaletteChanged()) | rpl::on_next([=] {
		label->setTextColorOverride(
			table->st().defaultValue.palette.linkFg->c);
	}, label->lifetime());

	raw->setClickedCallback([=] {
		show->showBox(PrepareShortInfoBox(peer, show));
	});

	if (!button || !handler) {
		return result;
	}
	return MakeValueWithSmallButton(
		table,
		result.release(),
		std::move(button),
		[=](not_null<RpWidget*> button) { handler(); },
		st::giveawayGiftCodeNamePosition.y());
}

object_ptr<RpWidget> MakePeerWithStatusValue(
		not_null<TableLayout*> table,
		std::shared_ptr<ChatHelpers::Show> show,
		PeerId id,
		Fn<void(not_null<RpWidget*>, EmojiStatusId)> pushStatusId) {
	auto result = object_ptr<RpWidget>(table);
	const auto raw = result.data();

	const auto peerLabel = MakePeerTableValue(table, show, id).release();
	peerLabel->setParent(raw);
	peerLabel->show();

	raw->resize(raw->width(), peerLabel->height());

	using namespace Info::Profile;
	struct State {
		rpl::variable<Badge::Content> content;
	};
	const auto peer = show->session().data().peer(id);
	const auto state = peerLabel->lifetime().make_state<State>();
	state->content = EmojiStatusIdValue(
		peer
	) | rpl::map([=](EmojiStatusId emojiStatusId) {
		if (!peer->session().premium()
			|| (!peer->isSelf() && !emojiStatusId)) {
			return Badge::Content();
		}
		return Badge::Content{
			.badge = BadgeType::Premium,
			.emojiStatusId = emojiStatusId,
		};
	});
	const auto badge = peerLabel->lifetime().make_state<Badge>(
		raw,
		st::infoPeerBadge,
		&peer->session(),
		state->content.value(),
		nullptr,
		[=] { return show->paused(ChatHelpers::PauseReason::Layer); });
	state->content.value(
	) | rpl::on_next([=](const Badge::Content &content) {
		if (const auto widget = badge->widget()) {
			pushStatusId(widget, content.emojiStatusId);
		}
	}, raw->lifetime());

	rpl::combine(
		raw->widthValue(),
		rpl::single(rpl::empty) | rpl::then(badge->updated())
	) | rpl::on_next([=](int width, const auto &) {
		const auto badgeWidget = badge->widget();
		const auto badgeSkip = badgeWidget
			? (st::normalFont->spacew + badgeWidget->width())
			: 0;
		peerLabel->resizeToNaturalWidth(width - badgeSkip);
		peerLabel->moveToLeft(0, 0, width);
		if (badgeWidget) {
			badgeWidget->moveToLeft(
				peerLabel->width() + st::normalFont->spacew,
				st::giftBoxByStarsStarTop,
				width);
		}
	}, raw->lifetime());

	return result;
}

object_ptr<RpWidget> MakeHiddenPeerTableValue(
		not_null<TableLayout*> table) {
	auto result = object_ptr<RpWidget>(table);
	const auto raw = result.data();

	const auto &st = st::giveawayGiftCodeUserpic;
	raw->resize(raw->width(), st.photoSize);

	const auto userpic = CreateChild<RpWidget>(raw);
	const auto usize = st.photoSize;
	userpic->resize(usize, usize);
	userpic->paintRequest() | rpl::on_next([=] {
		auto p = QPainter(userpic);
		EmptyUserpic::PaintHiddenAuthor(p, 0, 0, usize, usize);
	}, userpic->lifetime());

	const auto label = CreateChild<FlatLabel>(
		raw,
		tr::lng_gift_from_hidden(),
		table->st().defaultValue);
	raw->widthValue(
	) | rpl::on_next([=](int width) {
		const auto position = st::giveawayGiftCodeNamePosition;
		label->resizeToNaturalWidth(width - position.x());
		label->moveToLeft(position.x(), position.y(), width);
		const auto top = (raw->height() - userpic->height()) / 2;
		userpic->moveToLeft(0, top, width);
	}, label->lifetime());

	userpic->setAttribute(Qt::WA_TransparentForMouseEvents);
	label->setAttribute(Qt::WA_TransparentForMouseEvents);
	rpl::single(
		rpl::empty_value()
	) | rpl::then(style::PaletteChanged()) | rpl::on_next([=] {
		label->setTextColorOverride(st::windowFg->c);
	}, label->lifetime());

	return result;
}

void ShowTableRowTooltip(
		std::shared_ptr<TableRowTooltipData> data,
		not_null<QWidget*> target,
		rpl::producer<TextWithEntities> text,
		int duration,
		const Text::MarkedContext &context) {
	if (data->raw) {
		data->raw->toggleAnimated(false);
	}
	const auto parent = data->parent;
	const auto tooltip = CreateChild<ImportantTooltip>(
		parent,
		MakeNiceTooltipLabel(
			parent,
			std::move(text),
			st::boxWideWidth,
			st::defaultImportantTooltipLabel,
			st::defaultPopupMenu,
			context),
		st::defaultImportantTooltip);
	tooltip->toggleFast(false);

	base::install_event_filter(tooltip, qApp, [=](not_null<QEvent*> e) {
		if (e->type() == QEvent::MouseButtonPress) {
			tooltip->toggleAnimated(false);
		}
		return base::EventFilterResult::Continue;
	});

	const auto update = [=] {
		const auto geometry = MapFrom(parent, target, target->rect());
		const auto countPosition = [=](QSize size) {
			const auto left = geometry.x()
				+ (geometry.width() - size.width()) / 2;
			const auto right = parent->width()
				- st::normalFont->spacew;
			return QPoint(
				std::max(std::min(left, right - size.width()), 0),
				geometry.y() - size.height() - st::normalFont->descent);
		};
		tooltip->pointAt(geometry, RectPart::Top, countPosition);
	};
	parent->widthValue(
	) | rpl::on_next(update, tooltip->lifetime());

	update();
	tooltip->toggleAnimated(true);

	data->raw = tooltip;
	tooltip->shownValue() | rpl::filter(
		!rpl::mappers::_1
	) | rpl::on_next([=] {
		crl::on_main(tooltip, [=] {
			if (tooltip->isHidden()) {
				if (data->raw == tooltip) {
					data->raw = nullptr;
				}
				delete tooltip;
			}
		});
	}, tooltip->lifetime());

	base::timer_once(
		duration
	) | rpl::on_next([=] {
		tooltip->toggleAnimated(false);
	}, tooltip->lifetime());
}

object_ptr<RpWidget> MakeTableValueWithTooltip(
		not_null<TableLayout*> table,
		std::shared_ptr<TableRowTooltipData> data,
		TextWithEntities price,
		TextWithEntities tooltip,
		const Text::MarkedContext &context) {
	const auto label = CreateChild<FlatLabel>(
		table,
		rpl::single(price),
		table->st().defaultValue,
		st::defaultPopupMenu,
		context);
	label->setAttribute(Qt::WA_TransparentForMouseEvents);

	const auto handler = [=](not_null<RpWidget*> button) {
		ShowTableRowTooltip(
			data,
			button,
			rpl::single(tooltip),
			kTooltipDuration,
			context);
	};
	auto text = rpl::single(u"?"_q);
	return MakeValueWithSmallButton(table, label, std::move(text), handler);
}

} // namespace Ui
