/*
This file is part of Telegram Desktop,
the official desktop application for the Telegram messaging service.

For license and copyright information please follow this link:
https://github.com/telegramdesktop/tdesktop/blob/master/LEGAL
*/
#include "boxes/star_gift_resale_box.h"

#include "boxes/star_gift_box.h"
#include "boxes/transfer_gift_box.h"
#include "chat_helpers/compose/compose_show.h"
#include "core/ui_integration.h"
#include "data/stickers/data_custom_emoji.h"
#include "data/data_peer.h"
#include "data/data_session.h"
#include "data/data_star_gift.h"
#include "data/data_user.h"
#include "lang/lang_keys.h"
#include "info/peer_gifts/info_peer_gifts_common.h"
#include "main/main_session.h"
#include "menu/gift_resale_filter.h"
#include "ui/layers/generic_box.h"
#include "ui/text/text_utilities.h"
#include "ui/toast/toast.h"
#include "ui/widgets/buttons.h"
#include "ui/widgets/popup_menu.h"
#include "ui/wrap/slide_wrap.h"
#include "ui/painter.h"
#include "ui/ui_utility.h"
#include "window/window_session_controller.h"
#include "styles/style_chat_helpers.h"
#include "styles/style_credits.h"
#include "styles/style_layers.h"
#include "styles/style_menu_icons.h"

#include <QtWidgets/QApplication>

