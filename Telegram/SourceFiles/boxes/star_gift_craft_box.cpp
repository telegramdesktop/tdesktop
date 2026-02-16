/*
This file is part of Telegram Desktop,
the official desktop application for the Telegram messaging service.

For license and copyright information please follow this link:
https://github.com/telegramdesktop/tdesktop/blob/master/LEGAL
*/
#include "boxes/star_gift_craft_box.h"

#include "base/call_delayed.h"
#include "base/event_filter.h"
#include "base/random.h"
#include "base/timer_rpl.h"
#include "base/unixtime.h"
#include "boxes/star_gift_auction_box.h"
#include "boxes/star_gift_craft_animation.h"
#include "boxes/star_gift_preview_box.h"
#include "boxes/star_gift_resale_box.h"
#include "apiwrap.h"
#include "api/api_credits.h"
#include "api/api_premium.h"
#include "boxes/star_gift_box.h"
#include "data/components/gift_auctions.h"
#include "core/ui_integration.h"
#include "data/stickers/data_custom_emoji.h"
#include "data/data_document.h"
#include "data/data_session.h"
#include "data/data_star_gift.h"
#include "data/data_user.h"
#include "info/peer_gifts/info_peer_gifts_common.h"
#include "lang/lang_keys.h"
#include "main/main_app_config.h"
#include "main/main_session.h"
#include "ui/boxes/about_cocoon_box.h" // AddUniqueCloseButton.
#include "ui/boxes/confirm_box.h"
#include "ui/controls/button_labels.h"
#include "ui/controls/feature_list.h"
#include "ui/effects/numbers_animation.h"
#include "ui/layers/generic_box.h"
#include "ui/widgets/buttons.h"
#include "ui/widgets/gradient_round_button.h"
#include "ui/widgets/tooltip.h"
#include "ui/wrap/slide_wrap.h"
#include "ui/painter.h"
#include "ui/top_background_gradient.h"
#include "ui/ui_utility.h"
#include "ui/vertical_list.h"
#include "window/window_session_controller.h"
#include "styles/style_chat.h"
#include "styles/style_chat_helpers.h" // stickerPanDeleteIconFg
#include "styles/style_credits.h"
#include "styles/style_layers.h"
#include "styles/style_menu_icons.h"

namespace Ui {
namespace {

using namespace Info::PeerGifts;

struct ColorScheme {
	Data::UniqueGiftBackdrop backdrop;
	QColor button1;
	QColor button2;
};

[[nodiscard]] QColor ForgeBgOverlay() {
	return QColor(0xBA, 0xDF, 0xFF, 32);
}

[[nodiscard]] QColor ForgeFailOverlay() {
	return QColor(0xE1, 0x79, 0x23, 41);
}

[[nodiscard]] std::array<ColorScheme, 5> CraftBackdrops() {
	struct Colors {
		int center = 0;
		int edge = 0;
		int pattern = 0;
		int button1 = 0;
		int button2 = 0;
	};
	const auto hardcoded = [](Colors colors) {
		auto result = ColorScheme();
		const auto color = [](int value) {
			return QColor(
				(uint32(value) >> 16) & 0xFF,
				(uint32(value) >> 8) & 0xFF,
				(uint32(value)) & 0xFF);
		};
		result.backdrop.centerColor = color(colors.center);
		result.backdrop.edgeColor = color(colors.edge);
		result.backdrop.patternColor = color(colors.pattern);
		result.button1 = color(colors.button1);
		result.button2 = color(colors.button2);
		return result;
	};
	return {
		hardcoded({ 0x2C4359, 0x232E3F, 0x040C1A, 0x10A5DF, 0x2091E9 }),
		hardcoded({ 0x2C4359, 0x232E3F, 0x040C1A, 0x10A5DF, 0x2091E9 }),
		hardcoded({ 0x2C4359, 0x232E3F, 0x040C1A, 0x10A5DF, 0x2091E9 }),
		hardcoded({ 0x1C4843, 0x1A2E37, 0x040C1A, 0x3ACA49, 0x007D9E }),
		hardcoded({ 0x5D2E16, 0x371B1A, 0x040C1A, 0xE27519, 0xDD4819 }),
	};
}

struct GiftForCraft {
	std::shared_ptr<Data::UniqueGift> unique;
	Data::SavedStarGiftId manageId;

	[[nodiscard]] QString slugId() const {
		return unique ? unique->slug : QString();
	}

