/*
This file is part of Telegram Desktop,
the official desktop application for the Telegram messaging service.

For license and copyright information please follow this link:
https://github.com/telegramdesktop/tdesktop/blob/master/LEGAL
*/
#include "payments/ui/payments_reaction_box.h"

#include "base/qt/qt_compare.h"
#include "calls/group/ui/calls_group_stars_coloring.h"
#include "lang/lang_keys.h"
#include "ui/boxes/boost_box.h" // MakeBoostFeaturesBadge.
#include "ui/controls/who_reacted_context_action.h"
#include "ui/effects/premium_bubble.h"
#include "ui/effects/ministar_particles.h"
#include "ui/layers/generic_box.h"
#include "ui/text/text_utilities.h"
#include "ui/widgets/buttons.h"
#include "ui/widgets/checkbox.h"
#include "ui/widgets/labels.h"
#include "ui/widgets/continuous_sliders.h"
#include "ui/widgets/popup_menu.h"
#include "ui/wrap/slide_wrap.h"
#include "ui/color_int_conversion.h"
#include "ui/dynamic_image.h"
#include "ui/painter.h"
#include "ui/vertical_list.h"
#include "styles/style_calls.h"
#include "styles/style_chat.h"
#include "styles/style_chat_helpers.h"
#include "styles/style_credits.h"
#include "styles/style_info.h"
#include "styles/style_info_levels.h"
#include "styles/style_layers.h"
#include "styles/style_media_player.h"
#include "styles/style_premium.h"
#include "styles/style_settings.h"

namespace Settings {
[[nodiscard]] not_null<Ui::RpWidget*> AddBalanceWidget(
	not_null<Ui::RpWidget*> parent,
	not_null<Main::Session*> session,
	rpl::producer<CreditsAmount> balanceValue,
	bool rightAlign,
	rpl::producer<float64> opacityValue = nullptr,
	bool dark = false);
} // namespace Settings

namespace Ui {
namespace {

constexpr auto kMaxTopPaidShown = 3;

struct TopReactorKey {
	std::shared_ptr<DynamicImage> photo;
	int count = 0;
	QString name;