namespace Ui {
namespace {

constexpr auto kFiltersCount = 4;
constexpr auto kResaleBoughtToastDuration = 4 * crl::time(1000);

using Data::GiftAttributeId;
using Data::GiftAttributeIdType;
using Data::ResaleGiftsSort;
using Data::ResaleGiftsFilter;
using Data::ResaleGiftsDescriptor;
//using Data::MyGiftsDescriptor;

[[nodiscard]] Text::String ResaleTabText(QString text) {
	auto result = Text::String();
	result.setMarkedText(
		st::semiboldTextStyle,
		TextWithEntities{ text }.append(st::giftBoxResaleTabsDropdown),
		kMarkupTextOptions);
	return result;
}

[[nodiscard]] Text::String SortModeText(ResaleGiftsSort mode) {
	auto text = [&] {
		if (mode == ResaleGiftsSort::Number) {
			return Ui::Text::IconEmoji(&st::giftBoxResaleMiniNumber).append(
				tr::lng_gift_resale_number(tr::now));
		} else if (mode == ResaleGiftsSort::Price) {
			return Ui::Text::IconEmoji(&st::giftBoxResaleMiniPrice).append(
				tr::lng_gift_resale_price(tr::now));
		}
		return Ui::Text::IconEmoji(&st::giftBoxResaleMiniDate).append(
			tr::lng_gift_resale_date(tr::now));
	}();
	auto result = Text::String();
	result.setMarkedText(
		st::semiboldTextStyle,
		text,
		kMarkupTextOptions);
	return result;
}

struct ResaleTabs {
	rpl::producer<ResaleGiftsFilter> filter;
	object_ptr<RpWidget> widget;
};
[[nodiscard]] ResaleTabs MakeResaleTabs(
		std::shared_ptr<ChatHelpers::Show> show,
		const ResaleGiftsDescriptor &info,
		rpl::producer<ResaleGiftsFilter> filter) {
	auto widget = object_ptr<RpWidget>((QWidget*)nullptr);
	const auto raw = widget.data();

	struct Button {
		QRect geometry;
		Text::String text;
	};
	struct State {
		rpl::variable<ResaleGiftsFilter> filter;
		rpl::variable<int> fullWidth;
		std::vector<Button> buttons;
		base::unique_qptr<Ui::PopupMenu> menu;
		ResaleGiftsDescriptor lists;
		int dragx = 0;
		int pressx = 0;
		float64 dragscroll = 0.;
		float64 scroll = 0.;
		int scrollMax = 0;
		int selected = -1;
		int pressed = -1;
	};
	const auto state = raw->lifetime().make_state<State>();
	state->filter = std::move(filter);

	const auto forCraft = state->filter.current().forCraft;
	const auto wrapName = [=](QString name, int count) {
		auto result = tr::marked(name);
		if (!forCraft) {
			result.append(' ').append(tr::bold(
				Lang::FormatCountDecimal(count)));
		}
		return result;
	};

	state->lists.backdrops = info.backdrops;
	state->lists.models = info.models;
	state->lists.patterns = info.patterns;
	if (forCraft) {
		using namespace Data;
		const auto isRare = [](const UniqueGiftModelCount &entry) {
			return entry.model.rarityType() != UniqueGiftRarity::Default;
		};
		state->lists.models.erase(
			ranges::remove_if(state->lists.models, isRare),
			end(state->lists.models));
	}

	const auto scroll = [=] {
		return QPoint(int(base::SafeRound(state->scroll)), 0);
	};

	static constexpr auto IndexToType = [](int index) {
		Expects(index > 0 && index < 4);

		return (index == 1)
			? GiftAttributeIdType::Model
			: (index == 2)
			? GiftAttributeIdType::Backdrop
			: GiftAttributeIdType::Pattern;
	};

	const auto setSelected = [=](int index) {
		const auto was = (state->selected >= 0);
		const auto now = (index >= 0);
		state->selected = index;
		if (was != now) {
			raw->setCursor(now ? style::cur_pointer : style::cur_default);
		}
	};
	const auto showMenu = [=](int index) {
		if (state->menu) {
			return;
		}
		state->menu = base::make_unique_q<Ui::PopupMenu>(
			raw,
			st::giftBoxResaleFilter);
		const auto menu = state->menu.get();
		const auto modify = [=](Fn<void(ResaleGiftsFilter&)> modifier) {
			auto now = state->filter.current();
			modifier(now);
			state->filter = now;
		};
		const auto actionWithIcon = [=](
				QString text,
				Fn<void()> callback,
				not_null<const style::icon*> icon,
				bool checked = false) {
			auto action = base::make_unique_q<Ui::GiftResaleFilterAction>(
				menu->menu(),
				menu->st().menu,
				TextWithEntities{ text },
				Ui::Text::MarkedContext(),
				QString(),
				icon);
			action->setChecked(checked);
			action->setActionTriggered(std::move(callback));
			menu->addAction(std::move(action));
		};
		auto context = Core::TextContext({ .session = &show->session() });
		context.customEmojiFactory = [original = context.customEmojiFactory](
				QStringView data,
				const Ui::Text::MarkedContext &context) {
			return Ui::GiftResaleColorEmoji::Owns(data)
				? std::make_unique<Ui::GiftResaleColorEmoji>(data)
				: original(data, context);
		};
		const auto actionWithEmoji = [=](
				TextWithEntities text,
				Fn<void()> callback,
				QString data,
				bool checked) {
			auto action = base::make_unique_q<Ui::GiftResaleFilterAction>(
				menu->menu(),
				menu->st().menu,
				std::move(text),
				context,
				data,
				nullptr);
			action->setChecked(checked);
			action->setActionTriggered(std::move(callback));
			menu->addAction(std::move(action));
		};
		const auto actionWithDocument = [=](
				TextWithEntities text,
				Fn<void()> callback,
				DocumentId id,
				bool checked) {
			actionWithEmoji(
				std::move(text),
				std::move(callback),
				Data::SerializeCustomEmojiId(id),
				checked);
		};
		const auto actionWithColor = [=](
				TextWithEntities text,
				Fn<void()> callback,
				const QColor &color,
				bool checked) {
			actionWithEmoji(
				std::move(text),
				std::move(callback),
				Ui::GiftResaleColorEmoji::DataFor(color),
				checked);
		};
		if (!index) {
			const auto sort = [=](ResaleGiftsSort value) {
				modify([&](ResaleGiftsFilter &filter) {
					filter.sort = value;
				});
			};
			const auto is = [&](ResaleGiftsSort value) {
				return state->filter.current().sort == value;
			};
			actionWithIcon(tr::lng_gift_resale_sort_price(tr::now), [=] {
				sort(ResaleGiftsSort::Price);
			}, &st::menuIconOrderPrice, is(ResaleGiftsSort::Price));
			actionWithIcon(tr::lng_gift_resale_sort_date(tr::now), [=] {
				sort(ResaleGiftsSort::Date);
			}, &st::menuIconOrderDate, is(ResaleGiftsSort::Date));
			actionWithIcon(tr::lng_gift_resale_sort_number(tr::now), [=] {
				sort(ResaleGiftsSort::Number);
			}, &st::menuIconOrderNumber, is(ResaleGiftsSort::Number));
			menu->addSeparator();
			const auto starsOnly = state->filter.current().starsOnly;
			actionWithIcon(tr::lng_gift_resale_all_listings(tr::now), [=] {
				modify([&](ResaleGiftsFilter &filter) {
					filter.starsOnly = false;
				});
			}, &st::menuIconTopics, !starsOnly);
			actionWithIcon(tr::lng_gift_resale_stars_only(tr::now), [=] {
				modify([&](ResaleGiftsFilter &filter) {
					filter.starsOnly = true;
				});
			}, &st::menuIconStar, starsOnly);
		} else {
			const auto now = state->filter.current().attributes;
			const auto type = IndexToType(index);
			const auto has = ranges::contains(
				now,
				type,
				&GiftAttributeId::type);
			if (has) {
				actionWithIcon(tr::lng_gift_resale_filter_all(tr::now), [=] {
					modify([&](ResaleGiftsFilter &filter) {
						auto &list = filter.attributes;
						for (auto i = begin(list); i != end(list);) {
							if (i->type == type) {
								i = list.erase(i);
							} else {
								++i;
							}
						}
					});
				}, &st::menuIconSelect);
			}
			const auto toggle = [=](GiftAttributeId id) {
				modify([&](ResaleGiftsFilter &filter) {
					auto &list = filter.attributes;
					if (ranges::contains(list, id)) {
						list.remove(id);
					} else {
						list.emplace(id);
					}
				});
			};
			const auto checked = [=](GiftAttributeId id) {
				return !has || ranges::contains(now, id);
			};
			if (type == GiftAttributeIdType::Model) {
				for (auto &entry : state->lists.models) {
					const auto id = IdFor(entry.model);
					const auto text = wrapName(
						entry.model.name,
						entry.count);
					actionWithDocument(text, [=] {
						toggle(id);
					}, id.value, checked(id));
				}
			} else if (type == GiftAttributeIdType::Backdrop) {
				for (auto &entry : state->lists.backdrops) {
					const auto id = IdFor(entry.backdrop);
					const auto text = wrapName(
						entry.backdrop.name,
						entry.count);
					actionWithColor(text, [=] {
						toggle(id);
					}, entry.backdrop.centerColor, checked(id));
				}
			} else if (type == GiftAttributeIdType::Pattern) {
				for (auto &entry : state->lists.patterns) {
					const auto id = IdFor(entry.pattern);
					const auto text = wrapName(
						entry.pattern.name,
						entry.count);
					actionWithDocument(text, [=] {
						toggle(id);
					}, id.value, checked(id));
				}
			}
		}
		menu->popup(QCursor::pos());
	};

	state->filter.value(
	) | rpl::on_next([=](const ResaleGiftsFilter &fields) {
		auto x = st::giftBoxResaleTabsMargin.left();
		auto y = st::giftBoxResaleTabsMargin.top();

		setSelected(-1);
		state->buttons.resize(kFiltersCount);
		const auto &list = fields.attributes;
		const auto setForIndex = [&](int i, auto many, auto one) {
			const auto type = IndexToType(i);
			const auto count = ranges::count(
				list,
				type,
				&GiftAttributeId::type);
			state->buttons[i].text = ResaleTabText((count > 0)
				? many(tr::now, lt_count, count)
				: one(tr::now));
		};
		state->buttons[0].text = SortModeText(fields.sort);
		setForIndex(
			1,
			tr::lng_gift_resale_models,
			tr::lng_gift_resale_model);
		setForIndex(
			2,
			tr::lng_gift_resale_backdrops,
			tr::lng_gift_resale_backdrop);
		setForIndex(
			3,
			tr::lng_gift_resale_symbols,
			tr::lng_gift_resale_symbol);

		const auto padding = st::giftBoxTabPadding;
		for (auto &button : state->buttons) {
			const auto width = button.text.maxWidth();
			const auto height = st::giftBoxTabStyle.font->height;
			const auto r = QRect(0, 0, width, height).marginsAdded(padding);
			button.geometry = QRect(QPoint(x, y), r.size());
			x += r.width() + st::giftBoxResaleTabSkip;
		}
		state->fullWidth = x
			- st::giftBoxTabSkip
			+ st::giftBoxTabsMargin.right();
		const auto height = state->buttons.empty()
			? 0
			: (y
				+ state->buttons.back().geometry.height()
				+ st::giftBoxTabsMargin.bottom());
		raw->resize(raw->width(), height);
		raw->update();
	}, raw->lifetime());

	rpl::combine(
		raw->widthValue(),
		state->fullWidth.value()
	) | rpl::on_next([=](int outer, int inner) {
		state->scrollMax = std::max(0, inner - outer);
	}, raw->lifetime());

	raw->setMouseTracking(true);
	raw->events() | rpl::on_next([=](not_null<QEvent*> e) {
		const auto type = e->type();
		switch (type) {
		case QEvent::Leave: setSelected(-1); break;
		case QEvent::MouseMove: {
			const auto me = static_cast<QMouseEvent*>(e.get());
			const auto mousex = me->pos().x();
			const auto drag = QApplication::startDragDistance();
			if (state->dragx > 0) {
				state->scroll = std::clamp(
					state->dragscroll + state->dragx - mousex,
					0.,
					state->scrollMax * 1.);
				raw->update();
				break;
			} else if (state->pressx > 0
				&& std::abs(state->pressx - mousex) > drag) {
				state->dragx = state->pressx;
				state->dragscroll = state->scroll;
			}
			const auto position = me->pos() + scroll();
			for (auto i = 0, c = int(state->buttons.size()); i != c; ++i) {
				if (state->buttons[i].geometry.contains(position)) {
					setSelected(i);
					break;
				}
			}
		} break;
		case QEvent::Wheel: {
			const auto me = static_cast<QWheelEvent*>(e.get());
			state->scroll = std::clamp(
				state->scroll - ScrollDeltaF(me).x(),
				0.,
				state->scrollMax * 1.);
			raw->update();
		} break;
		case QEvent::MouseButtonPress: {
			const auto me = static_cast<QMouseEvent*>(e.get());
			if (me->button() != Qt::LeftButton) {
				break;
			}
			state->pressed = state->selected;
			state->pressx = me->pos().x();
		} break;
		case QEvent::MouseButtonRelease: {
			const auto me = static_cast<QMouseEvent*>(e.get());
			if (me->button() != Qt::LeftButton) {
				break;
			}
			const auto dragx = std::exchange(state->dragx, 0);
			const auto pressed = std::exchange(state->pressed, -1);
			state->pressx = 0;
			if (!dragx && pressed >= 0 && state->selected == pressed) {
				showMenu(pressed);
			}
		} break;
		}
	}, raw->lifetime());

	raw->paintRequest() | rpl::on_next([=] {
		auto p = QPainter(raw);
		auto hq = PainterHighQualityEnabler(p);
		const auto padding = st::giftBoxTabPadding;
		const auto shift = -scroll();
		for (const auto &button : state->buttons) {
			const auto geometry = button.geometry.translated(shift);

			p.setBrush(st::giftBoxTabBgActive);
			p.setPen(Qt::NoPen);
			const auto radius = geometry.height() / 2.;
			p.drawRoundedRect(geometry, radius, radius);
			p.setPen(st::giftBoxTabFgActive);

			button.text.draw(p, {
				.position = geometry.marginsRemoved(padding).topLeft(),
				.availableWidth = button.text.maxWidth(),
			});
		}
		{
			const auto &icon = st::defaultEmojiSuggestions;
			const auto w = icon.fadeRight.width();
			const auto &c = st::boxDividerBg->c;
			const auto r = QRect(0, 0, w, raw->height());
			const auto s = std::abs(float64(shift.x()));
			constexpr auto kF = 0.5;
			const auto opacityRight = (state->scrollMax - s)
				/ (icon.fadeRight.width() * kF);
			p.setOpacity(std::clamp(std::abs(opacityRight), 0., 1.));
			icon.fadeRight.fill(p, r.translated(raw->width() -  w, 0), c);

			const auto opacityLeft = s / (icon.fadeLeft.width() * kF);
			p.setOpacity(std::clamp(std::abs(opacityLeft), 0., 1.));
			icon.fadeLeft.fill(p, r, c);
		}
	}, raw->lifetime());

	return {
		.filter = state->filter.value(),
		.widget = std::move(widget),
	};
}

void GiftResaleBox(
		not_null<GenericBox*> box,
		not_null<Window::SessionController*> window,
		not_null<PeerData*> peer,
		ResaleGiftsDescriptor descriptor) {
	box->setWidth(st::boxWideWidth);

	const auto titleWrap = box->setPinnedToTopContent(
		object_ptr<Ui::VerticalLayout>(box.get()));

	titleWrap->add(object_ptr<Ui::FixedHeightWidget>(
		titleWrap,
		st::defaultVerticalListSkip));

	titleWrap->add(
		object_ptr<Ui::FlatLabel>(
			titleWrap,
			rpl::single(descriptor.title),
			st::boxTitle),
		QMargins(st::boxRowPadding.left(), 0, st::boxRowPadding.right(), 0));

	const auto countLabel = titleWrap->add(
		object_ptr<Ui::FlatLabel>(
			titleWrap,
			(descriptor.count
				? tr::lng_gift_resale_count(
					tr::now,
					lt_count,
					descriptor.count)
				: tr::lng_gift_resale_count_none(tr::now)),
			st::defaultFlatLabel),
		QMargins(
			st::boxRowPadding.left(),
			0,
			st::boxRowPadding.right(),
			st::defaultVerticalListSkip));
	countLabel->setTextColorOverride(st::windowSubTextFg->c);

	const auto content = box->verticalLayout();
	content->paintRequest() | rpl::on_next([=](QRect clip) {
		QPainter(content).fillRect(clip, st::boxDividerBg);
	}, content->lifetime());

	struct State {
		rpl::variable<bool> starsOnly;
		int lastMinHeight = 0;
	};
	const auto state = content->lifetime().make_state<State>();

	box->addButton(tr::lng_create_group_back(), [=] { box->closeBox(); });

	const auto filter = box->addLeftButton(rpl::single(QString()), [=] {
		state->starsOnly = !state->starsOnly.current();
	});
	filter->setText(rpl::conditional(
		state->starsOnly.value(),
		tr::lng_gift_resale_all_listings(),
		tr::lng_gift_resale_stars_only()));

	box->heightValue() | rpl::on_next([=](int height) {
		if (height > state->lastMinHeight) {
			state->lastMinHeight = height;
			box->setMinHeight(height);
		}
	}, content->lifetime());

	AddResaleGiftsList(
		window,
		peer,
		content,
		std::move(descriptor),
		&state->starsOnly,
		nullptr,
		false,
		[=](int count) {
			countLabel->setText(count
				? tr::lng_gift_resale_count(
					tr::now,
					lt_count,
					count)
				: tr::lng_gift_resale_count_none(tr::now));
		});
}

} // namespace

void AddResaleGiftsList(
		not_null<Window::SessionController*> window,
		not_null<PeerData*> peer,
		not_null<VerticalLayout*> container,
		Data::ResaleGiftsDescriptor descriptor,
		rpl::variable<bool> *starsOnly,
		Fn<void(std::shared_ptr<Data::UniqueGift>)> bought,
		bool forCraft,
		Fn<void(int)> countChanged) {
	struct State {
		rpl::event_stream<> updated;
		ResaleGiftsDescriptor data;
		rpl::variable<ResaleGiftsFilter> filter;
		rpl::variable<bool> empty = true;
		rpl::lifetime loading;
		int lastMinHeight = 0;
	};
	const auto state = container->lifetime().make_state<State>();
	state->filter = ResaleGiftsFilter{ .forCraft = forCraft };
	state->data = std::move(descriptor);

	auto tabs = MakeResaleTabs(
		window->uiShow(),
		state->data,
		state->filter.value());
	state->filter = std::move(tabs.filter);
	if (starsOnly) {
		starsOnly->changes(
		) | rpl::on_next([=](bool value) {
			auto f = state->filter.current();
			if (f.starsOnly != value) {
				f.starsOnly = value;
				state->filter = f;
			}
		}, container->lifetime());

		state->filter.changes(
		) | rpl::on_next([=](const ResaleGiftsFilter &f) {
			if (starsOnly->current() != f.starsOnly) {
				*starsOnly = f.starsOnly;
			}
		}, container->lifetime());

		if (starsOnly->current()) {
			auto f = state->filter.current();
			f.starsOnly = true;
			state->filter = f;
		}
	}
	if (forCraft) {
		const auto skip = st::giftBoxResaleTabsMargin.top()
			- st::giftBoxTabsMargin.bottom();
		const auto wrap = container->add(object_ptr<PaddingWrap<>>(
			container,
			std::move(tabs.widget),
			QMargins(0, 0, 0, skip)));
		wrap->paintOn([=](QPainter &p) {
			p.fillRect(wrap->rect(), st::windowBgOver);
		});
	} else {
		container->add(std::move(tabs.widget));
	}

	const auto session = &window->session();
	state->filter.changes() | rpl::on_next([=](ResaleGiftsFilter value) {
		state->data.offset = QString();
		state->loading = ResaleGiftsSlice(
			session,
			state->data.giftId,
			value,
			QString()
		) | rpl::on_next([=](ResaleGiftsDescriptor &&slice) {
			state->loading.destroy();
			state->data.offset = slice.list.empty()
				? QString()
				: slice.offset;
			state->data.count = slice.count;
			state->data.list = std::move(slice.list);
			if (countChanged) {
				countChanged(state->data.count);
			}
			state->updated.fire({});
		});
	}, container->lifetime());

	session->data().giftUpdates(
	) | rpl::on_next([=](const Data::GiftUpdate &update) {
		using Action = Data::GiftUpdate::Action;
		const auto action = update.action;
		if (action != Action::Transfer && action != Action::ResaleChange) {
			return;
		}
		const auto i = ranges::find(
			state->data.list,
			update.slug,
			[](const Data::StarGift &gift) {
				return gift.unique ? gift.unique->slug : QString();
			});
		if (i == end(state->data.list)) {
			return;
		} else if (action == Action::Transfer
			|| !i->unique->starsForResale) {
			state->data.list.erase(i);
		}
		state->updated.fire({});
	}, container->lifetime());

	using Descriptor = Info::PeerGifts::GiftDescriptor;
	auto customHandler = Fn<void(Descriptor)>();
	if (bought) {
		using StarGift = Info::PeerGifts::GiftTypeStars;
		customHandler = crl::guard(container, [=](Descriptor descriptor) {
			Expects(v::is<StarGift>(descriptor));

			const auto unique = v::get<StarGift>(descriptor).info.unique;
			const auto done = crl::guard(container, [=](bool ok) {
				if (ok) {
					bought(unique);
				}
			});
			const auto to = peer->session().user();
			ShowBuyResaleGiftBox(window->uiShow(), unique, false, to, done);
		});
	}

	auto gifts = rpl::single(
		rpl::empty
	) | rpl::then(
		state->updated.events() | rpl::type_erased
	) | rpl::map([=] {
		auto result = GiftsDescriptor();
		const auto selfId = window->session().userPeerId();
		for (const auto &gift : state->data.list) {
			const auto mine = (gift.unique->ownerId == selfId);
			if (mine && forCraft) {
				continue;
			}
			result.list.push_back(Info::PeerGifts::GiftTypeStars{
				.info = gift,
				.resale = true,
				.mine = mine,
				});
		}
		state->empty = result.list.empty();
		return result;
	});
	const auto loadMore = [=] {
		if (!state->data.offset.isEmpty()
			&& !state->loading) {
			state->loading = ResaleGiftsSlice(
				&peer->session(),
				state->data.giftId,
				state->filter.current(),
				state->data.offset
			) | rpl::on_next([=](ResaleGiftsDescriptor &&slice) {
				state->loading.destroy();
				state->data.offset = slice.list.empty()
					? QString()
					: slice.offset;
				state->data.list.insert(
					end(state->data.list),
					std::make_move_iterator(begin(slice.list)),
					std::make_move_iterator(end(slice.list)));
				state->updated.fire({});
			});
		}
	};
	container->add(MakeGiftsList({
		.window = window,
		.mode = forCraft ? GiftsListMode::CraftResale : GiftsListMode::Send,
		.peer = peer,
		.gifts = std::move(gifts),
		.loadMore = loadMore,
		.handler = customHandler,
	}));

	const auto skip = st::defaultSubsectionTitlePadding.top();
	const auto wrap = container->add(
		object_ptr<SlideWrap<FlatLabel>>(
			container,
			object_ptr<FlatLabel>(
				container,
				(forCraft
					? tr::lng_gift_craft_search_none()
					: tr::lng_gift_resale_search_none()),
				st::craftYourListEmpty),
			(st::boxRowPadding + QMargins(0, 0, 0, skip))),
		style::al_top);
	state->empty.value() | rpl::on_next([=](bool empty) {
		// Scroll doesn't jump up if we show before rows are cleared,
		// and we hide after rows are added.
		if (empty) {
			wrap->show(anim::type::instant);
		} else {
			crl::on_main(wrap, [=] {
				if (!state->empty.current()) {
					wrap->hide(anim::type::instant);
				}
			});
		}
	}, wrap->lifetime());
	wrap->entity()->setTryMakeSimilarLines(true);
}

void ShowResaleGiftBoughtToast(
		std::shared_ptr<Main::SessionShow> show,
		not_null<PeerData*> to,
		const Data::UniqueGift &gift) {
	show->showToast({
		.title = to->isSelf() ? QString() : tr::lng_gift_sent_title(tr::now),
		.text = TextWithEntities{ (to->isSelf()
			? tr::lng_gift_sent_resale_done_self(
				tr::now,
				lt_gift,
				Data::UniqueGiftName(gift))
			: tr::lng_gift_sent_resale_done(
				tr::now,
				lt_user,
				to->shortName())),
		},
		.duration = kResaleBoughtToastDuration,
	});
}

rpl::lifetime ShowStarGiftResale(
		not_null<Window::SessionController*> controller,
		not_null<PeerData*> peer,
		uint64 giftId,
		QString title,
		Fn<void()> finishRequesting) {
	const auto weak = base::make_weak(controller);
	const auto session = &controller->session();
	return Data::ResaleGiftsSlice(
		session,
		giftId
	) | rpl::on_next([=](ResaleGiftsDescriptor &&info) {
		if (const auto onstack = finishRequesting) {
			onstack();
		}
		if (!info.giftId || !info.count) {
			return;
		}
		info.title = title;
		if (const auto strong = weak.get()) {
			strong->show(Box(GiftResaleBox, strong, peer, std::move(info)));
		}
	});
}

} // namespace Ui