	explicit operator bool() const {
		return unique != nullptr;
	}
	friend inline bool operator==(
		const GiftForCraft &,
		const GiftForCraft &) = default;
};

struct CraftingView {
	object_ptr<RpWidget> widget;
	rpl::producer<int> editRequests;
	rpl::producer<int> removeRequests;
	Fn<void(std::shared_ptr<CraftState>)> grabForAnimation;
};

void ShowGiftCraftBox(
	not_null<Window::SessionController*> controller,
	std::vector<GiftForCraft> gifts,
	bool autoStartCraft);

[[nodiscard]] QString FormatPercent(int permille) {
	const auto rounded = (permille + 5) / 10;
	return QString::number(rounded) + '%';
}

[[nodiscard]] not_null<RpWidget*> MakeRadialPercent(
		not_null<RpWidget*> parent,
		const style::CraftRadialPercent &st,
		rpl::producer<int> permille,
		Fn<QString(int)> format = FormatPercent) {
	auto raw = CreateChild<RpWidget>(parent);

	struct State {
		State(const style::CraftRadialPercent &st, Fn<void()> callback)
		: numbers(st.font, std::move(callback)) {
			numbers.setDisabledMonospace(true);
		}

		Animations::Simple animation;
		NumbersAnimation numbers;
		int permille = -1;
	};
	const auto state = raw->lifetime().make_state<State>(
		st,
		[=] { raw->update(); });

	std::move(permille) | rpl::on_next([=](int value) {
		if (state->permille == value) {
			return;
		}
		state->animation.start([=] {
			raw->update();
		}, state->permille, value, st::slideWrapDuration);
		state->permille = value;
		state->numbers.setText(format(value), value);
	}, raw->lifetime());
	state->animation.stop();
	state->numbers.finishAnimating();

	raw->show();
	raw->setAttribute(Qt::WA_TransparentForMouseEvents);
	raw->paintOn([=, &st](QPainter &p) {
		static constexpr auto kArcSkip = arc::kFullLength / 4;
		static constexpr auto kArcStart = -(arc::kHalfLength - kArcSkip) / 2;
		static constexpr auto kArcLength = arc::kFullLength - kArcSkip;

		const auto paint = [&](QColor color, float64 permille) {
			p.setPen(QPen(color, st.stroke, Qt::SolidLine, Qt::RoundCap));
			p.setBrush(Qt::NoBrush);
			const auto part = kArcLength * (permille / 1000.);
			const auto length = int(base::SafeRound(part));
			const auto inner = raw->rect().marginsRemoved(
				{ st.stroke, st.stroke, st.stroke, st.stroke });
			p.drawArc(inner, kArcStart + kArcLength - length, length);
		};

		auto hq = PainterHighQualityEnabler(p);

		auto inactive = QColor(255, 255, 255, 64);
		paint(inactive, 1000.);
		paint(st::white->c, state->animation.value(state->permille));

		state->numbers.paint(
			p,
			(raw->width() - state->numbers.countWidth()) / 2,
			raw->height() - st.font->height,
			raw->width());
	});

	return raw;
}

AbstractButton *MakeCornerButton(
		not_null<RpWidget*> parent,
		not_null<GiftButton*> button,
		object_ptr<RpWidget> content,
		style::align align,
		const GiftForCraft &gift,
		rpl::producer<QColor> edgeColor) {
	Expects(content != nullptr);

	const auto result = CreateChild<AbstractButton>(parent);
	result->show();

	const auto inner = content.release();
	inner->setParent(result);
	inner->show();
	inner->sizeValue() | rpl::on_next([=](QSize size) {
		result->resize(size);
	}, result->lifetime());
	inner->move(0, 0);

	rpl::combine(
		button->geometryValue(),
		result->sizeValue()
	) | rpl::on_next([=](QRect geometry, QSize size) {
		const auto extend = st::defaultDropdownMenu.wrap.shadow.extend;
		geometry = geometry.marginsRemoved(extend);
		const auto out = QPoint(size.width(), size.height()) / 3;
		const auto left = (align == style::al_left)
			? (geometry.x() - out.x())
			: (geometry.x() + geometry.width() - size.width() + out.x());
		const auto top = geometry.y() - out.y();
		result->move(left, top);
	}, result->lifetime());

	struct State {
		rpl::variable<QColor> edgeColor;
		QColor buttonEdgeColor;
	};
	const auto state = result->lifetime().make_state<State>();
	state->edgeColor = std::move(edgeColor);
	state->buttonEdgeColor = gift.unique->backdrop.edgeColor;
	result->paintOn([=](QPainter &p) {
		const auto right = result->width();
		const auto bottom = result->height();
		const auto add = QPoint(right, bottom) / 3;
		const auto radius = bottom / 2.;
		auto gradient = QLinearGradient(
			(align == style::al_left) ? -add.x() : (right + add.x()),
			-add.y(),
			(align == style::al_left) ? (right + add.x()) : -add.x(),
			bottom + add.y());
		gradient.setColorAt(0, state->edgeColor.current());
		gradient.setColorAt(1, state->buttonEdgeColor);
		auto hq = PainterHighQualityEnabler(p);
		p.setPen(Qt::NoPen);
		p.setBrush(gradient);
		p.drawRoundedRect(result->rect(), radius, radius);
	});

	return result;
}

AbstractButton *MakePercentButton(
		not_null<RpWidget*> parent,
		not_null<GiftButton*> button,
		const GiftForCraft &gift,
		rpl::producer<QColor> edgeColor) {
	auto label = object_ptr<FlatLabel>(
		parent,
		FormatPercent(gift.unique->craftChancePermille),
		st::craftPercentLabel);
	label->setTextColorOverride(st::white->c);
	const auto result = MakeCornerButton(
		parent,
		button,
		std::move(label),
		style::al_left,
		gift,
		std::move(edgeColor));
	result->setAttribute(Qt::WA_TransparentForMouseEvents);
	return result;
}

AbstractButton *MakeRemoveButton(
		not_null<RpWidget*> parent,
		not_null<GiftButton*> button,
		int size,
		const GiftForCraft &gift,
		Fn<void()> onClick,
		rpl::producer<QColor> edgeColor) {
	auto remove = object_ptr<RpWidget>(parent);
	const auto &icon = st::stickerPanDeleteIconFg;
	const auto add = (size - icon.width()) / 2;
	remove->resize(icon.size() + QSize(add, add) * 2);
	remove->paintOn([=](QPainter &p) {
		const auto &icon = st::stickerPanDeleteIconFg;
		icon.paint(p, add, add, add * 2 + icon.width(), st::white->c);
	});
	remove->setAttribute(Qt::WA_TransparentForMouseEvents);
	const auto result = MakeCornerButton(
		parent,
		button,
		std::move(remove),
		style::al_right,
		gift,
		std::move(edgeColor));
	result->setClickedCallback(std::move(onClick));
	return result;
}

[[nodiscard]] CraftingView MakeCraftingView(
		not_null<RpWidget*> parent,
		not_null<Main::Session*> session,
		rpl::producer<std::vector<GiftForCraft>> chosen,
		rpl::producer<QColor> edgeColor) {
	const auto width = st::boxWideWidth;

	const auto buttonPadding = st::craftPreviewPadding;
	const auto buttonSize = st::giftBoxGiftTiny;
	const auto height = 2
		* (buttonPadding.top() + buttonSize + buttonPadding.bottom());

	auto widget = object_ptr<FixedHeightWidget>(parent, height);
	const auto raw = widget.data();

	struct Entry {
		GiftForCraft gift;
		GiftButton *button = nullptr;
		AbstractButton *add = nullptr;
		AbstractButton *percent = nullptr;
		AbstractButton *remove = nullptr;
	};
	struct State {
		explicit State(not_null<Main::Session*> session)
		: delegate(session, GiftButtonMode::CraftPreview) {
		}

		Delegate delegate;
		std::array<Entry, 4> entries;
		Fn<void(int)> refreshButton;
		rpl::event_stream<int> editRequests;
		rpl::event_stream<int> removeRequests;
		rpl::variable<int> chancePermille;
		rpl::variable<QColor> edgeColor;
		RpWidget *forgeRadial = nullptr;
	};
	const auto state = parent->lifetime().make_state<State>(session);
	state->edgeColor = std::move(edgeColor);

	state->refreshButton = [=](int index) {
		Expects(index >= 0 && index < state->entries.size());

		auto &entry = state->entries[index];
		const auto single = state->delegate.buttonSize();
		const auto geometry = QRect(
			((index % 2)
				? (width - buttonPadding.left() - single.width())
				: buttonPadding.left()),
			((index < 2)
				? buttonPadding.top()
				: (height - buttonPadding.top() - single.height())),
			single.width(),
			single.height());
		delete base::take(entry.add);
		delete base::take(entry.button);
		delete base::take(entry.percent);
		delete base::take(entry.remove);

		if (entry.gift) {
			entry.button = CreateChild<GiftButton>(raw, &state->delegate);
			entry.button->setDescriptor(GiftTypeStars{
				.info = {
					.id = entry.gift.unique->initialGiftId,
					.unique = entry.gift.unique,
					.document = entry.gift.unique->model.document,
				},
			}, GiftButton::Mode::CraftPreview);
			entry.button->show();
			if (index > 0) {
				entry.button->setClickedCallback([=] {
					state->editRequests.fire_copy(index);
				});
			} else {
				entry.button->setAttribute(Qt::WA_TransparentForMouseEvents);
			}
			entry.button->setGeometry(
				geometry,
				state->delegate.buttonExtend());

			entry.percent = MakePercentButton(
				raw,
				entry.button,
				entry.gift,
				state->edgeColor.value());
		} else {
			entry.add = CreateChild<AbstractButton>(raw);
			entry.add->show();
			entry.add->paintOn([=](QPainter &p) {
				auto hq = PainterHighQualityEnabler(p);
				const auto radius = st::boxRadius;
				p.setPen(Qt::NoPen);
				p.setBrush(ForgeBgOverlay());

				const auto rect = QRect(QPoint(), geometry.size());
				p.drawRoundedRect(rect, radius, radius);

				const auto &icon = st::craftAddIcon;
				icon.paintInCenter(p, rect, st::white->c);
			});
			entry.add->setClickedCallback([=] {
				state->editRequests.fire_copy(index);
			});
			entry.add->setGeometry(geometry);
		}

		const auto count = 4 - ranges::count(
			state->entries,
			nullptr,
			&Entry::button);
		const auto canRemove = (count > 1);
		for (auto i = 0; i != 4; ++i) {
			auto &entry = state->entries[i];
			if (entry.button) {
				if (!canRemove) {
					delete base::take(entry.remove);
				} else if (!entry.remove) {
					entry.remove = MakeRemoveButton(
						raw,
						entry.button,
						entry.percent->height(),
						entry.gift,
						[=] { state->removeRequests.fire_copy(i); },
						state->edgeColor.value());
				}
			}
		}
	};

	std::move(
		chosen
	) | rpl::on_next([=](const std::vector<GiftForCraft> &gifts) {
		auto chance = 0;
		for (auto i = 0; i != 4; ++i) {
			auto &entry = state->entries[i];
			const auto gift = (i < gifts.size()) ? gifts[i] : GiftForCraft();
			chance += gift.unique ? gift.unique->craftChancePermille : 0;
			if (entry.gift == gift && (entry.button || entry.add)) {
				continue;
			}
			entry.gift = gift;
			state->refreshButton(i);
		}
		state->chancePermille = chance;
	}, raw->lifetime());

	const auto center = [&] {
		const auto buttonPadding = st::craftPreviewPadding;
		const auto buttonSize = st::giftBoxGiftTiny;
		const auto left = buttonPadding.left()
			+ buttonSize
			+ buttonPadding.right();
		const auto center = (width - 2 * left);
		const auto top = (height - center) / 2;
		return QRect(left, top, center, center);
	}();
	raw->paintOn([=](QPainter &p) {
		auto hq = PainterHighQualityEnabler(p);

		const auto radius = st::boxRadius;

		p.setPen(Qt::NoPen);
		p.setBrush(ForgeBgOverlay());

		p.drawRoundedRect(center, radius, radius);

		st::craftForge.paintInCenter(p, center, st::white->c);
	});

	state->forgeRadial = MakeRadialPercent(
		raw,
		st::craftForgePercent,
		state->chancePermille.value());
	state->forgeRadial->setGeometry(center.marginsRemoved({
		st::craftForgePadding,
		st::craftForgePadding,
		st::craftForgePadding,
		st::craftForgePadding,
	}));

	auto grabForAnimation = [=](std::shared_ptr<CraftState> craftState) {
		craftState->forgeRect = center;
		craftState->forgePercent = GrabWidgetToImage(state->forgeRadial);

		auto giftsTotal = 0;
		auto lostIndex = 0;
		for (auto i = 0; i != 4; ++i) {
			auto &entry = state->entries[i];
			auto &corner = craftState->corners[i];

			if (entry.button) {
				corner.originalRect = entry.button->geometry();
				if (entry.percent) {
					corner.percentBadge = GrabWidgetToImage(entry.percent);
				}
				if (entry.remove) {
					corner.removeButton = GrabWidgetToImage(entry.remove);
				}
				if (lostIndex < craftState->lostGifts.size()) {
					craftState->lostGifts[lostIndex].cornerIndex = i;
					++lostIndex;
				}
				corner.giftButton.reset(entry.button);
				entry.button->setParent(parent);
				base::take(entry.button)->hide();

				++giftsTotal;
			} else if (entry.add) {
				corner.addButton = GrabWidgetToImage(entry.add);
				corner.originalRect = entry.add->geometry();
			}
		}

		const auto failedCount = MakeRadialPercent(
			raw,
			st::craftForgePercent,
			rpl::single(0),
			[=](int) { return QString::number(giftsTotal); });
		failedCount->setGeometry(state->forgeRadial->geometry());
		craftState->lostRadial = GrabWidgetToImage(failedCount);
		delete failedCount;

		const auto overlayBg = craftState->forgeBgOverlay = ForgeBgOverlay();
		const auto backdrop = CraftBackdrops()[giftsTotal - 1].backdrop;
		craftState->forgeBg1 = anim::color(
			backdrop.centerColor,
			QColor(overlayBg.red(), overlayBg.green(), overlayBg.blue()),
			overlayBg.alphaF());
		craftState->forgeBg2 = anim::color(
			backdrop.edgeColor,
			QColor(overlayBg.red(), overlayBg.green(), overlayBg.blue()),
			overlayBg.alphaF());
		const auto overlayFail = ForgeFailOverlay();
		craftState->forgeFail = anim::color(
			CraftBackdrops().back().backdrop.centerColor,
			QColor(overlayFail.red(), overlayFail.green(), overlayFail.blue()),
			overlayFail.alphaF());
		for (auto i = 0; i != 6; ++i) {
			craftState->forgeSides[i] = craftState->prepareEmptySide(i);
		}
	};

	return {
		.widget = std::move(widget),
		.editRequests = state->editRequests.events(),
		.removeRequests = state->removeRequests.events(),
		.grabForAnimation = std::move(grabForAnimation),
	};
}

void AddCraftGiftsList(
		not_null<Window::SessionController*> window,
		not_null<VerticalLayout*> container,
		Data::CraftGiftsDescriptor descriptor,
		const std::vector<GiftForCraft> &selected,
		Fn<void(std::shared_ptr<Data::UniqueGift>)> chosen) {
	struct State {
		rpl::event_stream<> updated;
		Data::CraftGiftsDescriptor data;
		rpl::variable<bool> empty = true;
		rpl::lifetime loading;
	};
	const auto state = container->lifetime().make_state<State>();
	state->data = std::move(descriptor);

	using Descriptor = Info::PeerGifts::GiftDescriptor;
	using StarGift = Info::PeerGifts::GiftTypeStars;
	auto handler = crl::guard(container, [=](Descriptor descriptor) {
		Expects(v::is<StarGift>(descriptor));

		const auto unique = v::get<StarGift>(descriptor).info.unique;
		chosen(unique);
	});

	auto gifts = rpl::single(
		rpl::empty
	) | rpl::then(state->updated.events()) | rpl::map([=] {
		auto result = GiftsDescriptor();
		const auto selfId = window->session().userPeerId();
		for (const auto &gift : state->data.list) {
			result.list.push_back(Info::PeerGifts::GiftTypeStars{
				.info = gift.info,
				.resale = true,
				.mine = (gift.info.unique->ownerId == selfId),
				});
		}
		state->empty = result.list.empty();
		return result;
	});
	const auto peer = window->session().user();
	const auto loadMore = [=] {
		if (!state->data.offset.isEmpty() && !state->loading) {
			state->loading = Data::CraftGiftsSlice(
				&peer->session(),
				state->data.giftId,
				state->data.offset
			) | rpl::on_next([=](Data::CraftGiftsDescriptor &&slice) {
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
		.mode = GiftsListMode::Craft,
		.peer = peer,
		.gifts = std::move(gifts),
		.selected = (selected
			| ranges::views::transform(&GiftForCraft::unique)
			| ranges::to_vector),
		.loadMore = loadMore,
		.handler = handler,
	}));

	const auto skip = st::defaultSubsectionTitlePadding.top();
	const auto wrap = container->add(
		object_ptr<SlideWrap<FlatLabel>>(
			container,
			object_ptr<FlatLabel>(
				container,
				tr::lng_gift_craft_select_none(),
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

void ShowSelectGiftBox(
		not_null<Window::SessionController*> controller,
		uint64 giftId,
		Fn<void(GiftForCraft)> chosen,
		std::vector<GiftForCraft> selected,
		Fn<void()> boxClosed) {
	struct Entry {
		Data::SavedStarGift gift;
		GiftButton *button = nullptr;
	};
	struct State {
		std::vector<Entry> entries;

		Data::CraftGiftsDescriptor craft;
		Data::ResaleGiftsDescriptor resale;

		rpl::lifetime craftLifetime;
		rpl::lifetime resaleLifetime;
	};

	const auto session = &controller->session();
	const auto state = std::make_shared<State>();

	const auto make = [=](not_null<GenericBox*> box) {
		box->setTitle(tr::lng_gift_craft_select_title());
		box->setWidth(st::boxWideWidth);

		box->boxClosing() | rpl::on_next(boxClosed, box->lifetime());

		AddSubsectionTitle(
			box->verticalLayout(),
			tr::lng_gift_craft_select_your());

		const auto got = crl::guard(box, [=](
				std::shared_ptr<Data::UniqueGift> gift) {
			if (!ShowCraftLaterError(box->uiShow(), gift)) {
				chosen(GiftForCraft{ .unique = gift });
				box->closeBox();
			}
		});

		AddCraftGiftsList(
			controller,
			box->verticalLayout(),
			state->craft,
			selected,
			got);

		if (const auto count = state->resale.count) {
			AddSubsectionTitle(
				box->verticalLayout(),
				tr::lng_gift_craft_select_market(
					lt_count,
					rpl::single(count * 1.)));

			AddResaleGiftsList(
				controller,
				session->user(),
				box->verticalLayout(),
				state->resale,
				rpl::single(false),
				got,
				true);
		}

		box->addButton(tr::lng_box_ok(), [=] {
			box->closeBox();
		});
		box->setMaxHeight(st::boxWideWidth);
	};
	const auto show = crl::guard(controller, [=] {
		controller->show(Box(make));
	});

	state->craftLifetime = Data::CraftGiftsSlice(
		session,
		giftId
	) | rpl::on_next([=](Data::CraftGiftsDescriptor &&info) {
		state->craftLifetime.destroy();

		state->craft = std::move(info);
		if (state->resale.giftId) {
			show();
		}
	});

	state->resaleLifetime = Data::ResaleGiftsSlice(
		session,
		giftId,
		{ .forCraft = true }
	) | rpl::on_next([=](Data::ResaleGiftsDescriptor &&info) {
		state->resaleLifetime.destroy();

		state->resale = std::move(info);
		if (state->craft.giftId) {
			show();
		}
	});
}

[[nodiscard]] object_ptr<RpWidget> MakeRarityExpectancyPreview(
		not_null<RpWidget*> parent,
		not_null<Main::Session*> session,
		rpl::producer<std::vector<GiftForCraft>> gifts) {
	auto result = object_ptr<RpWidget>(parent);
	const auto raw = result.data();

	struct AttributeState {
		rpl::variable<int> permille;
		bool isBackdrop = false;
		QString name;

		Animations::Simple backdropAnimation;
		Data::UniqueGiftBackdrop wasBackdrop;
		Data::UniqueGiftBackdrop nowBackdrop;
		QImage wasFrame;
		QImage nowFrame;

		Animations::Simple patternAnimation;
		DocumentData *wasPattern = nullptr;
		DocumentData *nowPattern = nullptr;
		std::unique_ptr<Text::CustomEmoji> wasEmoji;
		std::unique_ptr<Text::CustomEmoji> nowEmoji;

		AbstractButton *button = nullptr;
		RpWidget *radial = nullptr;
	};

	struct State {
		std::array<AttributeState, 8> attrs;
		int count = 0;
		ImportantTooltip *tooltip = nullptr;
	};

	const auto state = raw->lifetime().make_state<State>();

	const auto single = st::craftAttributeSize;
	const auto skip = st::craftAttributeSkip;

	for (auto i = 0; i != 8; ++i) {
		auto &attr = state->attrs[i];
		const auto btn = CreateChild<AbstractButton>(raw);
		attr.button = btn;
		btn->resize(single, single);
		btn->hide();

		const auto idx = i;
		btn->paintOn([=](QPainter &p) {
			auto &a = state->attrs[idx];
			const auto sub = st::craftAttributePadding;
			const auto inner = QRect(0, 0, single, single).marginsRemoved(
				{ sub, sub, sub, sub });
			if (a.isBackdrop) {
				const auto progress = a.backdropAnimation.value(1.);
				if (progress < 1.) {
					p.drawImage(inner.topLeft(), a.wasFrame);
					p.setOpacity(progress);
				}
				if (a.nowFrame.isNull()) {
					const auto ratio = style::DevicePixelRatio();
					a.nowFrame = QImage(
						inner.size() * ratio,
						QImage::Format_ARGB32_Premultiplied);
					a.nowFrame.fill(Qt::transparent);
					a.nowFrame.setDevicePixelRatio(ratio);
					auto q = QPainter(&a.nowFrame);
					auto hq = PainterHighQualityEnabler(q);
					auto gradient = QLinearGradient(
						QPointF(inner.width(), 0),
						QPointF(0, inner.height()));
					gradient.setColorAt(0., a.nowBackdrop.centerColor);
					gradient.setColorAt(1., a.nowBackdrop.edgeColor);
					q.setPen(Qt::NoPen);
					q.setBrush(gradient);
					q.drawEllipse(
						0,
						0,
						inner.width(),
						inner.height());

					q.setCompositionMode(
						QPainter::CompositionMode_Source);
					q.setBrush(Qt::transparent);
					const auto max = u"100%"_q;
					const auto &font = st::craftAttributePercent.font;
					const auto tw = font->width(max);
					q.drawRoundedRect(
						(inner.width() - tw) / 2,
						inner.height() + sub - font->height,
						tw,
						font->height,
						font->height / 2.,
						font->height / 2.);
				}
				p.drawImage(inner.topLeft(), a.nowFrame);
				p.setOpacity(1.);
			} else {
				const auto center = QRect(0, 0, single, single).center();
				const auto emojiShift
					= (single - Emoji::GetCustomSizeNormal()) / 2;
				const auto pos = QPoint(emojiShift, emojiShift);
				const auto progress = a.patternAnimation.value(1.);
				if (progress < 1.) {
					p.translate(center);
					p.save();
					p.setOpacity(1. - progress);
					p.scale(1. - progress, 1. - progress);
					p.translate(-center);
					a.wasEmoji->paint(p, {
						.textColor = st::white->c,
						.position = pos,
					});
					p.restore();
					p.scale(progress, progress);
					p.setOpacity(progress);
					p.translate(-center);
				}
				if (!a.nowEmoji) {
					a.nowEmoji = session->data().customEmojiManager().create(
						a.nowPattern,
						[=] { btn->update(); });
				}
				a.nowEmoji->paint(p, {
					.textColor = st::white->c,
					.position = pos,
				});
			}
		});

		attr.radial = MakeRadialPercent(
			btn,
			st::craftAttributePercent,
			attr.permille.value());
		attr.radial->setGeometry(0, 0, single, single);

		btn->setClickedCallback([=] {
			auto &a = state->attrs[idx];
			if (state->tooltip) {
				state->tooltip->toggleAnimated(false);
			}

			auto text = a.isBackdrop
				? tr::lng_gift_craft_chance_backdrop(
					tr::now,
					lt_percent,
					TextWithEntities{ FormatPercent(a.permille.current()) },
					lt_name,
					TextWithEntities{ a.name },
					Ui::Text::RichLangValue)
				: tr::lng_gift_craft_chance_symbol(
					tr::now,
					lt_percent,
					TextWithEntities{ FormatPercent(a.permille.current()) },
					lt_name,
					TextWithEntities{ a.name },
					Ui::Text::RichLangValue);
			const auto tooltip = CreateChild<ImportantTooltip>(
				parent,
				MakeNiceTooltipLabel(
					parent,
					rpl::single(std::move(text)),
					st::boxWideWidth / 2,
					st::defaultImportantTooltipLabel,
					st::defaultPopupMenu),
				st::defaultImportantTooltip);
			tooltip->toggleFast(false);

			base::install_event_filter(tooltip, qApp, [=](not_null<QEvent*> e) {
				if (e->type() == QEvent::MouseButtonPress) {
					tooltip->toggleAnimated(false);
				}
				return base::EventFilterResult::Continue;
			});

			const auto geometry = MapFrom(parent, btn, btn->rect());
			const auto countPosition = [=](QSize size) {
				const auto left = geometry.x()
					+ (geometry.width() - size.width()) / 2;
				const auto right = parent->width()
					- st::normalFont->spacew;
				return QPoint(
					std::max(std::min(left, right - size.width()), 0),
					geometry.y()
						- size.height()
						- st::normalFont->descent);
			};
			tooltip->pointAt(geometry, RectPart::Top, countPosition);
			tooltip->toggleAnimated(true);

			state->tooltip = tooltip;
			tooltip->shownValue() | rpl::filter(
				!rpl::mappers::_1
			) | rpl::on_next([=] {
				crl::on_main(tooltip, [=] {
					if (tooltip->isHidden()) {
						if (state->tooltip == tooltip) {
							state->tooltip = nullptr;
						}
						delete tooltip;
					}
				});
			}, tooltip->lifetime());

			base::timer_once(
				3000
			) | rpl::on_next([=] {
				tooltip->toggleAnimated(false);
			}, tooltip->lifetime());
		});
	}

	const auto relayout = [=](int count) {
		const auto twoRows = (count > 5);
		const auto row1 = twoRows ? ((count + 1) / 2) : count;
		const auto row2 = twoRows ? (count - row1) : 0;
		const auto rowWidth = [&](int n) {
			return n * single + (n - 1) * skip;
		};
		const auto w1 = rowWidth(row1);
		const auto w2 = row2 ? rowWidth(row2) : 0;
		const auto naturalWidth = std::max(w1, w2);
		const auto totalHeight = twoRows
			? (2 * single + st::craftAttributeRowSkip)
			: single;

		for (auto i = 0; i != 8; ++i) {
			auto &attr = state->attrs[i];
			if (i < count) {
				const auto inRow1 = (i < row1);
				const auto rowItems = inRow1 ? row1 : row2;
				const auto rowW = rowWidth(rowItems);
				const auto indexInRow = inRow1 ? i : (i - row1);
				const auto x = (naturalWidth - rowW) / 2
					+ indexInRow * (single + skip);
				const auto y = inRow1
					? 0
					: (single + st::craftAttributeRowSkip);
				attr.button->setGeometry(x, y, single, single);
				attr.button->show();
			} else {
				attr.button->hide();
			}
		}

		raw->setNaturalWidth(naturalWidth);
		raw->resize(naturalWidth, totalHeight);
	};

	const auto permilles = session->appConfig().craftAttributePermilles();
	const auto computePermille = [=](int total, int count) {
		Expects(total > 0);
		Expects(count > 0);

		const auto &list = (total <= permilles.size())
			? permilles[total - 1]
			: std::vector<int>();
		return (count <= list.size()) ? list[count - 1] : 1000;
	};

	std::move(
		gifts
	) | rpl::on_next([=](const std::vector<GiftForCraft> &list) {
		struct BackdropEntry {
			Data::UniqueGiftBackdrop fields;
			QString name;
			int count = 0;
		};
		struct PatternEntry {
			not_null<DocumentData*> document;
			QString name;
			int count = 0;
		};
		auto backdrops = std::vector<BackdropEntry>();
		auto patterns = std::vector<PatternEntry>();
		for (const auto &gift : list) {
			const auto &backdrop = gift.unique->backdrop;
			const auto &pattern = gift.unique->pattern;
			const auto proj1 = &BackdropEntry::fields;
			const auto i = ranges::find(backdrops, backdrop, proj1);
			if (i != end(backdrops)) {
				++i->count;
			} else {
				backdrops.push_back({ backdrop, backdrop.name, 1 });
			}
			const auto proj2 = &PatternEntry::document;
			const auto j = ranges::find(
				patterns,
				pattern.document,
				proj2);
			if (j != end(patterns)) {
				++j->count;
			} else {
				patterns.push_back({ pattern.document, pattern.name, 1 });
			}
		}
		ranges::sort(backdrops, ranges::greater(), &BackdropEntry::count);
		ranges::sort(patterns, ranges::greater(), &PatternEntry::count);

		const auto total = int(list.size());
		auto slotIndex = 0;
		for (const auto &b : backdrops) {
			if (slotIndex >= 8) break;
			auto &a = state->attrs[slotIndex];
			a.isBackdrop = true;
			a.name = b.name;
			a.permille = computePermille(total, b.count);
			if (a.nowBackdrop != b.fields) {
				if (!a.nowFrame.isNull()) {
					a.wasBackdrop = a.nowBackdrop;
					a.wasFrame = base::take(a.nowFrame);
					a.backdropAnimation.stop();
					a.backdropAnimation.start([=, btn = a.button] {
						btn->update();
					}, 0., 1., st::fadeWrapDuration);
				}
				a.nowBackdrop = b.fields;
				a.nowFrame = QImage();
			}
			++slotIndex;
		}
		for (const auto &pt : patterns) {
			if (slotIndex >= 8) break;
			auto &a = state->attrs[slotIndex];
			a.isBackdrop = false;
			a.name = pt.name;
			a.permille = computePermille(total, pt.count);
			if (a.nowPattern != pt.document) {
				if (a.nowEmoji) {
					a.wasPattern = a.nowPattern;
					a.wasEmoji = base::take(a.nowEmoji);
					a.patternAnimation.stop();
					a.patternAnimation.start([=, btn = a.button] {
						btn->update();
					}, 0., 1., st::fadeWrapDuration);
				}
				a.nowPattern = pt.document;
				a.nowEmoji = nullptr;
			}
			++slotIndex;
		}

		const auto newCount = slotIndex;
		if (state->count != newCount) {
			state->count = newCount;
			relayout(newCount);
		}
	}, raw->lifetime());

	return result;
}

void Craft(
		not_null<GenericBox*> box,
		not_null<Window::SessionController*> controller,
		std::shared_ptr<CraftState> state,
		const std::vector<GiftForCraft> &gifts) {
	auto show = controller->uiShow();
	auto startRequest = [=](Fn<void(CraftResult)> done) {
#if 0
		constexpr auto kDelays = std::array<crl::time, 7>{
			100, 200, 300, 400, 500, 1000, 2000
		};
		const auto delay = kDelays[base::RandomIndex(kDelays.size())];
		const auto giftsCopy = gifts;

		base::call_delayed(delay, box, [=] {
			//static auto testing = 0;
			const auto shouldSucceed = false;// (((++testing) / 4) % 2 == 0);
			const auto count = int(giftsCopy.size());
			if (shouldSucceed && count > 0) {
				const auto &chosen = giftsCopy[base::RandomIndex(count)];
				auto info = Data::StarGift{
					.id = chosen.unique->initialGiftId,
					.unique = chosen.unique,
					.document = chosen.unique->model.document,
				};
				auto result = std::make_shared<Data::GiftUpgradeResult>(
					Data::GiftUpgradeResult{
						.info = std::move(info),
						.manageId = chosen.manageId,
					});
				done(std::move(result));
			} else {
				done(nullptr);
			}
		});
#endif

		auto inputs = QVector<MTPInputSavedStarGift>();
		for (const auto &gift : gifts) {
			inputs.push_back(
				Api::InputSavedStarGiftId(gift.manageId, gift.unique));
		}
		const auto weak = base::make_weak(controller);
		const auto session = &controller->session();
		session->api().request(MTPpayments_CraftStarGift(
			MTP_vector<MTPInputSavedStarGift>(inputs)
		)).done([=](const MTPUpdates &result) {
			session->api().applyUpdates(result);
			session->data().nextForUpgradeGiftInvalidate(session->user());
			const auto gift = FindUniqueGift(session, result);
			const auto slug = gift ? gift->info.unique->slug : QString();
			for (const auto &input : gifts) {
				const auto action = (slug == input.unique->slug)
					? Data::GiftUpdate::Action::Upgraded
					: Data::GiftUpdate::Action::Delete;
				controller->session().data().notifyGiftUpdate({
					.id = input.manageId,
					.slug = input.unique->slug,
					.action = action,
				});
			}
			done(gift);
		}).fail([=](const MTP::Error &error) {
			const auto type = error.type();
			const auto waitPrefix = u"STARGIFT_CRAFT_TOO_EARLY_"_q;
			if (type.startsWith(waitPrefix)) {
				done(CraftResultWait{
					.seconds = type.mid(waitPrefix.size()).toInt(),
				});
			} else {
				done(CraftResultError{ type });
			}
		}).send();
	};
	const auto requested = std::make_shared<bool>();
	const auto giftId = gifts.front().unique->initialGiftId;
	auto retryWithNewGift = [=](Fn<void()> closeCurrent) {
		if (*requested) {
			return;
		}
		*requested = true;
		ShowSelectGiftBox(controller, giftId, [=](GiftForCraft chosen) {
			ShowGiftCraftBox(controller, { chosen }, false);
			closeCurrent();
		}, {}, [=] { *requested = false; });
	};
	StartCraftAnimation(
		box,
		std::move(show),
		std::move(state),
		std::move(startRequest),
		std::move(retryWithNewGift));
}

void AddPreviewNewModels(
		not_null<VerticalLayout*> container,
		std::shared_ptr<Main::SessionShow> show,
		const QString &giftName,
		Data::UniqueGiftAttributes attributes,
		rpl::producer<bool> visible) {
	auto exceptional = std::vector<Data::UniqueGiftModel>();
	for (const auto &model : attributes.models) {
		if (Data::UniqueGiftAttributeHasSpecialRarity(model)) {
			exceptional.push_back(model);
		}
	}
	attributes.models = std::move(exceptional);

	auto emoji = TextWithEntities();
	const auto indices = RandomIndicesSubset(
		int(attributes.models.size()),
		std::min(3, int(attributes.models.size())));
	for (const auto index : indices) {
		emoji.append(Data::SingleCustomEmoji(
			attributes.models[index].document));
	}

	auto badge = object_ptr<AbstractButton>(container);
	const auto badgeRaw = badge.data();

	const auto label = CreateChild<FlatLabel>(
		badgeRaw,
		tr::lng_gift_craft_view_all(
			lt_emoji,
			rpl::single(emoji),
			lt_arrow,
			rpl::single(Ui::Text::IconEmoji(&st::textMoreIconEmoji)),
			Text::WithEntities),
		st::uniqueGiftResalePrice,
		st::defaultPopupMenu,
		Core::TextContext({ .session = &show->session() }));
	label->setTextColorOverride(st::white->c);

	label->sizeValue() | rpl::on_next([=](QSize size) {
		const auto padding = st::craftPreviewAllPadding;
		badgeRaw->setNaturalWidth(
			padding.left() + size.width() + padding.right());
		badgeRaw->resize(
			badgeRaw->naturalWidth(),
			padding.top() + size.height() + padding.bottom());
	}, label->lifetime());

	badgeRaw->widthValue() | rpl::on_next([=](int width) {
		const auto padding = st::craftPreviewAllPadding;
		label->move(padding.left(), padding.top());
	}, badgeRaw->lifetime());

	badgeRaw->paintOn([=](QPainter &p) {
		auto hq = PainterHighQualityEnabler(p);
		const auto rect = badgeRaw->rect();
		const auto radius = rect.height() / 2.;
		p.setPen(Qt::NoPen);
		p.setBrush(ForgeBgOverlay());
		p.drawRoundedRect(rect, radius, radius);
	});

	label->setAttribute(Qt::WA_TransparentForMouseEvents);
	badgeRaw->setClickedCallback([=] {
		auto previewAttrs = attributes;
		previewAttrs.models.erase(
			ranges::remove_if(previewAttrs.models, [](const auto &m) {
				return !Data::UniqueGiftAttributeHasSpecialRarity(m);
			}),
			end(previewAttrs.models));
		show->show(Box(
			StarGiftPreviewBox,
			giftName,
			previewAttrs,
			Data::GiftAttributeIdType::Model,
			nullptr));
	});

	const auto wrap = container->add(
		object_ptr<SlideWrap<AbstractButton>>(
			container,
			std::move(badge),
			st::craftPreviewAllMargin),
		style::al_top);
	std::move(visible) | rpl::on_next([=](bool shown) {
		wrap->toggle(shown, anim::type::instant);
	}, wrap->lifetime());
}

void MakeCraftContent(
		not_null<GenericBox*> box,
		not_null<Window::SessionController*> controller,
		std::vector<GiftForCraft> gifts,
		bool autoStartCraft) {
	Expects(!gifts.empty());

	struct State {
		std::shared_ptr<CraftState> craftState;
		GradientButton *button = nullptr;

		FlatLabel *title = nullptr;
		FlatLabel *about = nullptr;
		RpWidget *attributes = nullptr;
		RpWidget *craftingView = nullptr;
		Fn<void(std::shared_ptr<CraftState>)> grabCraftingView;

		rpl::variable<std::vector<GiftForCraft>> chosen;
		rpl::variable<QString> name;
		rpl::variable<int> successPercentPermille;
		rpl::variable<QString> successPercentText;

		int requestingIndex = -1;
		bool crafting = false;
	};
	const auto session = &controller->session();
	const auto giftId = gifts.front().unique->initialGiftId;
	const auto state = box->lifetime().make_state<State>();

	const auto auctions = &controller->session().giftAuctions();
	auto attributes = auctions->attributes(giftId).value_or(
		Data::UniqueGiftAttributes());

	state->craftState = std::make_shared<CraftState>();
	state->craftState->session = session;
	state->craftState->coversAnimate = true;

	{
		auto backdrops = CraftBackdrops();
		const auto emoji = Text::IconEmoji(&st::craftPattern);
		const auto data = emoji.entities.front().data();
		for (auto i = 0; i != backdrops.size(); ++i) {
			auto &cover = state->craftState->covers[i];
			cover.backdrop.colors = backdrops[i].backdrop;
			cover.pattern.emoji = Text::TryMakeSimpleEmoji(data);
			cover.button1 = backdrops[i].button1;
			cover.button2 = backdrops[i].button2;
		}
	}

	const auto giftName = gifts.front().unique->title;
	state->chosen = std::move(gifts);
	for (auto i = 0; i != int(state->chosen.current().size()); ++i) {
		state->craftState->covers[i].shown = true;
	}

	state->name = state->chosen.value(
	) | rpl::map([=](const std::vector<GiftForCraft> &gifts) {
		return Data::UniqueGiftName(*gifts.front().unique);
	});
	state->successPercentPermille = state->chosen.value(
	) | rpl::map([=](const std::vector<GiftForCraft> &gifts) {
		auto result = 0;
		for (const auto &entry : gifts) {
			if (const auto gift = entry.unique.get()) {
				result += gift->craftChancePermille;
			}
		}
		return result;
	});
	state->successPercentText = state->successPercentPermille.value(
	) | rpl::map(FormatPercent);

	const auto raw = box->verticalLayout();

	state->chosen.value(
	) | rpl::on_next([=](const std::vector<GiftForCraft> &gifts) {
		state->craftState->updateForGiftCount(int(gifts.size()), [=] {
			raw->update();
		});
	}, box->lifetime());

	const auto title = state->title = raw->add(
		object_ptr<Ui::FlatLabel>(
			box,
			tr::lng_gift_craft_title(),
			st::uniqueGiftTitle),
		st::craftTitleMargin,
		style::al_top);
	title->setTextColorOverride(QColor(255, 255, 255));

	auto crafting = MakeCraftingView(
		raw,
		session,
		state->chosen.value(),
		state->craftState->edgeColor.value());
	const auto craftingHeight = crafting.widget->height();
	state->craftingView = crafting.widget.data();
	state->grabCraftingView = std::move(crafting.grabForAnimation);
	raw->add(std::move(crafting.widget));
	std::move(crafting.removeRequests) | rpl::on_next([=](int index) {
		auto chosen = state->chosen.current();
		if (index < chosen.size()) {
			chosen.erase(begin(chosen) + index);
			state->chosen = std::move(chosen);
		}
	}, raw->lifetime());

	std::move(
		crafting.editRequests
	) | rpl::on_next([=](int index) {
		const auto guard = base::make_weak(raw);
		if (state->requestingIndex >= 0) {
			state->requestingIndex = index;
			return;
		}
		state->requestingIndex = index;
		const auto callback = [=](GiftForCraft chosen) {
			auto copy = state->chosen.current();
			if (state->requestingIndex < copy.size()) {
				copy[state->requestingIndex] = chosen;
			} else {
				copy.push_back(chosen);
			}
			state->chosen = std::move(copy);
		};
		ShowSelectGiftBox(
			controller,
			giftId,
			crl::guard(raw, callback),
			state->chosen.current(),
			crl::guard(raw, [=] { state->requestingIndex = -1; }));
	}, raw->lifetime());

	auto fullName = state->chosen.value(
	) | rpl::map([=](const std::vector<GiftForCraft> &list) {
		const auto unique = list.front().unique.get();
		return Data::SingleCustomEmoji(
			unique->model.document
		).append(' ').append(tr::bold(Data::UniqueGiftName(*unique)));
	});
	auto aboutText = rpl::combine(
		tr::lng_gift_craft_about1(lt_gift, fullName, tr::rich),
		tr::lng_gift_craft_about2(tr::rich)
	) | rpl::map([=](TextWithEntities &&a, TextWithEntities &&b) {
		return a.append('\n').append('\n').append(b);
	});
	const auto about = state->about = raw->add(
		object_ptr<FlatLabel>(
			raw,
			std::move(aboutText),
			st::craftAbout,
			st::defaultPopupMenu,
			Core::TextContext({ .session = session })),
		st::craftAboutMargin,
		style::al_top);
	about->setTextColorOverride(st::white->c);
	about->setTryMakeSimilarLines(true);

	state->attributes = raw->add(
		MakeRarityExpectancyPreview(raw, session, state->chosen.value()),
		st::craftAttributesMargin,
		style::al_top);

	auto viewAllVisible = state->chosen.value(
	) | rpl::map([](const std::vector<GiftForCraft> &list) {
		auto backdropIds = base::flat_set<int>();
		auto patternPtrs = base::flat_set<DocumentData*>();
		for (const auto &gift : list) {
			backdropIds.emplace(gift.unique->backdrop.id);
			patternPtrs.emplace(gift.unique->pattern.document.get());
		}
		return int(backdropIds.size() + patternPtrs.size()) <= 5;
	});

	AddPreviewNewModels(
		raw,
		controller->uiShow(),
		giftName,
		std::move(attributes),
		std::move(viewAllVisible));

	raw->paintOn([=](QPainter &p) {
		const auto &cs = state->craftState;
		if (cs->craftingStarted) {
			return;
		}
		const auto wasButton1 = cs->button1;
		const auto wasButton2 = cs->button2;
		cs->paint(p, raw->size(), craftingHeight);
		if (cs->button1 != wasButton1 || cs->button2 != wasButton2) {
			state->button->setStops({
				{ 0., cs->button1 },
				{ 1., cs->button2 },
			});
		}
	});

	const auto button = state->button = raw->add(
		object_ptr<GradientButton>(raw, QGradientStops()),
		st::craftBoxButtonPadding);
	button->setFullRadius(true);
	button->startGlareAnimation();

	const auto startCrafting = [=] {
		if (state->crafting) {
			return;
		}
		state->crafting = true;

		const auto &cs = state->craftState;
		const auto &gifts = state->chosen.current();
		cs->giftName = state->name.current();
		cs->successPermille = state->successPercentPermille.current();

		for (const auto &gift : gifts) {
			if (gift.unique) {
				cs->lostGifts.push_back({
					.number = u"#"_q + Lang::FormatCountDecimal(gift.unique->number),
				});
			}
		}

		if (state->grabCraftingView) {
			state->grabCraftingView(cs);
		}

		const auto craftingPos = state->craftingView->pos();
		cs->craftingTop = craftingPos.y();
		cs->craftingBottom = craftingPos.y() + state->craftingView->height();

		for (auto &corner : cs->corners) {
			corner.originalRect.translate(craftingPos);
		}
		cs->forgeRect.translate(craftingPos);
		cs->craftingAreaCenterY = cs->forgeRect.center().y();

		const auto aboutPos = state->about->pos();
		const auto buttonY = state->button->pos().y();
		const auto buttonBottom = buttonY + state->button->height();
		cs->originalButtonY = buttonY;
		const auto bottomRect = QRect(
			0,
			aboutPos.y(),
			raw->width(),
			buttonBottom - aboutPos.y());

		const auto ratio = style::DevicePixelRatio();
		auto bottomPart = QImage(
			bottomRect.size() * ratio,
			QImage::Format_ARGB32_Premultiplied);
		bottomPart.fill(Qt::transparent);
		bottomPart.setDevicePixelRatio(ratio);

		const auto renderWidget = [&](QWidget *widget) {
			const auto pos = widget->pos() - bottomRect.topLeft();
			widget->render(&bottomPart, pos, {}, QWidget::DrawChildren);
		};
		renderWidget(state->about);
		renderWidget(state->attributes);
		renderWidget(state->button);

		cs->bottomPart = std::move(bottomPart);
		cs->bottomPartY = aboutPos.y();
		cs->containerHeight = raw->height();

		Craft(
			box,
			controller,
			state->craftState,
			state->chosen.current());
	};
	button->setClickedCallback(startCrafting);

	SetButtonTwoLabels(
		button,
		st::giftBox.button.textTop,
		tr::lng_gift_craft_button(
			lt_gift,
			state->name.value() | rpl::map(tr::marked),
			tr::marked),
		tr::lng_gift_craft_button_chance(
			lt_percent,
			state->successPercentText.value() | rpl::map(tr::marked),
			tr::marked),
		st::resaleButtonTitle,
		st::resaleButtonSubtitle);

	raw->widthValue() | rpl::on_next([=](int width) {
		const auto padding = st::craftBoxButtonPadding;
		button->setNaturalWidth(width - padding.left() - padding.right());
		button->resize(button->naturalWidth(), st::giftBox.button.height);
	}, raw->lifetime());

	if (autoStartCraft) {
//		base::call_delayed(crl::time(1000), raw, startCrafting);
	}
}

void ShowGiftCraftBox(
		not_null<Window::SessionController*> controller,
		std::vector<GiftForCraft> gifts,
		bool autoStartCraft) {
	controller->show(Box([=](not_null<GenericBox*> box) {
		box->setStyle(st::giftCraftBox);
		box->setWidth(st::boxWideWidth);
		box->setNoContentMargin(true);
		MakeCraftContent(
			box,
			controller,
			gifts,
			autoStartCraft);
		AddUniqueCloseButton(box);

#if _DEBUG
		if (autoStartCraft) {
			static const auto full = gifts;
			box->boxClosing() | rpl::on_next([=] {
				base::call_delayed(1000, controller, [=] {
					auto copy = gifts;
					if (copy.size() > 1) {
						copy.pop_back();
					} else {
						copy = full;
					}
					ShowGiftCraftBox(controller, copy, true);
				});
			}, box->lifetime());
		}
#endif
	}));
}

} // namespace

void ShowGiftCraftInfoBox(
		not_null<Window::SessionController*> controller,
		std::shared_ptr<Data::UniqueGift> gift,
		Data::SavedStarGiftId savedId) {
	controller->show(Box([=](not_null<GenericBox*> box) {
		const auto container = box->verticalLayout();
		auto cover = tr::lng_gift_craft_info_title(
		) | rpl::map([=](const QString &title) {
			auto result = UniqueGiftCover{ *gift };
			result.values.title = title;
			return result;
		});
		AddUniqueGiftCover(container, std::move(cover), {
			.subtitle = tr::lng_gift_craft_info_about(tr::marked),
		});
		AddSkip(container);

		AddUniqueCloseButton(box);

		const auto features = std::vector<FeatureListEntry>{
			{
				st::menuIconUnique,
				tr::lng_gift_craft_rare_title(tr::now),
				tr::lng_gift_craft_rare_about(tr::now, tr::rich),
			},
			{
				st::menuIconCraftTraits,
				tr::lng_gift_craft_chance_title(tr::now),
				tr::lng_gift_craft_chance_about(tr::now, tr::marked),
			},
			{
				st::menuIconCraftChance,
				tr::lng_gift_craft_affect_title(tr::now),
				tr::lng_gift_craft_affect_about(tr::now, tr::marked),
			},
		};
		for (const auto &feature : features) {
			container->add(
				MakeFeatureListEntry(container, feature),
				st::boxRowPadding);
		}

		box->setStyle(st::giftBox);
		box->setWidth(st::boxWideWidth);
		box->setNoContentMargin(true);

		box->addButton(tr::lng_gift_craft_start_button(), [=] {
			const auto auctions = &controller->session().giftAuctions();
			const auto show = crl::guard(box, [=] {
				ShowGiftCraftBox(
					controller,
					{ GiftForCraft{ gift, savedId } },
					false);
				box->closeBox();
			});
			if (auctions->attributes(gift->initialGiftId)) {
				show();
			} else {
				auctions->requestAttributes(gift->initialGiftId, show);
			}
		});
	}));
}

void ShowTestGiftCraftBox(
		not_null<Window::SessionController*> controller,
		std::vector<GiftForCraftEntry> gifts) {
	auto converted = std::vector<GiftForCraft>();
	converted.reserve(gifts.size());
	for (auto &gift : gifts) {
		auto entry = GiftForCraft();
		entry.unique = std::move(gift.unique);
		entry.manageId = std::move(gift.manageId);
		converted.push_back(std::move(entry));
	}
	ShowGiftCraftBox(controller, std::move(converted), true);
}

bool ShowCraftLaterError(
		std::shared_ptr<Show> show,
		std::shared_ptr<Data::UniqueGift> gift) {
	const auto now = base::unixtime::now();
	if (gift->canCraftAt <= now) {
		return false;
	}
	ShowCraftLaterError(show, gift->canCraftAt);
	return true;
}

void ShowCraftLaterError(
		std::shared_ptr<Show> show,
		TimeId when) {
	const auto data = base::unixtime::parse(when);
	const auto time = QLocale().toString(data.time(), QLocale::ShortFormat);
	const auto date = langDayOfMonthShort(data.date());

	show->show(MakeInformBox({
		.text = tr::lng_gift_craft_when(
			lt_date,
			rpl::single(tr::bold(date)),
			lt_time,
			rpl::single(tr::bold(time)),
			tr::marked),
		.title = tr::lng_gift_craft_unavailable(),
	}));
}

} // namespace Ui