	friend inline auto operator<=>(
		const TopReactorKey &,
		const TopReactorKey &) = default;
	friend inline bool operator==(
		const TopReactorKey &,
		const TopReactorKey &) = default;
};

[[nodiscard]] QImage GenerateBadgeImage(
		const std::vector<Calls::Group::Ui::StarsColoring> &colorings,
		int count,
		bool videoStream) {
	return GenerateSmallBadgeImage(
		Lang::FormatCountDecimal(count),
		st::paidReactTopStarIcon,
		(videoStream
			? Ui::ColorFromSerialized(
				Calls::Group::Ui::StarsColoringForCount(
					colorings,
					count
				).bgLight)
			: st::creditsBg3->c),
		videoStream ? st::white->c : st::premiumButtonFg->c,
		videoStream ? &st::groupCallTopReactorBadge : nullptr);
}

void AddArrowDown(not_null<RpWidget*> widget) {
	const auto arrow = CreateChild<RpWidget>(widget);
	const auto icon = &st::paidReactChannelArrow;
	const auto skip = st::lineWidth * 4;
	const auto size = icon->width() + skip * 2;
	arrow->resize(size, size);
	widget->widthValue() | rpl::on_next([=](int width) {
		const auto left = (width - st::paidReactTopUserpic) / 2;
		arrow->moveToRight(left - skip, -st::lineWidth, width);
	}, widget->lifetime());
	arrow->paintRequest() | rpl::on_next([=] {
		Painter p(arrow);
		auto hq = PainterHighQualityEnabler(p);
		p.setBrush(st::activeButtonBg);
		p.setPen(st::activeButtonFg);
		const auto rect = arrow->rect();
		const auto line = st::lineWidth;
		p.drawEllipse(rect.marginsRemoved({ line, line, line, line }));
		icon->paint(p, skip, (size - icon->height()) / 2 + line, size);
	}, widget->lifetime());
	arrow->setAttribute(Qt::WA_TransparentForMouseEvents);
	arrow->show();
}

[[nodiscard]] not_null<RpWidget*> MakeTopReactor(
		not_null<QWidget*> parent,
		const PaidReactionTop &data,
		int place,
		const std::vector<Calls::Group::Ui::StarsColoring> &colorings,
		Fn<void()> selectShownPeer,
		bool videoStream) {
	auto top = 0;
	auto height = st::paidReactTopNameSkip + st::normalFont->height;
	if (videoStream) {
		top += st::paidReactCrownSkip;
		height += top;
	}

	const auto result = CreateChild<AbstractButton>(parent);
	result->resize(0, height);
	result->show();
	if (data.click && !data.my) {
		result->setClickedCallback(data.click);
	} else if (data.click && selectShownPeer) {
		result->setClickedCallback(selectShownPeer);
		AddArrowDown(result);
	} else {
		result->setAttribute(Qt::WA_TransparentForMouseEvents);
	}

	struct State {
		QImage badge;
		Text::String name;
	};
	const auto state = result->lifetime().make_state<State>();
	state->name.setText(st::defaultTextStyle, data.name);

	const auto count = data.count;
	const auto photo = data.photo->clone();
	photo->subscribeToUpdates([=] {
		result->update();
	});
	style::PaletteChanged(
	) | rpl::on_next([=] {
		state->badge = QImage();
	}, result->lifetime());
	result->paintRequest() | rpl::on_next([=] {
		auto p = Painter(result);
		const auto left = (result->width() - st::paidReactTopUserpic) / 2;
		p.drawImage(left, top, photo->image(st::paidReactTopUserpic));

		if (state->badge.isNull()) {
			state->badge = GenerateBadgeImage(colorings, count, videoStream);
		}
		const auto bwidth = state->badge.width()
			/ state->badge.devicePixelRatio();
		p.drawImage(
			(result->width() - bwidth) / 2,
			top + st::paidReactTopBadgeSkip,
			state->badge);

		if (videoStream) {
			const auto bg = Calls::Group::Ui::StarsColoringForCount(
				colorings,
				count
			).bgLight;
			const auto &icon = st::paidReactCrown;
			const auto left = (result->width() - icon.width()) / 2;
			const auto shift = st::paidReactCrownOutline;
			const auto outline = st::groupCallMembersBg->c;
			icon.paint(p, left - shift, shift, result->width(), outline);
			icon.paint(p, left + shift, shift, result->width(), outline);
			icon.paint(p, left, 0, result->width(), bg);

			const auto top = st::paidReactCrownTop;
			p.setPen(st::white);
			p.setFont(st::levelStyle.font);
			p.drawText(
				QRect(left, top, icon.width(), icon.height()),
				QString::number(place),
				style::al_top);

			p.setPen(st::groupCallMembersFg);
		} else {
			p.setPen(st::windowFg);
		}
		const auto skip = st::normalFont->spacew;
		const auto nameTop = top + st::paidReactTopNameSkip;
		const auto available = result->width() - skip * 2;
		state->name.draw(p, skip, nameTop, available, style::al_top);
	}, result->lifetime());

	return result;
}

void SelectShownPeer(
		std::shared_ptr<base::weak_qptr<PopupMenu>> menu,
		not_null<QWidget*> parent,
		const std::vector<PaidReactionTop> &mine,
		uint64 selected,
		Fn<void(uint64)> callback) {
	if (*menu) {
		(*menu)->hideMenu();
	}
	(*menu) = CreateChild<PopupMenu>(
		parent,
		st::paidReactChannelMenu);

	struct Entry {
		not_null<Ui::WhoReactedEntryAction*> action;
		std::shared_ptr<Ui::DynamicImage> userpic;
	};
	auto actions = std::make_shared<std::vector<Entry>>();
	actions->reserve(mine.size());
	for (const auto &entry : mine) {
		auto action = base::make_unique_q<WhoReactedEntryAction>(
			(*menu)->menu(),
			nullptr,
			(*menu)->menu()->st(),
			Ui::WhoReactedEntryData());
		const auto index = int(actions->size());
		actions->push_back({ action.get(), entry.photo->clone() });
		const auto id = entry.barePeerId;
		const auto updateUserpic = [=] {
			const auto size = st::defaultWhoRead.photoSize;
			actions->at(index).action->setData({
				.text = entry.name,
				.type = ((id == selected)
					? Ui::WhoReactedType::RefRecipientNow
					: Ui::WhoReactedType::RefRecipient),
				.userpic = actions->at(index).userpic->image(size),
				.callback = [=] { callback(id); },
			});
		};
		actions->back().userpic->subscribeToUpdates(updateUserpic);

		(*menu)->addAction(std::move(action));
		updateUserpic();
	}
	(*menu)->popup(QCursor::pos());
}

void FillTopReactors(
		not_null<VerticalLayout*> container,
		std::vector<PaidReactionTop> top,
		const std::vector<Calls::Group::Ui::StarsColoring> &colorings,
		rpl::producer<int> chosen,
		rpl::producer<uint64> shownPeer,
		Fn<void(uint64)> changeShownPeer,
		bool videoStream) {
	const auto badge = videoStream
		? nullptr
		: container->add(
			object_ptr<SlideWrap<RpWidget>>(
				container,
				MakeBoostFeaturesBadge(
					container,
					tr::lng_paid_react_top_title(),
					[](QRect) { return st::creditsBg3->b; }),
				st::paidReactTopTitleMargin),
			st::boxRowPadding,
			style::al_top);
	const auto wrap = container->add(
		object_ptr<SlideWrap<FixedHeightWidget>>(
			container,
			object_ptr<FixedHeightWidget>(container, 0),
			st::paidReactTopMargin));
	const auto parent = wrap->entity();
	using Key = TopReactorKey;
	struct State {
		base::flat_map<Key, not_null<RpWidget*>> cache;
		std::vector<not_null<RpWidget*>> widgets;
		rpl::event_stream<> updated;
		std::optional<int> initialChosen;
		bool chosenChanged = false;
	};
	const auto state = wrap->lifetime().make_state<State>();
	const auto menu = std::make_shared<base::weak_qptr<Ui::PopupMenu>>();

	rpl::combine(
		std::move(chosen),
		std::move(shownPeer)
	) | rpl::on_next([=](int chosen, uint64 barePeerId) {
		if (!state->initialChosen) {
			state->initialChosen = chosen;
		} else if (*state->initialChosen != chosen) {
			state->chosenChanged = true;
		}
		auto mine = std::vector<PaidReactionTop>();
		auto list = std::vector<PaidReactionTop>();
		list.reserve(kMaxTopPaidShown + 1);
		for (const auto &entry : top) {
			if (!entry.my) {
				list.push_back(entry);
			} else if (entry.barePeerId == barePeerId) {
				auto copy = entry;
				if (state->chosenChanged) {
					copy.count += chosen;
				}
				list.push_back(copy);
			}
			if (entry.my && entry.barePeerId) {
				mine.push_back(entry);
			}
		}
		ranges::stable_sort(
			list,
			ranges::greater(),
			&PaidReactionTop::count);
		while (list.size() > kMaxTopPaidShown
			|| (!list.empty() && !list.back().count)) {
			list.pop_back();
		}
		auto selectShownPeer = (mine.size() < 2)
			? Fn<void()>()
			: [=] { SelectShownPeer(
				menu,
				parent,
				mine,
				barePeerId,
				changeShownPeer); };
		if (list.empty()) {
			if (badge) {
				badge->hide(anim::type::normal);
			}
			wrap->hide(anim::type::normal);
		} else {
			if (badge) {
				badge->show(anim::type::normal);
			}
			for (const auto &widget : state->widgets) {
				widget->hide();
			}
			state->widgets.clear();
			auto index = 0;
			for (const auto &entry : list) {
				const auto key = Key{
					.photo = entry.photo,
					.count = entry.count,
					.name = entry.name,
				};
				const auto i = state->cache.find(key);
				const auto widget = (i != end(state->cache))
					? i->second
					: MakeTopReactor(
						parent,
						entry,
						++index,
						colorings,
						selectShownPeer,
						videoStream);
				state->widgets.push_back(widget);
				widget->show();
			}
			for (const auto &[k, widget] : state->cache) {
				if (widget->isHidden()) {
					delete widget;
				}
			}
			wrap->show(anim::type::normal);
		}

		state->updated.fire({});
	}, wrap->lifetime());
	if (badge) {
		badge->finishAnimating();
	}
	wrap->finishAnimating();

	rpl::combine(
		state->updated.events_starting_with({}),
		wrap->widthValue()
	) | rpl::on_next([=](auto, int width) {
		if (!state->widgets.empty()) {
			parent->resize(parent->width(), state->widgets.back()->height());
		}
		const auto single = width / 4;
		if (single <= st::paidReactTopUserpic) {
			return;
		}
		const auto count = int(state->widgets.size());
		auto left = (width - single * count) / 2;
		for (const auto &widget : state->widgets) {
			widget->setGeometry(left, 0, single, widget->height());
			left += single;
		}
	}, wrap->lifetime());
}

[[nodiscard]] not_null<RpWidget*> MakeStarSelectInfoBlock(
		not_null<RpWidget*> parent,
		rpl::producer<TextWithEntities> title,
		rpl::producer<QString> subtext,
		Fn<void()> click,
		Text::MarkedContext context,
		bool dark) {
	const auto result = CreateChild<AbstractButton>(parent);

	const auto titleHeight = st::starSelectInfoTitle.style.font->height;
	const auto subtextHeight = st::starSelectInfoSubtext.style.font->height;
	const auto height = titleHeight + subtextHeight;

	result->paintRequest() | rpl::on_next([=] {
		auto p = QPainter(result);
		auto hq = PainterHighQualityEnabler(p);
		p.setPen(Qt::NoPen);
		p.setBrush(dark ? st::groupCallMembersBgOver : st::windowBgOver);
		const auto radius = st::boxRadius;
		p.drawRoundedRect(result->rect(), radius, radius);
	}, result->lifetime());

	result->resize(
		result->width(),
		QSize(height, height).grownBy(st::starSelectInfoPadding).height());

	const auto titleLabel = CreateChild<FlatLabel>(
		result,
		std::move(title),
		dark ? st::videoStreamInfoTitle : st::starSelectInfoTitle,
		st::defaultPopupMenu,
		context);
	const auto subtextLabel = CreateChild<FlatLabel>(
		result,
		std::move(subtext),
		dark ? st::videoStreamInfoSubtext : st::starSelectInfoSubtext);

	if (click) {
		result->setClickedCallback(std::move(click));
		titleLabel->setAttribute(Qt::WA_TransparentForMouseEvents);
		subtextLabel->setAttribute(Qt::WA_TransparentForMouseEvents);
	} else {
		result->setAttribute(Qt::WA_TransparentForMouseEvents);
	}

	rpl::combine(
		result->widthValue(),
		titleLabel->widthValue(),
		subtextLabel->widthValue()
	) | rpl::on_next([=](int width, int titlew, int subtextw) {
		const auto padding = st::starSelectInfoPadding;
		titleLabel->moveToLeft((width - titlew) / 2, padding.top(), width);
		subtextLabel->moveToLeft(
			(width - subtextw) / 2,
			padding.top() + titleHeight,
			width);
	}, result->lifetime());

	return result;
}

} // namespace

void PaidReactionsBox(
		not_null<GenericBox*> box,
		PaidReactionBoxArgs &&args) {
	Expects(!args.top.empty());

	const auto dark = args.dark;
	args.min = std::max(args.min, 1);
	args.max = std::max({
		args.min + 1,
		args.max,
		args.explicitlyAllowed,
		args.chosen,
	});

	const auto allowed = args.explicitlyAllowed;
	args.chosen = (allowed && args.chosen == allowed)
		? allowed
		: std::clamp(args.chosen, args.min, args.max);

	box->setWidth(st::boxWideWidth);
	box->setStyle(dark ? st::darkEditStarsBox : st::paidReactBox);
	box->setNoContentMargin(true);

	struct State {
		rpl::variable<int> chosen;
		rpl::variable<uint64> shownPeer;
		uint64 savedShownPeer = 0;
	};
	const auto state = box->lifetime().make_state<State>();

	state->chosen = args.chosen;
	const auto changed = [=](int count) {
		state->chosen = count;
	};

	const auto colorings = args.colorings;
	const auto videoStreamChoosing = args.videoStreamChoosing;
	const auto videoStreamSending = args.videoStreamSending;
	const auto videoStreamAdmin = args.videoStreamAdmin;
	const auto videoStream = videoStreamChoosing || videoStreamSending;
	const auto initialShownPeer = ranges::find(
		args.top,
		true,
		&PaidReactionTop::my
	)->barePeerId;
	state->shownPeer = initialShownPeer;
	state->savedShownPeer = ranges::find_if(args.top, [](
			const PaidReactionTop &entry) {
		return entry.my && entry.barePeerId != 0;
	})->barePeerId;

	const auto content = box->verticalLayout();
	AddSkip(content, st::boxTitleClose.height + st::paidReactBubbleTop);

	const auto activeFgOverride = [=](int count) {
		const auto coloring = Calls::Group::Ui::StarsColoringForCount(
			colorings,
			count);
		return Ui::ColorFromSerialized(coloring.bgLight);
	};
	AddStarSelectBubble(
		content,
		BoxShowFinishes(box),
		state->chosen.value(),
		args.max,
		videoStream ? activeFgOverride : Fn<QColor(int)>());

	const auto already = ranges::find(
		args.top,
		true,
		&PaidReactionTop::my)->count;
	PaidReactionSlider(
		content,
		(dark ? st::darkEditStarsSlider : st::paidReactSlider),
		args.min,
		args.explicitlyAllowed,
		rpl::single(args.chosen),
		args.max,
		changed,
		videoStream ? activeFgOverride : Fn<QColor(int)>());

	box->addTopButton(
		dark ? st::darkEditStarsClose : st::boxTitleClose,
		[=] { box->closeBox(); });

	const auto addTopReactors = [&] {
		FillTopReactors(
			content,
			std::move(args.top),
			colorings,
			(videoStreamAdmin
				? rpl::single(state->chosen.current())
				: state->chosen.value() | rpl::type_erased),
			(videoStreamAdmin
				? rpl::single(state->shownPeer.current())
				: state->shownPeer.value() | rpl::type_erased),
			[=](uint64 barePeerId) {
				state->shownPeer = state->savedShownPeer = barePeerId;
			},
			videoStream);
	};

	if (videoStreamChoosing) {
		using namespace Calls::Group::Ui;
		box->addRow(
			VideoStreamStarsLevel(box, colorings, state->chosen.value()),
			st::boxRowPadding + QMargins(0, st::paidReactTitleSkip, 0, 0));
	} else if (videoStreamSending) {
		addTopReactors();
	}

	box->addRow(
		object_ptr<FlatLabel>(
			box,
			(videoStreamAdmin
				? tr::lng_paid_admin_title()
				: videoStreamChoosing
				? tr::lng_paid_comment_title()
				: videoStreamSending
				? tr::lng_paid_reaction_title()
				: tr::lng_paid_react_title()),
			dark ? st::darkEditStarsCenteredTitle : st::boostCenteredTitle),
		st::boxRowPadding + QMargins(0, st::paidReactTitleSkip, 0, 0),
		style::al_top);

	const auto labelWrap = box->addRow(
		object_ptr<RpWidget>(box),
		(st::boxRowPadding
			+ QMargins(0, st::lineWidth, 0, st::boostBottomSkip)));
	const auto label = CreateChild<FlatLabel>(
		labelWrap,
		(videoStreamAdmin
			? tr::lng_paid_admin_about(tr::marked)
			: videoStream
			? (videoStreamChoosing
				? tr::lng_paid_comment_about
				: tr::lng_paid_reaction_about)(
					lt_name,
					rpl::single(tr::bold(args.name)),
					tr::rich)
			: already
			? tr::lng_paid_react_already(
				lt_count,
				rpl::single(already) | tr::to_count(),
				tr::rich)
			: tr::lng_paid_react_about(
				lt_channel,
				rpl::single(tr::bold(args.name)),
				tr::rich)),
		dark ? st::darkEditStarsText : st::boostText);
	label->setTryMakeSimilarLines(true);
	labelWrap->widthValue() | rpl::on_next([=](int width) {
		label->resizeToWidth(width);
	}, label->lifetime());
	label->heightValue() | rpl::on_next([=](int height) {
		const auto min = 2 * st::normalFont->height;
		const auto skip = std::max((min - height) / 2, 0);
		labelWrap->resize(labelWrap->width(), 2 * skip + height);
		label->moveToLeft(0, skip);
	}, label->lifetime());

	if (!videoStream) {
		addTopReactors();

		const auto skip = st::defaultCheckbox.margin.bottom();
		const auto named = box->addRow(
			object_ptr<Checkbox>(
				box,
				tr::lng_paid_react_show_in_top(tr::now),
				state->shownPeer.current() != 0,
				st::paidReactBoxCheckbox),
			st::boxRowPadding + QMargins(0, 0, 0, skip),
			style::al_top);
		named->checkedValue(
		) | rpl::on_next([=](bool show) {
			state->shownPeer = show ? state->savedShownPeer : 0;
		}, named->lifetime());
	}

	AddSkip(content);
	AddSkip(content);

	AddDividerText(
		content,
		(videoStreamAdmin
			? tr::lng_paid_react_admin_cant(tr::marked)
			: tr::lng_paid_react_agree(
				lt_link,
				rpl::combine(
					tr::lng_paid_react_agree_link(),
					tr::lng_group_invite_subscription_about_url()
				) | rpl::map([](const QString &text, const QString &url) {
					return tr::link(text, url);
				}),
				tr::rich)),
		st::defaultBoxDividerLabelPadding,
		dark ? st::groupCallDividerLabel : st::defaultDividerLabel);

	const auto button = box->addButton(rpl::single(QString()), [=] {
		args.send(state->chosen.current(), state->shownPeer.current());
	});

	box->boxClosing() | rpl::filter([=] {
		return state->shownPeer.current() != initialShownPeer;
	}) | rpl::on_next([=] {
		args.send(0, state->shownPeer.current());
	}, box->lifetime());

	button->setText(args.submit(state->chosen.value()));

	AddStarSelectBalance(
		box,
		args.session,
		std::move(args.balanceValue),
		dark);
}

object_ptr<BoxContent> MakePaidReactionBox(PaidReactionBoxArgs &&args) {
	return Box(PaidReactionsBox, std::move(args));
}

int MaxTopPaidDonorsShown() {
	return kMaxTopPaidShown;
}

QImage GenerateSmallBadgeImage(
		QString text,
		const style::icon &icon,
		QColor bg,
		QColor fg,
		const style::RoundCheckbox *borderSt) {
	const auto length = st::chatSimilarBadgeFont->width(text);
	const auto contents = st::chatSimilarLockedIconPosition.x()
		+ icon.width()
		+ st::paidReactTopStarSkip
		+ length;
	const auto badge = QRect(
		st::chatSimilarBadgePadding.left(),
		st::chatSimilarBadgePadding.top(),
		contents,
		st::chatSimilarBadgeFont->height);
	const auto rect = badge.marginsAdded(st::chatSimilarBadgePadding);
	const auto add = borderSt ? borderSt->width : 0;
	const auto ratio = style::DevicePixelRatio();
	auto result = QImage(
		(rect + QMargins(add, add, add, add)).size() * ratio,
		QImage::Format_ARGB32_Premultiplied);
	result.setDevicePixelRatio(ratio);
	result.fill(Qt::transparent);
	auto q = QPainter(&result);

	const auto &font = st::chatSimilarBadgeFont;
	const auto textTop = badge.y() + font->ascent;
	const auto position = st::chatSimilarLockedIconPosition;

	auto hq = PainterHighQualityEnabler(q);
	q.translate(add, add);
	q.setBrush(bg);
	if (borderSt) {
		q.setPen(QPen(borderSt->border->c, borderSt->width));
	} else {
		q.setPen(Qt::NoPen);
	}
	const auto radius = rect.height() / 2.;
	const auto shift = add / 2.;
	q.drawRoundedRect(
		QRectF(rect) + QMarginsF(shift, shift, shift, shift),
		radius,
		radius);

	auto textLeft = 0;
	icon.paint(
		q,
		badge.x() + position.x(),
		badge.y() + position.y(),
		rect.width());
	textLeft += position.x() + icon.width() + st::paidReactTopStarSkip;

	q.setFont(font);
	q.setPen(fg);
	q.drawText(textLeft, textTop, text);
	q.end();

	return result;
}

StarSelectDiscreter StarSelectDiscreterForMax(int max) {
	Expects(max >= 2);

	// 1/8 of width is 1..10
	// 1/3 of width is 1..100
	// 2/3 of width is 1..1000

	auto thresholds = base::flat_map<float64, int>();
	thresholds.emplace(0., 1);
	if (max <= 40) {
		thresholds.emplace(1., max);
	} else if (max <= 300) {
		thresholds.emplace(1. / 4, 10);
		thresholds.emplace(1., max);
	} else if (max <= 600) {
		thresholds.emplace(1. / 8, 10);
		thresholds.emplace(1. / 2, 100);
		thresholds.emplace(1., max);
	} else if (max <= 1900) {
		thresholds.emplace(1. / 8, 10);
		thresholds.emplace(1. / 3, 100);
		thresholds.emplace(1., max);
	} else if (max <= 10000) {
		thresholds.emplace(1. / 8, 10);
		thresholds.emplace(1. / 3, 100);
		thresholds.emplace(2. / 3, 1000);
		thresholds.emplace(1., max);
	} else {
		thresholds.emplace(1. / 10, 10);
		thresholds.emplace(1. / 6, 100);
		thresholds.emplace(1. / 3, 1000);
		thresholds.emplace(1., max);
	}

	const auto ratioToValue = [=](float64 ratio) {
		ratio = std::clamp(ratio, 0., 1.);
		const auto j = thresholds.lower_bound(ratio);
		if (j == begin(thresholds)) {
			return 1;
		}
		const auto i = j - 1;
		const auto progress = (ratio - i->first) / (j->first - i->first);
		const auto value = i->second + (j->second - i->second) * progress;
		return int(base::SafeRound(value));
	};
	const auto valueToRatio = [=](int value) {
		value = std::clamp(value, 1, max);
		auto i = begin(thresholds);
		auto j = i + 1;
		while (j->second < value) {
			i = j++;
		}
		const auto progress = (value - i->second)
			/ float64(j->second - i->second);
		return i->first + (j->first - i->first) * progress;
	};
	return {
		.ratioToValue = ratioToValue,
		.valueToRatio = valueToRatio,
	};
}

void PaidReactionSlider(
		not_null<VerticalLayout*> container,
		const style::MediaSlider &st,
		int min,
		int explicitlyAllowed,
		rpl::producer<int> current,
		int max,
		Fn<void(int)> changed,
		Fn<QColor(int)> activeFgOverride) {
	Expects(explicitlyAllowed <= max);

	if (!explicitlyAllowed) {
		explicitlyAllowed = min;
	}
	const auto slider = container->add(
		object_ptr<MediaSlider>(container, st),
		st::boxRowPadding + QMargins(0, st::paidReactSliderTop, 0, 0));
	slider->resize(slider->width(), st::paidReactSlider.seekSize.height());

	const auto update = [=](int count) {
		if (activeFgOverride) {
			const auto color = activeFgOverride(count);
			slider->setColorOverrides({
				.activeBg = color,
				.activeBorder = color,
				.seekFg = st::groupCallMembersFg->c,
				.seekBorder = color,
				.inactiveBorder = Qt::transparent,
			});
		}
	};

	const auto discreter = StarSelectDiscreterForMax(max);
	slider->setAlwaysDisplayMarker(true);
	slider->setDirection(ContinuousSlider::Direction::Horizontal);

	const auto ratioToValue = [=](float64 ratio) {
		const auto value = discreter.ratioToValue(ratio);
		return (value <= explicitlyAllowed && explicitlyAllowed < min)
			? explicitlyAllowed
			: std::max(value, min);
	};

	std::move(current) | rpl::on_next([=](int value) {
		value = std::clamp(value, 1, max);
		if (discreter.ratioToValue(slider->value()) != value) {
			slider->setValue(discreter.valueToRatio(value));
			update(value);
		}
	}, slider->lifetime());

	slider->setAdjustCallback([=](float64 ratio) {
		return discreter.valueToRatio(ratioToValue(ratio));
	});
	const auto callback = [=](float64 ratio) {
		const auto value = ratioToValue(ratio);
		update(value);
		changed(value);
	};
	slider->setChangeProgressCallback(callback);
	slider->setChangeFinishedCallback(callback);



	struct State {
		StarParticles particles = StarParticles(
			StarParticles::Type::Right,
			200,
			st::lineWidth * 7);
		Ui::Animations::Basic animation;
	};
	const auto state = slider->lifetime().make_state<State>();

	const auto stars = Ui::CreateChild<Ui::RpWidget>(slider->parentWidget());
	stars->show();
	stars->raise();
	slider->geometryValue() | rpl::on_next([=](QRect rect) {
		stars->setGeometry(rect);
	}, stars->lifetime());

	state->animation.init([=] { stars->update(); });
	stars->setAttribute(Qt::WA_TransparentForMouseEvents);

	const auto seekSize = st::paidReactSlider.seekSize.width();
	const auto seekRadius = seekSize / 2.;
	stars->paintRequest() | rpl::on_next([=] {
		if (!state->animation.animating()) {
			state->animation.start();
		}
		auto p = QPainter(stars);
		auto hq = PainterHighQualityEnabler(p);
		const auto progress = slider->value();
		const auto rect = stars->rect();
		const auto availableWidth = rect.width() - seekSize;
		const auto seekCenter = seekRadius + availableWidth * progress;

		state->particles.setSpeed(.1 + progress * .3);
		state->particles.setVisible(.25 + .65 * progress);

		auto fullPath = QPainterPath();
		fullPath.addRoundedRect(QRectF(rect), seekRadius, seekRadius);
		auto circlePath = QPainterPath();
		circlePath.addEllipse(
			QPointF(seekCenter, rect.height() / 2.),
			seekRadius,
			seekRadius);
		auto rightRect = QPainterPath();
		rightRect.addRect(
			QRectF(seekCenter, 0, rect.width() - seekCenter, rect.height()));

		p.setClipPath(fullPath.subtracted(circlePath));
		state->particles.setColor(Qt::white);
		state->particles.paint(p, rect, crl::now());
		p.setClipping(false);

		p.setClipPath(fullPath.intersected(circlePath.united(rightRect)));
		state->particles.setColor(activeFgOverride
			? st::groupCallMemberInactiveIcon->c
			: st::creditsBg3->c);
		state->particles.paint(p, rect, crl::now());
	}, stars->lifetime());
}

void AddStarSelectBalance(
		not_null<GenericBox*> box,
		not_null<Main::Session*> session,
		rpl::producer<CreditsAmount> balanceValue,
		bool dark) {
	const auto balance = Settings::AddBalanceWidget(
		box->verticalLayout(),
		session,
		std::move(balanceValue),
		false,
		nullptr,
		dark);
	rpl::combine(
		balance->sizeValue(),
		box->widthValue()
	) | rpl::on_next([=] {
		balance->moveToLeft(
			st::creditsHistoryRightSkip * 2,
			st::creditsHistoryRightSkip);
		balance->update();
	}, balance->lifetime());
}

not_null<Premium::BubbleWidget*> AddStarSelectBubble(
		not_null<VerticalLayout*> container,
		rpl::producer<> showFinishes,
		rpl::producer<int> value,
		int max,
		Fn<QColor(int)> activeFgOverride) {
	const auto valueToRatio = StarSelectDiscreterForMax(max).valueToRatio;
	auto bubbleRowState = rpl::duplicate(value) | rpl::map([=](int value) {
		const auto full = st::boxWideWidth
			- st::boxRowPadding.left()
			- st::boxRowPadding.right();
		const auto marker = st::paidReactSlider.seekSize.width();
		const auto start = marker / 2;
		const auto inner = full - marker;
		const auto correct = start + inner * valueToRatio(value);
		return Premium::BubbleRowState{
			.counter = value,
			.ratio = correct / full,
		};
	});

	const auto bubble = Premium::AddBubbleRow(
		container,
		st::boostBubble,
		std::move(showFinishes),
		std::move(bubbleRowState),
		Premium::BubbleType::Credits,
		nullptr,
		&st::paidReactBubbleIcon,
		st::boxRowPadding);
	bubble->show();
	if (activeFgOverride) {
		std::move(value) | rpl::on_next([=](int count) {
			bubble->setBrushOverride(activeFgOverride(count));
		}, bubble->lifetime());
	}
	return bubble;
}

object_ptr<RpWidget> MakeStarSelectInfoBlocks(
		not_null<RpWidget*> parent,
		std::vector<StarSelectInfoBlock> blocks,
		Text::MarkedContext context,
		bool dark) {
	Expects(!blocks.empty());

	auto result = object_ptr<RpWidget>(parent.get());
	const auto raw = result.data();

	struct State {
		std::vector<not_null<RpWidget*>> blocks;
	};
	const auto state = raw->lifetime().make_state<State>();

	for (auto &info : blocks) {
		state->blocks.push_back(MakeStarSelectInfoBlock(
			raw,
			std::move(info.title),
			std::move(info.subtext),
			std::move(info.click),
			context,
			dark));
	}
	raw->resize(raw->width(), state->blocks.front()->height());
	raw->widthValue() | rpl::on_next([=](int width) {
		const auto count = int(state->blocks.size());
		const auto skip = (st::boxRowPadding.left() / 2);
		const auto single = (width - skip * (count - 1)) / float64(count);
		if (single < 1.) {
			return;
		}
		auto x = 0.;
		const auto w = int(base::SafeRound(single));
		for (const auto &block : state->blocks) {
			block->resizeToWidth(w);
			block->moveToLeft(int(base::SafeRound(x)), 0);
			x += single + skip;
		}
	}, raw->lifetime());

	return result;
}

} // namespace Ui
