/*
This file is part of Telegram Desktop,
the official desktop application for the Telegram messaging service.

For license and copyright information please follow this link:
https://github.com/telegramdesktop/tdesktop/blob/master/LEGAL
*/
#include "ui/widgets/chat_filters_tabs_strip.h"

#include "data/data_chat_filters.h"
#include "data/data_session.h"
#include "lang/lang_keys.h"
#include "main/main_session.h"
#include "ui/widgets/discrete_sliders.h"
#include "ui/widgets/scroll_area.h"
#include "ui/wrap/slide_wrap.h"
#include "styles/style_dialogs.h" // dialogsSearchTabs

#include <QScrollBar>

namespace Ui {

not_null<Ui::RpWidget*> AddChatFiltersTabsStrip(
		not_null<Ui::RpWidget*> parent,
		not_null<Main::Session*> session,
		rpl::producer<int> multiSelectHeightValue,
		Fn<void(int)> setAddedTopScrollSkip,
		Fn<void(FilterId)> choose) {
	class Slider final : public Ui::SettingsSlider {
	public:
		using Ui::SettingsSlider::SettingsSlider;

		[[nodiscard]] int centerOfSection(int section) const {
			const auto widths = Ui::SettingsSlider::countSectionsWidths(0);
			auto result = 0;
			if (section >= 0 && section < widths.size()) {
				for (auto i = 0; i < section; i++) {
					result += widths[i];
				}
				result += widths[section] / 2;
			}
			return result;
		}

		void fitWidthToSections() {
			const auto widths = Ui::SettingsSlider::countSectionsWidths(0);
			resizeToWidth(ranges::accumulate(widths, .0));
		}
	};

	struct State final {
		Ui::Animations::Simple animation;
		std::optional<FilterId> lastFilterId = std::nullopt;
	};

	const auto &scrollSt = st::defaultScrollArea;
	const auto wrap = Ui::CreateChild<Ui::SlideWrap<Ui::RpWidget>>(
		parent,
		object_ptr<Ui::RpWidget>(parent));
	const auto container = wrap->entity();
	const auto scroll = Ui::CreateChild<Ui::ScrollArea>(container, scrollSt);
	const auto sliderPadding = st::dialogsSearchTabsPadding;
	const auto slider = scroll->setOwnedWidget(
		object_ptr<Ui::PaddingWrap<Slider>>(
			parent,
			object_ptr<Slider>(parent, st::dialogsSearchTabs),
			QMargins(sliderPadding, 0, sliderPadding, 0)))->entity();
	const auto state = wrap->lifetime().make_state<State>();
	wrap->toggle(false, anim::type::instant);
	container->sizeValue() | rpl::start_with_next([=](const QSize &s) {
		scroll->resize(s + QSize(0, scrollSt.deltax * 4));
	}, scroll->lifetime());
	rpl::combine(
		parent->widthValue(),
		slider->heightValue()
	) | rpl::start_with_next([=](int w, int h) {
		container->resize(w, h);
	}, wrap->lifetime());
	scroll->setCustomWheelProcess([=](not_null<QWheelEvent*> e) {
		const auto pixelDelta = e->pixelDelta();
		const auto angleDelta = e->angleDelta();
		if (std::abs(pixelDelta.x()) + std::abs(angleDelta.x())) {
			return false;
		}
		const auto bar = scroll->horizontalScrollBar();
		const auto y = pixelDelta.y() ? pixelDelta.y() : angleDelta.y();
		bar->setValue(bar->value() - y);
		return true;
	});

	const auto scrollToIndex = [=](int index, anim::type type) {
		const auto to = index
			? (slider->centerOfSection(index) - scroll->width() / 2)
			: 0;
		const auto bar = scroll->horizontalScrollBar();
		state->animation.stop();
		if (type == anim::type::instant) {
			bar->setValue(to);
		} else {
			state->animation.start(
				[=](float64 v) { bar->setValue(v); },
				bar->value(),
				std::min(to, bar->maximum()),
				st::defaultTabsSlider.duration);
		}
	};

	const auto applyFilter = [=](const Data::ChatFilter &filter) {
		choose(filter.id());
	};

	const auto filterByIndex = [=](int index) -> const Data::ChatFilter& {
		const auto &list = session->data().chatsFilters().list();
		Assert(index >= 0 && index < list.size());
		return list[index];
	};

	const auto rebuild = [=] {
		const auto &list = session->data().chatsFilters().list();
		auto sections = ranges::views::all(
			list
		) | ranges::views::transform([](const Data::ChatFilter &filter) {
			return filter.title().isEmpty()
				? tr::lng_filters_all(tr::now)
				: filter.title();
		}) | ranges::to_vector;
		slider->setSections(std::move(sections));
		slider->fitWidthToSections();
		[&] {
			const auto lookingId = state->lastFilterId.value_or(list[0].id());
			for (auto i = 0; i < list.size(); i++) {
				const auto &filter = list[i];
				if (filter.id() == lookingId) {
					const auto wasLast = !!state->lastFilterId;
					state->lastFilterId = filter.id();
					slider->setActiveSectionFast(i);
					scrollToIndex(
						i,
						wasLast ? anim::type::normal : anim::type::instant);
					applyFilter(filter);
					return;
				}
			}
			if (list.size()) {
				const auto index = 0;
				const auto &filter = filterByIndex(index);
				state->lastFilterId = filter.id();
				slider->setActiveSectionFast(index);
				scrollToIndex(index, anim::type::instant);
				applyFilter(filter);
			}
		}();
		slider->sectionActivated() | rpl::start_with_next([=](int index) {
			const auto &filter = filterByIndex(index);
			state->lastFilterId = filter.id();
			scrollToIndex(index, anim::type::normal);
			applyFilter(filter);
		}, wrap->lifetime());
		wrap->toggle((list.size() > 1), anim::type::instant);
	};
	session->data().chatsFilters().changed(
	) | rpl::start_with_next(rebuild, wrap->lifetime());
	rebuild();

	session->data().chatsFilters().isChatlistChanged(
	) | rpl::start_with_next([=](FilterId id) {
		if (!id || !state->lastFilterId || (id != state->lastFilterId)) {
			return;
		}
		for (const auto &filter : session->data().chatsFilters().list()) {
			if (filter.id() == id) {
				applyFilter(filter);
				return;
			}
		}
	}, wrap->lifetime());

	{
		std::move(
			multiSelectHeightValue
		) | rpl::start_with_next([=](int height) {
			wrap->moveToLeft(0, height);
		}, wrap->lifetime());
		wrap->heightValue() | rpl::start_with_next([=](int height) {
			setAddedTopScrollSkip(height);
		}, wrap->lifetime());
		parent->widthValue() | rpl::start_with_next([=](int w) {
			wrap->resizeToWidth(w);
		}, wrap->lifetime());
	}

	return wrap;
}

} // namespace Ui
