/*
This file is part of Telegram Desktop,
the official desktop application for the Telegram messaging service.

For license and copyright information please follow this link:
https://github.com/telegramdesktop/tdesktop/blob/master/LEGAL
*/
#include "info/boosts/giveaway/select_countries_box.h"

#include "countries/countries_instance.h"
#include "lang/lang_keys.h"
#include "ui/emoji_config.h"
#include "ui/layers/generic_box.h"
#include "ui/painter.h"
#include "ui/rect.h"
#include "ui/widgets/buttons.h"
#include "ui/widgets/checkbox.h"
#include "ui/widgets/labels.h"
#include "ui/widgets/multi_select.h"
#include "ui/wrap/slide_wrap.h"
#include "styles/style_boxes.h"
#include "styles/style_giveaway.h"
#include "styles/style_settings.h"

namespace Ui {
namespace {

void AddSkip(not_null<Ui::VerticalLayout*> container) {
	container->add(object_ptr<Ui::FixedHeightWidget>(
		container,
		st::settingsSectionSkip));
}

[[nodiscard]] QImage CacheFlagEmoji(const QString &flag) {
	const auto &st = st::giveawayGiftCodeCountrySelect.item;
	auto roundPaintCache = QImage(
		Size(st.height) * style::DevicePixelRatio(),
		QImage::Format_ARGB32_Premultiplied);
	roundPaintCache.setDevicePixelRatio(style::DevicePixelRatio());
	roundPaintCache.fill(Qt::transparent);
	{
		const auto size = st.height;
		auto p = Painter(&roundPaintCache);
		auto hq = PainterHighQualityEnabler(p);
		const auto flagText = Ui::Text::String(st::defaultTextStyle, flag);
		p.setPen(st.textBg);
		p.setBrush(st.textBg);
		p.drawEllipse(0, 0, size, size);
		flagText.draw(p, {
			.position = QPoint(
				0 + (size - flagText.maxWidth()) / 2,
				0 + (size - flagText.minHeight()) / 2),
			.outerWidth = size,
			.availableWidth = size,
		});
	}
	return roundPaintCache;
}

} // namespace

void SelectCountriesBox(
		not_null<Ui::GenericBox*> box,
		const std::vector<QString> &selected,
		Fn<void(std::vector<QString>)> doneCallback,
		Fn<bool(int)> checkErrorCallback) {
	struct State final {
		std::vector<QString> resultList;
	};
	const auto state = box->lifetime().make_state<State>();

	const auto multiSelect = box->setPinnedToTopContent(
		object_ptr<Ui::MultiSelect>(
			box,
			st::giveawayGiftCodeCountrySelect,
			tr::lng_participant_filter()));
	AddSkip(box->verticalLayout());
	const auto &buttonSt = st::giveawayGiftCodeCountryButton;

	struct Entry final {
		Ui::SlideWrap<Ui::SettingsButton> *wrap = nullptr;
		QStringList list;
		QString iso2;
	};

	auto countries = Countries::Instance().list();
	ranges::sort(countries, [](
			const Countries::Info &a,
			const Countries::Info &b) {
		return (a.name.compare(b.name, Qt::CaseInsensitive) < 0);
	});
	auto buttons = std::vector<Entry>();
	buttons.reserve(countries.size());
	for (const auto &country : countries) {
		const auto flag = Countries::Instance().flagEmojiByISO2(country.iso2);
		if (!Ui::Emoji::Find(flag)) {
			continue;
		}
		const auto itemId = buttons.size();
		auto button = object_ptr<SettingsButton>(
			box->verticalLayout(),
			rpl::single(flag + ' ' + country.name),
			buttonSt);
		const auto radio = Ui::CreateChild<Ui::RpWidget>(button.data());
		const auto radioView = std::make_shared<Ui::RadioView>(
			st::defaultRadio,
			false,
			[=] { radio->update(); });

		{
			const auto radioSize = radioView->getSize();
			radio->resize(radioSize);
			radio->paintRequest(
			) | rpl::start_with_next([=](const QRect &r) {
				auto p = QPainter(radio);
				radioView->paint(p, 0, 0, radioSize.width());
			}, radio->lifetime());
			const auto buttonHeight = buttonSt.height
				+ rect::m::sum::v(buttonSt.padding);
			radio->moveToLeft(
				st::giveawayRadioPosition.x(),
				(buttonHeight - radioSize.height()) / 2);
		}

		const auto roundPaintCache = CacheFlagEmoji(flag);
		const auto paintCallback = [=](Painter &p, int x, int y, int, int) {
			p.drawImage(x, y, roundPaintCache);
		};
		const auto choose = [=](bool clicked) {
			const auto value = !radioView->checked();
			if (value && checkErrorCallback(state->resultList.size())) {
				return;
			}
			radioView->setChecked(value, anim::type::normal);

			if (value) {
				state->resultList.push_back(country.iso2);
				multiSelect->addItem(
					itemId,
					country.name,
					st::activeButtonBg,
					paintCallback,
					clicked
						? Ui::MultiSelect::AddItemWay::Default
						: Ui::MultiSelect::AddItemWay::SkipAnimation);
			} else {
				auto &list = state->resultList;
				list.erase(ranges::remove(list, country.iso2), end(list));
				multiSelect->removeItem(itemId);
			}
		};
		button->setClickedCallback([=] {
			choose(true);
		});
		if (ranges::contains(selected, country.iso2)) {
			choose(false);
		}

		const auto wrap = box->verticalLayout()->add(
			object_ptr<Ui::SlideWrap<Ui::SettingsButton>>(
				box,
				std::move(button)));
		wrap->toggle(true, anim::type::instant);

		{
			auto list = QStringList{
				flag,
				country.name,
				country.alternativeName,
			};
			buttons.push_back({ wrap, std::move(list), country.iso2 });
		}
	}

	const auto noResults = box->addRow(
		object_ptr<Ui::SlideWrap<Ui::VerticalLayout>>(
			box,
			object_ptr<Ui::VerticalLayout>(box)));
	{
		noResults->toggle(false, anim::type::instant);
		const auto container = noResults->entity();
		AddSkip(container);
		AddSkip(container);
		container->add(
			object_ptr<Ui::CenterWrap<Ui::FlatLabel>>(
				container,
				object_ptr<Ui::FlatLabel>(
					container,
					tr::lng_search_messages_none(),
					st::membersAbout)));
		AddSkip(container);
		AddSkip(container);
	}

	multiSelect->setQueryChangedCallback([=](const QString &query) {
		auto wasAnyFound = false;
		for (const auto &entry : buttons) {
			const auto found = ranges::any_of(entry.list, [&](
					const QString &s) {
				return s.startsWith(query, Qt::CaseInsensitive);
			});
			entry.wrap->toggle(found, anim::type::instant);
			wasAnyFound |= found;
		}
		noResults->toggle(!wasAnyFound, anim::type::instant);
	});
	multiSelect->setItemRemovedCallback([=](uint64 itemId) {
		auto &list = state->resultList;
		auto &button = buttons[itemId];
		const auto it = ranges::find(list, button.iso2);
		if (it != end(list)) {
			list.erase(it);
			button.wrap->entity()->clicked({}, Qt::LeftButton);
		}
	});

	box->addButton(tr::lng_settings_save(), [=] {
		doneCallback(state->resultList);
		box->closeBox();
	});
	box->addButton(tr::lng_cancel(), [=] {
		box->closeBox();
	});
}

} // namespace Ui
