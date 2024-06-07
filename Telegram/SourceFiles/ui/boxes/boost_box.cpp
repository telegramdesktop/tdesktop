/*
This file is part of Telegram Desktop,
the official desktop application for the Telegram messaging service.

For license and copyright information please follow this link:
https://github.com/telegramdesktop/tdesktop/blob/master/LEGAL
*/
#include "ui/boxes/boost_box.h"

#include "info/profile/info_profile_icon.h"
#include "lang/lang_keys.h"
#include "ui/boxes/confirm_box.h"
#include "ui/effects/fireworks_animation.h"
#include "ui/effects/premium_graphics.h"
#include "ui/layers/generic_box.h"
#include "ui/text/text_utilities.h"
#include "ui/widgets/buttons.h"
#include "ui/wrap/fade_wrap.h"
#include "ui/painter.h"
#include "styles/style_giveaway.h"
#include "styles/style_layers.h"
#include "styles/style_premium.h"

#include <QtGui/QGuiApplication>

namespace Ui {
namespace {

[[nodiscard]] BoostCounters AdjustByReached(BoostCounters data) {
	const auto exact = (data.boosts == data.thisLevelBoosts);
	const auto reached = !data.nextLevelBoosts || (exact && data.mine > 0);
	if (reached) {
		--data.level;
		data.boosts = data.nextLevelBoosts = std::max({
			data.boosts,
			data.thisLevelBoosts,
			1
		});
		data.thisLevelBoosts = 0;
	} else {
		data.boosts = std::max(data.thisLevelBoosts, data.boosts);
		data.nextLevelBoosts = std::max(
			data.nextLevelBoosts,
			data.boosts + 1);
	}
	return data;
}

[[nodiscard]] object_ptr<Ui::RpWidget> MakeTitle(
		not_null<Ui::RpWidget*> parent,
		rpl::producer<QString> title,
		rpl::producer<QString> repeated,
		bool centered = true) {
	auto result = object_ptr<Ui::RpWidget>(parent);

	struct State {
		not_null<Ui::FlatLabel*> title;
		not_null<Ui::FlatLabel*> repeated;
	};
	const auto notEmpty = [](const QString &text) {
		return !text.isEmpty();
	};
	const auto state = parent->lifetime().make_state<State>(State{
		.title = Ui::CreateChild<Ui::FlatLabel>(
			result.data(),
			rpl::duplicate(title),
			st::boostTitle),
		.repeated = Ui::CreateChild<Ui::FlatLabel>(
			result.data(),
			rpl::duplicate(repeated) | rpl::filter(notEmpty),
			st::boostTitleBadge),
	});
	state->title->show();
	state->repeated->showOn(std::move(repeated) | rpl::map(notEmpty));

	result->resize(result->width(), st::boostTitle.style.font->height);

	rpl::combine(
		result->widthValue(),
		rpl::duplicate(title),
		state->repeated->shownValue(),
		state->repeated->widthValue()
	) | rpl::start_with_next([=](int outer, auto&&, bool shown, int badge) {
		const auto repeated = shown ? badge : 0;
		const auto skip = st::boostTitleBadgeSkip;
		const auto available = outer - repeated - skip;
		const auto use = std::min(state->title->textMaxWidth(), available);
		state->title->resizeToWidth(use);
		const auto left = centered
			? (outer - use - skip - repeated) / 2
			: 0;
		state->title->moveToLeft(left, 0);
		const auto mleft = st::boostTitleBadge.margin.left();
		const auto mtop = st::boostTitleBadge.margin.top();
		state->repeated->moveToLeft(left + use + skip + mleft, mtop);
	}, result->lifetime());

	const auto badge = state->repeated;
	badge->paintRequest() | rpl::start_with_next([=] {
		auto p = QPainter(badge);
		auto hq = PainterHighQualityEnabler(p);
		const auto radius = std::min(badge->width(), badge->height()) / 2;
		p.setPen(Qt::NoPen);
		p.setBrush(st::premiumButtonBg2);
		p.drawRoundedRect(badge->rect(), radius, radius);
	}, badge->lifetime());

	return result;
}

[[nodiscard]] object_ptr<Ui::FlatLabel> MakeFeaturesBadge(
		not_null<QWidget*> parent,
		rpl::producer<QString> text) {
	auto result = object_ptr<Ui::FlatLabel>(
		parent,
		std::move(text),
		st::boostLevelBadge);
	const auto label = result.data();

	label->show();
	label->paintRequest() | rpl::start_with_next([=] {
		const auto size = label->textMaxWidth();
		const auto rect = QRect(
			(label->width() - size) / 2,
			st::boostLevelBadge.margin.top(),
			size,
			st::boostLevelBadge.style.font->height
		).marginsAdded(st::boostLevelBadge.margin);
		auto p = QPainter(label);
		auto hq = PainterHighQualityEnabler(p);
		auto gradient = QLinearGradient(
			rect.topLeft(),
			rect.topRight());
		gradient.setStops(Ui::Premium::GiftGradientStops());
		p.setBrush(gradient);
		p.setPen(Qt::NoPen);
		p.drawRoundedRect(rect, rect.height() / 2., rect.height() / 2.);

		const auto &lineFg = st::windowBgRipple;
		const auto line = st::boostLevelBadgeLine;
		const auto top = st::boostLevelBadge.margin.top()
			+ ((st::boostLevelBadge.style.font->height - line) / 2);
		const auto left = 0;
		const auto skip = st::boostLevelBadgeSkip;
		if (const auto right = rect.x() - skip; right > left) {
			p.fillRect(left, top, right - left, line, lineFg);
		}
		const auto right = label->width();
		if (const auto left = rect.x() + rect.width() + skip
			; left < right) {
			p.fillRect(left, top, right - left, line, lineFg);
		}
	}, label->lifetime());

	return result;
}

void AddFeaturesList(
		not_null<Ui::VerticalLayout*> container,
		const Ui::BoostFeatures &features,
		int startFromLevel,
		bool group) {
	const auto add = [&](
			rpl::producer<TextWithEntities> text,
			const style::icon &st) {
		const auto label = container->add(
			object_ptr<Ui::FlatLabel>(
				container,
				std::move(text),
				st::boostFeatureLabel),
			st::boostFeaturePadding);
		object_ptr<Info::Profile::FloatingIcon>(
			label,
			st,
			st::boostFeatureIconPosition);
	};
	const auto proj = &Ui::Text::RichLangValue;
	const auto max = std::max({
		features.linkLogoLevel,
		features.transcribeLevel,
		features.emojiPackLevel,
		features.emojiStatusLevel,
		features.wallpaperLevel,
		features.customWallpaperLevel,
		(features.nameColorsByLevel.empty()
			? 0
			: features.nameColorsByLevel.back().first),
		(features.linkStylesByLevel.empty()
			? 0
			: features.linkStylesByLevel.back().first),
	});
	auto nameColors = 0;
	auto linkStyles = 0;
	for (auto i = std::max(startFromLevel, 1); i <= max; ++i) {
		const auto unlocks = (i == startFromLevel);
		container->add(
			MakeFeaturesBadge(
				container,
				(unlocks
					? tr::lng_boost_level_unlocks
					: tr::lng_boost_level)(
						lt_count,
						rpl::single(float64(i)))),
			st::boostLevelBadgePadding);
		if (i >= features.customWallpaperLevel) {
			add(
				(group
					? tr::lng_feature_custom_background_group
					: tr::lng_feature_custom_background_channel)(proj),
				st::boostFeatureCustomBackground);
		}
		if (i >= features.wallpaperLevel) {
			add(
				(group
					? tr::lng_feature_backgrounds_group
					: tr::lng_feature_backgrounds_channel)(
						lt_count,
						rpl::single(float64(features.wallpapersCount)),
						proj),
				st::boostFeatureBackground);
		}
		if (i >= features.emojiStatusLevel) {
			add(
				tr::lng_feature_emoji_status(proj),
				st::boostFeatureEmojiStatus);
		}
		if (group && i >= features.transcribeLevel) {
			add(
				tr::lng_feature_transcribe(proj),
				st::boostFeatureTranscribe);
		}
		if (group && i >= features.emojiPackLevel) {
			add(
				tr::lng_feature_custom_emoji_pack(proj),
				st::boostFeatureCustomEmoji);
		}
		if (!group) {
			if (const auto j = features.linkStylesByLevel.find(i)
				; j != end(features.linkStylesByLevel)) {
				linkStyles += j->second;
			}
			if (i >= features.linkLogoLevel) {
				add(
					tr::lng_feature_link_emoji(proj),
					st::boostFeatureCustomLink);
			}
			if (linkStyles > 0) {
				add(tr::lng_feature_link_style_channel(
					lt_count,
					rpl::single(float64(linkStyles)),
					proj
				), st::boostFeatureLink);
			}
			if (const auto j = features.nameColorsByLevel.find(i)
				; j != end(features.nameColorsByLevel)) {
				nameColors += j->second;
			}
			if (nameColors > 0) {
				add(tr::lng_feature_name_color_channel(
					lt_count,
					rpl::single(float64(nameColors)),
					proj
				), st::boostFeatureName);
			}
			add(tr::lng_feature_reactions(
				lt_count,
				rpl::single(float64(i)),
				proj
			), st::boostFeatureCustomReactions);
		}
		add(
			tr::lng_feature_stories(lt_count, rpl::single(float64(i)), proj),
			st::boostFeatureStories);
	}
}

} // namespace

void StartFireworks(not_null<QWidget*> parent) {
	const auto result = Ui::CreateChild<RpWidget>(parent.get());
	result->setAttribute(Qt::WA_TransparentForMouseEvents);
	result->setGeometry(parent->rect());
	result->show();

	auto &lifetime = result->lifetime();
	const auto animation = lifetime.make_state<FireworksAnimation>([=] {
		result->update();
	});
	result->paintRequest() | rpl::start_with_next([=] {
		auto p = QPainter(result);
		if (!animation->paint(p, result->rect())) {
			crl::on_main(result, [=] { delete result; });
		}
	}, lifetime);
}

void BoostBox(
		not_null<GenericBox*> box,
		BoostBoxData data,
		Fn<void(Fn<void(BoostCounters)>)> boost) {
	box->setWidth(st::boxWideWidth);
	box->setStyle(st::boostBox);

	//AssertIsDebug();
	//data.boost = {
	//	.level = 2,
	//	.boosts = 3,
	//	.thisLevelBoosts = 2,
	//	.nextLevelBoosts = 5,
	//	.mine = 2,
	//};

	struct State {
		rpl::variable<BoostCounters> data;
		rpl::variable<bool> full;
		bool submitted = false;
	};
	const auto state = box->lifetime().make_state<State>();
	state->data = std::move(data.boost);

	FillBoostLimit(
		BoxShowFinishes(box),
		box->verticalLayout(),
		state->data.value(),
		st::boxRowPadding);

	box->setMaxHeight(st::boostBoxMaxHeight);
	const auto close = box->addTopButton(
		st::boxTitleClose,
		[=] { box->closeBox(); });

	const auto name = data.name;

	auto title = state->data.value(
	) | rpl::map([=](BoostCounters counters) {
		return (counters.mine > 0)
			? tr::lng_boost_channel_you_title(
				lt_channel,
				rpl::single(name))
			: !counters.nextLevelBoosts
			? tr::lng_boost_channel_title_max()
			: counters.level
			? (data.group
				? tr::lng_boost_channel_title_more_group()
				: tr::lng_boost_channel_title_more())
			: (data.group
				? tr::lng_boost_channel_title_first_group()
				: tr::lng_boost_channel_title_first());
	}) | rpl::flatten_latest();
	auto repeated = state->data.value(
	) | rpl::map([=](BoostCounters counters) {
		return (counters.mine > 1) ? u"x%1"_q.arg(counters.mine) : u""_q;
	});

	auto text = state->data.value(
	) | rpl::map([=](BoostCounters counters) {
		const auto bold = Ui::Text::Bold(name);
		const auto now = counters.boosts;
		const auto full = !counters.nextLevelBoosts;
		const auto left = (counters.nextLevelBoosts > now)
			? (counters.nextLevelBoosts - now)
			: 0;
		auto post = tr::lng_boost_channel_post_stories(
			lt_count,
			rpl::single(float64(counters.level + (left ? 1 : 0))),
			Ui::Text::RichLangValue);
		return (counters.mine || full)
			? (left
				? tr::lng_boost_channel_needs_unlock(
					lt_count,
					rpl::single(float64(left)),
					lt_channel,
					rpl::single(bold),
					Ui::Text::RichLangValue)
				: (!counters.level
					? (data.group
						? tr::lng_boost_channel_reached_first_group
						: tr::lng_boost_channel_reached_first)(
							Ui::Text::RichLangValue)
					: (data.group
						? tr::lng_boost_channel_reached_more_group
						: tr::lng_boost_channel_reached_more)(
							lt_count,
							rpl::single(float64(counters.level)),
							lt_post,
							std::move(post),
							Ui::Text::RichLangValue)))
			: tr::lng_boost_channel_needs_unlock(
				lt_count,
				rpl::single(float64(left)),
				lt_channel,
				rpl::single(bold),
				Ui::Text::RichLangValue);
	}) | rpl::flatten_latest();

	auto faded = object_ptr<Ui::FadeWrap<>>(
		close->parentWidget(),
		MakeTitle(
			box,
			(data.group
				? tr::lng_boost_group_button
				: tr::lng_boost_channel_button)(),
			rpl::duplicate(repeated),
			false));
	const auto titleInner = faded.data();
	titleInner->move(st::boxTitlePosition);
	titleInner->resizeToWidth(st::boxWideWidth
		- st::boxTitleClose.width
		- st::boxTitlePosition.x());
	titleInner->hide(anim::type::instant);
	crl::on_main(titleInner, [=] {
		titleInner->raise();
		titleInner->toggleOn(rpl::single(
			rpl::empty
		) | rpl::then(
			box->scrolls()
		) | rpl::map([=] {
			return box->scrollTop() > 0;
		}));
	});

	box->addRow(
		MakeTitle(box, std::move(title), std::move(repeated)),
		st::boxRowPadding + QMargins(0, st::boostTitleSkip, 0, 0));

	box->addRow(
		object_ptr<Ui::FlatLabel>(
			box,
			std::move(text),
			st::boostText),
		(st::boxRowPadding
			+ QMargins(0, st::boostTextSkip, 0, st::boostBottomSkip)));

	const auto current = state->data.current();
	box->setTitle(rpl::single(QString()));
	AddFeaturesList(
		box->verticalLayout(),
		data.features,
		current.level + (current.nextLevelBoosts ? 1 : 0),
		data.group);

	const auto allowMulti = data.allowMulti;
	auto submit = state->data.value(
	) | rpl::map([=](BoostCounters counters) {
		return (!counters.nextLevelBoosts || (counters.mine && !allowMulti))
			? tr::lng_box_ok()
			: (counters.mine > 0)
			? tr::lng_boost_again_button()
			: data.group
			? tr::lng_boost_group_button()
			: tr::lng_boost_channel_button();
	}) | rpl::flatten_latest();

	const auto button = box->addButton(rpl::duplicate(submit), [=] {
		if (state->submitted) {
			return;
		} else if (state->data.current().nextLevelBoosts > 0
			&& (allowMulti || !state->data.current().mine)) {
			state->submitted = true;
			const auto was = state->data.current().mine;

			//AssertIsDebug();
			//state->submitted = false;
			//if (state->data.current().level == 5
			//	&& state->data.current().boosts == 11) {
			//	state->data = BoostCounters{
			//		.level = 5,
			//		.boosts = 14,
			//		.thisLevelBoosts = 9,
			//		.nextLevelBoosts = 15,
			//		.mine = 14,
			//	};
			//} else if (state->data.current().level == 5) {
			//	state->data = BoostCounters{
			//		.level = 7,
			//		.boosts = 16,
			//		.thisLevelBoosts = 15,
			//		.nextLevelBoosts = 19,
			//		.mine = 16,
			//	};
			//} else if (state->data.current().level == 4) {
			//	state->data = BoostCounters{
			//		.level = 5,
			//		.boosts = 11,
			//		.thisLevelBoosts = 9,
			//		.nextLevelBoosts = 15,
			//		.mine = 9,
			//	};
			//} else if (state->data.current().level == 3) {
			//	state->data = BoostCounters{
			//		.level = 4,
			//		.boosts = 7,
			//		.thisLevelBoosts = 7,
			//		.nextLevelBoosts = 9,
			//		.mine = 5,
			//	};
			//} else {
			//	state->data = BoostCounters{
			//		.level = 3,
			//		.boosts = 5,
			//		.thisLevelBoosts = 5,
			//		.nextLevelBoosts = 7,
			//		.mine = 3,
			//	};
			//}
			//return;

			boost(crl::guard(box, [=](BoostCounters result) {
				state->submitted = false;

				if (result.thisLevelBoosts || result.nextLevelBoosts) {
					if (result.mine > was) {
						StartFireworks(box->parentWidget());
					}
					state->data = result;
				}
			}));
		} else {
			box->closeBox();
		}
	});

	rpl::combine(
		std::move(submit),
		box->widthValue()
	) | rpl::start_with_next([=](const QString &, int width) {
		const auto &padding = st::boostBox.buttonPadding;
		button->resizeToWidth(width
			- padding.left()
			- padding.right());
		button->moveToLeft(padding.left(), button->y());
	}, button->lifetime());
}

object_ptr<Ui::RpWidget> MakeLinkLabel(
		not_null<QWidget*> parent,
		rpl::producer<QString> text,
		rpl::producer<QString> link,
		std::shared_ptr<Ui::Show> show,
		object_ptr<Ui::RpWidget> right) {
	auto result = object_ptr<Ui::AbstractButton>(parent);
	const auto raw = result.data();

	const auto rawRight = right.release();
	if (rawRight) {
		rawRight->setParent(raw);
		rawRight->show();
	}

	struct State {
		State(
			not_null<QWidget*> parent,
			rpl::producer<QString> value,
			rpl::producer<QString> link)
		: text(std::move(value))
		, link(std::move(link))
		, label(parent, text.value(), st::giveawayGiftCodeLink)
		, bg(st::roundRadiusLarge, st::windowBgOver) {
		}

		rpl::variable<QString> text;
		rpl::variable<QString> link;
		Ui::FlatLabel label;
		Ui::RoundRect bg;
	};

	const auto state = raw->lifetime().make_state<State>(
		raw,
		rpl::duplicate(text),
		std::move(link));
	state->label.setSelectable(true);

	rpl::combine(
		raw->widthValue(),
		std::move(text)
	) | rpl::start_with_next([=](int outer, const auto&) {
		const auto textWidth = state->label.textMaxWidth();
		const auto skipLeft = st::giveawayGiftCodeLink.margin.left();
		const auto skipRight = rawRight
			? rawRight->width()
			: st::giveawayGiftCodeLink.margin.right();
		const auto available = outer - skipRight - skipLeft;
		const auto use = std::min(textWidth, available);
		state->label.resizeToWidth(use);
		const auto forCenter = (outer - use) / 2;
		const auto x = (forCenter < skipLeft)
			? skipLeft
			: (forCenter > outer - skipRight - use)
			? (outer - skipRight - use)
			: forCenter;
		state->label.moveToLeft(x, st::giveawayGiftCodeLink.margin.top());
	}, raw->lifetime());

	raw->paintRequest() | rpl::start_with_next([=] {
		auto p = QPainter(raw);
		state->bg.paint(p, raw->rect());
	}, raw->lifetime());

	state->label.setAttribute(Qt::WA_TransparentForMouseEvents);

	raw->resize(raw->width(), st::giveawayGiftCodeLinkHeight);
	if (rawRight) {
		raw->widthValue() | rpl::start_with_next([=](int width) {
			rawRight->move(width - rawRight->width(), 0);
		}, raw->lifetime());
	}
	raw->setClickedCallback([=] {
		QGuiApplication::clipboard()->setText(state->link.current());
		show->showToast(tr::lng_username_copied(tr::now));
	});

	return result;
}

void BoostBoxAlready(not_null<GenericBox*> box, bool group) {
	ConfirmBox(box, {
		.text = (group
			? tr::lng_boost_error_already_text_group
			: tr::lng_boost_error_already_text)(Text::RichLangValue),
		.title = tr::lng_boost_error_already_title(),
		.inform = true,
	});
}

void GiftForBoostsBox(
		not_null<GenericBox*> box,
		QString channel,
		int receive,
		bool again) {
	ConfirmBox(box, {
		.text = (again
			? tr::lng_boost_need_more_again
			: tr::lng_boost_need_more_text)(
				lt_count,
				rpl::single(receive) | tr::to_count(),
				lt_channel,
				rpl::single(TextWithEntities{ channel }),
				Text::RichLangValue),
		.title = tr::lng_boost_need_more(),
		.inform = true,
	});
}

void GiftedNoBoostsBox(not_null<GenericBox*> box, bool group) {
	InformBox(box, {
		.text = (group
			? tr::lng_boost_error_gifted_text_group
			: tr::lng_boost_error_gifted_text)(Text::RichLangValue),
		.title = tr::lng_boost_error_gifted_title(),
	});
}

void PremiumForBoostsBox(
		not_null<GenericBox*> box,
		bool group,
		Fn<void()> buyPremium) {
	ConfirmBox(box, {
		.text = (group
			? tr::lng_boost_error_premium_text_group
			: tr::lng_boost_error_premium_text)(Text::RichLangValue),
		.confirmed = buyPremium,
		.confirmText = tr::lng_boost_error_premium_yes(),
		.title = tr::lng_boost_error_premium_title(),
	});
}

void AskBoostBox(
		not_null<GenericBox*> box,
		AskBoostBoxData data,
		Fn<void()> openStatistics,
		Fn<void()> startGiveaway) {
	box->setWidth(st::boxWideWidth);
	box->setStyle(st::boostBox);

	FillBoostLimit(
		BoxShowFinishes(box),
		box->verticalLayout(),
		rpl::single(data.boost),
		st::boxRowPadding);

	box->addTopButton(st::boxTitleClose, [=] { box->closeBox(); });

	auto title = v::match(data.reason.data, [](AskBoostChannelColor) {
		return tr::lng_boost_channel_title_color();
	}, [](AskBoostWallpaper) {
		return tr::lng_boost_channel_title_wallpaper();
	}, [](AskBoostEmojiStatus) {
		return tr::lng_boost_channel_title_status();
	}, [](AskBoostEmojiPack) {
		return tr::lng_boost_group_title_emoji();
	}, [](AskBoostCustomReactions) {
		return tr::lng_boost_channel_title_reactions();
	}, [](AskBoostCpm) {
		return tr::lng_boost_channel_title_cpm();
	});
	auto reasonText = v::match(data.reason.data, [&](
			AskBoostChannelColor data) {
		return tr::lng_boost_channel_needs_level_color(
			lt_count,
			rpl::single(float64(data.requiredLevel)),
			Ui::Text::RichLangValue);
	}, [&](AskBoostWallpaper data) {
		return (data.group
			? tr::lng_boost_group_needs_level_wallpaper
			: tr::lng_boost_channel_needs_level_wallpaper)(
				lt_count,
				rpl::single(float64(data.requiredLevel)),
				Ui::Text::RichLangValue);
	}, [&](AskBoostEmojiStatus data) {
		return (data.group
			? tr::lng_boost_group_needs_level_status
			: tr::lng_boost_channel_needs_level_status)(
				lt_count,
				rpl::single(float64(data.requiredLevel)),
				Ui::Text::RichLangValue);
	}, [&](AskBoostEmojiPack data) {
		return tr::lng_boost_group_needs_level_emoji(
			lt_count,
			rpl::single(float64(data.requiredLevel)),
			Ui::Text::RichLangValue);
	}, [&](AskBoostCustomReactions data) {
		return tr::lng_boost_channel_needs_level_reactions(
			lt_count,
			rpl::single(float64(data.count)),
			lt_same_count,
			rpl::single(TextWithEntities{ QString::number(data.count) }),
			Ui::Text::RichLangValue);
	}, [&](AskBoostCpm data) {
		return tr::lng_boost_channel_needs_level_cpm(
			lt_count,
			rpl::single(float64(data.requiredLevel)),
			Ui::Text::RichLangValue);
	});
	auto text = rpl::combine(
		std::move(reasonText),
		tr::lng_boost_channel_ask(Ui::Text::RichLangValue)
	) | rpl::map([](TextWithEntities &&text, TextWithEntities &&ask) {
		return text.append(u"\n\n"_q).append(std::move(ask));
	});
	box->addRow(
		object_ptr<Ui::FlatLabel>(
			box,
			std::move(title),
			st::boostCenteredTitle),
		st::boxRowPadding + QMargins(0, st::boostTitleSkip, 0, 0));
	box->addRow(
		object_ptr<Ui::FlatLabel>(
			box,
			std::move(text),
			st::boostText),
		(st::boxRowPadding
			+ QMargins(0, st::boostTextSkip, 0, st::boostBottomSkip)));

	auto stats = object_ptr<Ui::IconButton>(box, st::boostLinkStatsButton);
	stats->setClickedCallback(openStatistics);
	box->addRow(MakeLinkLabel(
		box,
		rpl::single(data.link),
		rpl::single(data.link),
		box->uiShow(),
		std::move(stats)));

	auto submit = tr::lng_boost_channel_ask_button();
	const auto button = box->addButton(rpl::duplicate(submit), [=] {
		QGuiApplication::clipboard()->setText(data.link);
		box->uiShow()->showToast(tr::lng_username_copied(tr::now));
	});
	rpl::combine(
		std::move(submit),
		box->widthValue()
	) | rpl::start_with_next([=](const QString &, int width) {
		const auto &padding = st::boostBox.buttonPadding;
		button->resizeToWidth(width
			- padding.left()
			- padding.right());
		button->moveToLeft(padding.left(), button->y());
	}, button->lifetime());
}

void FillBoostLimit(
		rpl::producer<> showFinished,
		not_null<VerticalLayout*> container,
		rpl::producer<BoostCounters> data,
		style::margins limitLinePadding) {
	const auto addSkip = [&](int skip) {
		container->add(object_ptr<Ui::FixedHeightWidget>(container, skip));
	};

	const auto ratio = [=](BoostCounters counters) {
		const auto min = counters.thisLevelBoosts;
		const auto max = counters.nextLevelBoosts;

		Assert(counters.boosts >= min && counters.boosts <= max);
		const auto count = (max - min);
		const auto index = (counters.boosts - min);
		if (!index) {
			return 0.;
		} else if (index == count) {
			return 1.;
		} else if (count == 2) {
			return 0.5;
		}
		const auto available = st::boxWideWidth
			- st::boxPadding.left()
			- st::boxPadding.right();
		const auto average = available / float64(count);
		const auto levelWidth = [&](int add) {
			return st::normalFont->width(
				tr::lng_boost_level(
					tr::now,
					lt_count,
					counters.level + add));
		};
		const auto paddings = 2 * st::premiumLineTextSkip;
		const auto labelLeftWidth = paddings + levelWidth(0);
		const auto labelRightWidth = paddings + levelWidth(1);
		const auto first = std::max(average, labelLeftWidth * 1.);
		const auto last = std::max(average, labelRightWidth * 1.);
		const auto other = (available - first - last) / (count - 2);
		return (first + (index - 1) * other) / available;
	};

	auto adjustedData = rpl::duplicate(data) | rpl::map(AdjustByReached);

	auto bubbleRowState = rpl::duplicate(
		adjustedData
	) | rpl::combine_previous(
		BoostCounters()
	) | rpl::map([=](BoostCounters previous, BoostCounters counters) {
		return Premium::BubbleRowState{
			.counter = counters.boosts,
			.ratio = ratio(counters),
			.animateFromZero = (counters.level != previous.level),
			.dynamic = true,
		};
	});
	Premium::AddBubbleRow(
		container,
		st::boostBubble,
		std::move(showFinished),
		rpl::duplicate(bubbleRowState),
		true,
		nullptr,
		&st::premiumIconBoost,
		limitLinePadding);
	addSkip(st::premiumLineTextSkip);

	const auto level = [](int level) {
		return tr::lng_boost_level(tr::now, lt_count, level);
	};
	auto limitState = std::move(
		bubbleRowState
	) | rpl::map([](const Premium::BubbleRowState &state) {
		return Premium::LimitRowState{
			.ratio = state.ratio,
			.animateFromZero = state.animateFromZero,
			.dynamic = state.dynamic
		};
	});
	auto left = rpl::duplicate(
		adjustedData
	) | rpl::map([=](BoostCounters counters) {
		return level(counters.level);
	});
	auto right = rpl::duplicate(
		adjustedData
	) | rpl::map([=](BoostCounters counters) {
		return level(counters.level + 1);
	});
	Premium::AddLimitRow(
		container,
		st::boostLimits,
		Premium::LimitRowLabels{
			.leftLabel = std::move(left),
			.rightLabel = std::move(right),
		},
		std::move(limitState),
		limitLinePadding);
}

} // namespace Ui
