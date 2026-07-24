/*
This file is part of Telegram Desktop,
the official desktop application for the Telegram messaging service.

For license and copyright information please follow this link:
https://github.com/telegramdesktop/tdesktop/blob/master/LEGAL
*/
#include "settings/settings_search.h"

#include "base/event_filter.h"
#include "core/application.h"
#include "core/click_handler_types.h"
#include "core/deep_links/deep_links_settings.h"
#include "lang/lang_keys.h"
#include "main/main_session.h"
#include "settings/settings_builder.h"
#include "settings/settings_common.h"
#include "settings/settings_faq_suggestions.h"
#include "settings/settings_recent_searches.h"
#include "ui/painter.h"
#include "ui/text/text_entity.h"
#include "ui/toast/toast.h"
#include "ui/search_field_controller.h"
#include "ui/vertical_list.h"
#include "ui/widgets/buttons.h"
#include "ui/widgets/popup_menu.h"
#include "ui/widgets/checkbox.h"
#include "ui/widgets/elastic_scroll.h"
#include "ui/widgets/fields/input_field.h"
#include "ui/widgets/labels.h"
#include "ui/widgets/scroll_area.h"
#include "ui/wrap/padding_wrap.h"
#include "ui/wrap/vertical_layout.h"
#include "window/window_session_controller.h"
#include "styles/style_chat_helpers.h"
#include "styles/style_info.h"
#include "styles/style_layers.h"
#include "styles/style_menu_icons.h"
#include "styles/style_settings.h"
#include "styles/style_widgets.h"

namespace Settings {
namespace {

struct SearchResultItem {
	int index = 0;
	int matchCount = 0;
};

[[nodiscard]] QStringList PrepareEntryWords(const Builder::SearchEntry &entry) {
	auto combined = entry.title;
	for (const auto &keyword : entry.keywords) {
		combined += ' ' + keyword;
	}
	return TextUtilities::PrepareSearchWords(combined);
}

[[nodiscard]] int CalculateDepth(
		Type sectionId,
		const Builder::SearchRegistry &registry) {
	const auto path = registry.sectionPath(sectionId);
	if (path.isEmpty()) {
		return 0;
	}
	return path.count(u" > "_q) + 1;
}

void SetupCheckIcon(
		not_null<Ui::SettingsButton*> button,
		Builder::SearchEntryCheckIcon checkIcon,
		const style::SettingsButton &st) {
	struct CheckWidget {
		CheckWidget(QWidget *parent, bool checked)
		: widget(parent)
		, view(st::defaultCheck, checked) {
			view.finishAnimating();
		}
		Ui::RpWidget widget;
		Ui::CheckView view;
	};
	const auto checked = (checkIcon == Builder::SearchEntryCheckIcon::Checked);
	const auto check = button->lifetime().make_state<CheckWidget>(
		button,
		checked);
	check->widget.setAttribute(Qt::WA_TransparentForMouseEvents);
	check->widget.resize(check->view.getSize());
	check->widget.show();

	button->sizeValue(
	) | rpl::on_next([=, left = st.iconLeft](QSize size) {
		check->widget.moveToLeft(
			left,
			(size.height() - check->widget.height()) / 2,
			size.width());
	}, check->widget.lifetime());

	check->widget.paintRequest(
	) | rpl::on_next([=](QRect clip) {
		auto p = QPainter(&check->widget);
		check->view.paint(p, 0, 0, check->widget.width());
		p.setOpacity(0.5);
		p.fillRect(clip, st::boxBg);
	}, check->widget.lifetime());
}

[[nodiscard]] not_null<Ui::SettingsButton*> CreateSearchResultButtonRaw(
		not_null<QWidget*> parent,
		const QString &title,
		const QString &subtitle,
		const style::SettingsButton &st,
		IconDescriptor &&icon,
		Builder::SearchEntryCheckIcon checkIcon) {
	auto buttonObj = CreateButtonWithIcon(
		parent,
		rpl::single(title),
		st,
		std::move(icon));
	const auto button = buttonObj.release();
	button->setPointerCursor(false);
	if (checkIcon != Builder::SearchEntryCheckIcon::None) {
		SetupCheckIcon(button, checkIcon, st);
	}
	const auto details = Ui::CreateChild<Ui::FlatLabel>(
		button,
		subtitle,
		st::settingsSearchResultDetails);
	details->show();
	details->moveToLeft(
		st.padding.left(),
		st.padding.top() + st.height - details->height());
	details->setAttribute(Qt::WA_TransparentForMouseEvents);
	return button;
}

} // namespace

Search::Search(
	QWidget *parent,
	not_null<Window::SessionController*> controller)
: Section(parent, controller) {
	setupContent();
}

rpl::producer<QString> Search::title() {
	return tr::lng_dlg_filter();
}

void Search::setInnerFocus() {
	if (_searchField) {
		_searchField->setFocus();
	}
}

base::weak_qptr<Ui::RpWidget> Search::createPinnedToTop(
		not_null<QWidget*> parent) {
	_searchController = std::make_unique<Ui::SearchFieldController>("");
	auto rowView = _searchController->createRowView(
		parent,
		st::infoLayerMediaSearch);
	_searchField = rowView.field;
	_searchField->customUpDown(true);

	const auto searchContainer = Ui::CreateChild<Ui::FixedHeightWidget>(
		parent.get(),
		st::infoLayerMediaSearch.height);
	const auto wrap = rowView.wrap.release();
	wrap->setParent(searchContainer);
	wrap->show();

	searchContainer->widthValue(
	) | rpl::on_next([=](int width) {
		wrap->resizeToWidth(width);
		wrap->moveToLeft(0, 0);
	}, searchContainer->lifetime());

	_searchController->queryChanges() | rpl::on_next([=](QString &&query) {
		rebuildResults(std::move(query));
	}, searchContainer->lifetime());

	_searchField->submits(
	) | rpl::on_next([=](Qt::KeyboardModifiers) {
		const auto index = (_selected >= 0) ? _selected : 0;
		if (index < int(_visibleButtons.size())) {
			_visibleButtons[index]->clicked(
				Qt::NoModifier,
				Qt::LeftButton);
		}
	}, searchContainer->lifetime());

	base::install_event_filter(_searchField, [=](not_null<QEvent*> e) {
		if (e->type() != QEvent::KeyPress) {
			return base::EventFilterResult::Continue;
		}
		const auto key = static_cast<QKeyEvent*>(e.get())->key();
		if (key == Qt::Key_Up
			|| key == Qt::Key_Down
			|| key == Qt::Key_PageUp
			|| key == Qt::Key_PageDown) {
			handleKeyNavigation(key);
			return base::EventFilterResult::Cancel;
		}
		return base::EventFilterResult::Continue;
	}, lifetime());

	if (!_pendingQuery.isEmpty()) {
		_searchField->setText(base::take(_pendingQuery));
	}

	return base::make_weak(not_null<Ui::RpWidget*>{ searchContainer });
}

void Search::setupContent() {
	const auto content = Ui::CreateChild<Ui::VerticalLayout>(this);

	_list = content->add(object_ptr<Ui::VerticalLayout>(content));

	setupCustomizations();
	buildIndex();
	rebuildResults(QString());

	controller()->session().faqSuggestions().loadedValue(
	) | rpl::filter([](bool loaded) {
		return loaded;
	}) | rpl::take(1) | rpl::on_next([=] {
		for (auto i = _faqStartIndex; i < int(_entries.size()); ++i) {
			const auto it = _buttonCache.find(i);
			if (it != _buttonCache.end()) {
				_trackedButtons.remove(it->second);
				delete it->second;
				_buttonCache.erase(it);
			}
		}
		buildIndex();
		const auto query = _searchController
			? _searchController->query()
			: QString();
		rebuildResults(query);
	}, lifetime());

	Ui::ResizeFitChild(this, content);
}

void Search::setupCustomizations() {
	const auto isPaused = Window::PausedIn(
		controller(),
		Window::GifPauseReason::Layer);
	const auto add = [&](const QString &id, ResultCustomization value) {
		_customizations[id] = std::move(value);
	};

	add(u"main/credits"_q, {
		.hook = [=](not_null<Ui::SettingsButton*> b) {
			AddPremiumStar(b, true, isPaused);
		},
		.st = &st::settingsSearchResult,
	});
	add(u"main/premium"_q, {
		.hook = [=](not_null<Ui::SettingsButton*> b) {
			AddPremiumStar(b, false, isPaused);
		},
		.st = &st::settingsSearchResult,
	});
}

void Search::buildIndex() {
	_entries.clear();
	_firstLetterIndex.clear();
	_entryIdToIndex.clear();

	const auto &registry = Builder::SearchRegistry::Instance();
	const auto rawEntries = registry.collectAll(&controller()->session());

	_entries.reserve(rawEntries.size());
	for (const auto &entry : rawEntries) {
		const auto index = int(_entries.size());
		auto indexed = IndexedEntry{
			.entry = entry,
			.terms = PrepareEntryWords(entry),
			.depth = CalculateDepth(entry.section, registry),
		};
		if (!entry.id.isEmpty()) {
			_entryIdToIndex[entry.id] = index;
		}
		_entries.push_back(std::move(indexed));
	}

	_faqStartIndex = int(_entries.size());

	const auto &faq = controller()->session().faqSuggestions();
	for (const auto &faqEntry : faq.entries()) {
		auto entry = Builder::SearchEntry{
			.title = faqEntry.title,
		};
		auto indexed = IndexedEntry{
			.entry = std::move(entry),
			.terms = TextUtilities::PrepareSearchWords(faqEntry.title),
			.depth = 1000,
			.faqUrl = faqEntry.url,
			.faqSection = faqEntry.section,
		};
		_entries.push_back(std::move(indexed));
	}

	for (auto i = 0; i < int(_entries.size()); ++i) {
		for (const auto &term : _entries[i].terms) {
			if (!term.isEmpty()) {
				_firstLetterIndex[term[0]].insert(i);
			}
		}
	}
}

void Search::clearSelection() {
	if (_selected >= 0 && _selected < int(_visibleButtons.size())) {
		_visibleButtons[_selected]->setSynteticOver(false);
	}
	_selected = -1;
}

void Search::scrollToButton(not_null<Ui::SettingsButton*> button) {
	const auto scrollIn = [&](auto &&scroll) {
		if (const auto inner = scroll->widget()) {
			const auto globalPos = button->mapToGlobal(QPoint(0, 0));
			const auto localPos = inner->mapFromGlobal(globalPos);
			scroll->scrollToY(
				localPos.y(),
				localPos.y() + button->height());
		}
	};
	for (auto widget = button->parentWidget()
		; widget
		; widget = widget->parentWidget()) {
		if (const auto scroll = dynamic_cast<Ui::ScrollArea*>(widget)) {
			scrollIn(scroll);
			return;
		}
		if (const auto scroll = dynamic_cast<Ui::ElasticScroll*>(widget)) {
			scrollIn(scroll);
			return;
		}
	}
}

void Search::selectByKeyboard(int newSelected) {
	const auto count = int(_visibleButtons.size());
	if (!count) {
		return;
	}
	newSelected = std::clamp(newSelected, 0, count - 1);
	if (newSelected == _selected) {
		return;
	}
	const auto applySelection = [&] {
		for (auto i = 0; i < count; ++i) {
			if (i != newSelected && _visibleButtons[i]->isOver()) {
				_visibleButtons[i]->setSynteticOver(false);
			}
		}
		_selected = newSelected;
		_visibleButtons[_selected]->setSynteticOver(true);
	};
	applySelection();
	scrollToButton(_visibleButtons[_selected]);
	applySelection();
}

void Search::setupButtonMouseTracking(
		not_null<Ui::SettingsButton*> button) {
	if (!_trackedButtons.emplace(button).second) {
		return;
	}
	button->events(
	) | rpl::filter([](not_null<QEvent*> e) {
		return e->type() == QEvent::Enter;
	}) | rpl::on_next([=] {
		if (_selected >= 0) {
			clearSelection();
		}
	}, button->lifetime());
}

void Search::handleKeyNavigation(int key) {
	constexpr auto kPageSkip = 5;
	const auto startIndex = [&] {
		if (_selected >= 0) {
			return _selected;
		}
		for (auto i = 0; i < int(_visibleButtons.size()); ++i) {
			if (_visibleButtons[i]->isOver()) {
				return i;
			}
		}
		return -1;
	}();

	if (key == Qt::Key_Down) {
		selectByKeyboard((startIndex < 0) ? 0 : (startIndex + 1));
	} else if (key == Qt::Key_Up) {
		if (startIndex > 0) {
			selectByKeyboard(startIndex - 1);
		} else if (startIndex == 0) {
			clearSelection();
		}
	} else if (key == Qt::Key_PageDown) {
		selectByKeyboard((startIndex < 0) ? 0 : (startIndex + kPageSkip));
	} else if (key == Qt::Key_PageUp) {
		if (startIndex > 0) {
			selectByKeyboard(startIndex - kPageSkip);
		} else if (startIndex == 0) {
			clearSelection();
		}
	}
}

not_null<Ui::SettingsButton*> Search::createEntryButton(
		int entryIndex,
		const QString &subtitle) {
	const auto &indexed = _entries[entryIndex];
	const auto &entry = indexed.entry;
	const auto hasIcon = entry.icon.icon != nullptr;
	const auto hasCheckIcon = !hasIcon
		&& (entry.checkIcon != Builder::SearchEntryCheckIcon::None);

	const auto it = _customizations.find(entry.id);
	const auto custom = (it != _customizations.end())
		? &it->second
		: nullptr;

	const auto &st = custom && custom->st
		? *custom->st
		: (hasIcon || hasCheckIcon)
		? st::settingsSearchResult
		: st::settingsSearchResultNoIcon;

	const auto button = CreateSearchResultButtonRaw(
		this,
		entry.title,
		subtitle,
		st,
		IconDescriptor{ entry.icon.icon },
		(hasCheckIcon
			? entry.checkIcon
			: Builder::SearchEntryCheckIcon::None));

	if (custom && custom->hook) {
		custom->hook(button);
	}

	const auto controlId = entry.id;
	const auto targetSection = entry.section;
	const auto deeplink = entry.deeplink;
	if (!deeplink.isEmpty() || targetSection || !controlId.isEmpty()) {
		button->addClickHandler([=] {
			bumpRecentEntry(controlId);
			if (!deeplink.isEmpty()) {
				Core::App().openLocalUrl(
					deeplink,
					QVariant::fromValue(ClickHandlerContext{
						.sessionWindow = base::make_weak(controller()),
					}));
			} else {
				if (!controlId.isEmpty()) {
					controller()->setHighlightControlId(controlId);
				}
				showOtherMethod()(targetSection);
			}
		});
	}
	const auto copyLink = !deeplink.isEmpty()
		? deeplink
		: Core::DeepLinks::SettingsDeepLink(targetSection, controlId);
	if (!copyLink.isEmpty() || !controlId.isEmpty()) {
		base::install_event_filter(button, [=](not_null<QEvent*> e) {
			if (e->type() != QEvent::ContextMenu) {
				return base::EventFilterResult::Continue;
			}
			const auto inRecent = !controlId.isEmpty()
				&& ranges::contains(
					controller()->session().recentSettingsSearches().list(),
					controlId);
			if (copyLink.isEmpty() && !inRecent) {
				return base::EventFilterResult::Continue;
			}
			_contextMenu = base::make_unique_q<Ui::PopupMenu>(
				button,
				st::popupMenuWithIcons);
			if (!copyLink.isEmpty()) {
				_contextMenu->addAction(
					tr::lng_context_copy_link(tr::now),
					[=] {
						TextUtilities::SetClipboardText(
							TextForMimeData::Simple(copyLink));
						controller()->showToast({
							.text = {
								tr::lng_channel_public_link_copied(tr::now),
							},
							.iconLottie = u"toast/voip_invite"_q,
							.iconLottieSize = st::toastLottieIconSize,
						});
					},
					&st::menuIconLink);
			}
			if (inRecent) {
				_contextMenu->addAction(
					tr::lng_recent_remove(tr::now),
					[=] {
						controller()->session().recentSettingsSearches().remove(
							controlId);
						const auto query = _searchController
							? _searchController->query()
							: QString();
						rebuildResults(query);
					},
					&st::menuIconDelete);
			}
			_contextMenu->popup(QCursor::pos());
			return base::EventFilterResult::Cancel;
		}, button->lifetime());
	}

	_buttonCache.emplace(entryIndex, button);
	return button;
}

void Search::rebuildResults(const QString &query) {
	_list->detachRows();
	clearSelection();
	_visibleButtons.clear();

	const auto queryWords = TextUtilities::PrepareSearchWords(query);

	if (queryWords.isEmpty()) {
		rebuildRecentResults();
		rebuildFaqResults();
		_list->resizeToWidth(_list->width());
		return;
	}

	auto results = std::vector<SearchResultItem>();
	{
		auto toFilter = (const base::flat_set<int>*)nullptr;
		for (const auto &word : queryWords) {
			if (word.isEmpty()) {
				continue;
			}
			const auto it = _firstLetterIndex.find(word[0]);
			if (it == _firstLetterIndex.end() || it->second.empty()) {
				toFilter = nullptr;
				break;
			} else if (!toFilter || it->second.size() < toFilter->size()) {
				toFilter = &it->second;
			}
		}

		if (toFilter) {
			for (const auto entryIndex : *toFilter) {
				const auto &indexed = _entries[entryIndex];
				auto matched = 0;
				for (const auto &queryWord : queryWords) {
					for (const auto &term : indexed.terms) {
						if (term.startsWith(queryWord)) {
							++matched;
							break;
						}
					}
				}
				if (matched > 0) {
					results.push_back({
						.index = entryIndex,
						.matchCount = matched,
					});
				}
			}

			ranges::sort(results, [&](const auto &a, const auto &b) {
				if (a.matchCount != b.matchCount) {
					return a.matchCount > b.matchCount;
				}
				const auto &entryA = _entries[a.index];
				const auto &entryB = _entries[b.index];
				if (entryA.depth != entryB.depth) {
					return entryA.depth < entryB.depth;
				}
				return entryA.entry.title < entryB.entry.title;
			});
		}
	}

	if (results.empty() && !queryWords.isEmpty()) {
		_list->add(
			object_ptr<Ui::FlatLabel>(
				_list,
				tr::lng_search_tab_no_results(),
				st::settingsSearchNoResults),
			st::settingsSearchNoResultsPadding);
	} else {
		const auto &registry = Builder::SearchRegistry::Instance();
		const auto faqSubtitle = tr::lng_settings_faq_subtitle(tr::now);
		const auto weak = base::make_weak(controller());

		for (const auto &result : results) {
			const auto entryIndex = result.index;
			const auto &indexed = _entries[entryIndex];
			const auto &entry = indexed.entry;
			const auto isFaq = !indexed.faqUrl.isEmpty();

			const auto cached = _buttonCache.find(entryIndex);
			if (cached != _buttonCache.end()) {
				addButton(cached->second);
				continue;
			}

			if (isFaq) {
				const auto subtitle = faqSubtitle
					+ u" > "_q
					+ indexed.faqSection;
				const auto button = CreateSearchResultButtonRaw(
					this,
					entry.title,
					subtitle,
					st::settingsSearchResultNoIcon,
					IconDescriptor{},
					Builder::SearchEntryCheckIcon::None);

				const auto url = indexed.faqUrl;
				button->addClickHandler([=] {
					UrlClickHandler::Open(
						url,
						QVariant::fromValue(ClickHandlerContext{
							.sessionWindow = weak,
						}));
				});

				_buttonCache.emplace(entryIndex, button);
				addButton(button);
			} else {
				const auto parentsOnly = entry.id.isEmpty();
				const auto subtitle = registry.sectionPath(
					entry.section,
					parentsOnly);
				addButton(createEntryButton(entryIndex, subtitle));
			}
		}
	}

	_list->resizeToWidth(_list->width());
}

void Search::sectionSaveState(std::any &state) {
	const auto query = _searchController
		? _searchController->query()
		: _pendingQuery;
	if (!query.isEmpty()) {
		state = SearchSectionState{ query };
	}
}

void Search::sectionRestoreState(const std::any &state) {
	const auto saved = std::any_cast<SearchSectionState>(&state);
	if (saved && !saved->query.isEmpty()) {
		if (_searchField) {
			_searchField->setText(saved->query);
		} else {
			_pendingQuery = saved->query;
		}
	}
}

void Search::bumpRecentEntry(const QString &entryId) {
	if (!entryId.isEmpty()) {
		controller()->session().recentSettingsSearches().bump(entryId);
	}
}

void Search::rebuildRecentResults() {
	const auto &recentIds
		= controller()->session().recentSettingsSearches().list();
	if (recentIds.empty()) {
		return;
	}

	const auto &registry = Builder::SearchRegistry::Instance();

	auto added = false;
	for (const auto &entryId : recentIds) {
		const auto it = _entryIdToIndex.find(entryId);
		if (it == _entryIdToIndex.end()) {
			continue;
		}
		if (!added) {
			Ui::AddSubsectionTitle(_list, tr::lng_recent_title());
			added = true;
		}
		const auto entryIndex = it->second;
		const auto cached = _buttonCache.find(entryIndex);
		if (cached != _buttonCache.end()) {
			addButton(cached->second);
			continue;
		}
		const auto &entry = _entries[entryIndex].entry;
		const auto parentsOnly = entry.id.isEmpty();
		const auto subtitle = registry.sectionPath(
			entry.section,
			parentsOnly);
		addButton(createEntryButton(entryIndex, subtitle));
	}
}

void Search::rebuildFaqResults() {
	if (_faqStartIndex >= int(_entries.size())) {
		return;
	}

	if (!_visibleButtons.empty()) {
		Ui::AddSubsectionTitle(_list, tr::lng_settings_faq());
	}

	const auto faqSubtitle = tr::lng_settings_faq_subtitle(tr::now);
	const auto weak = base::make_weak(controller());

	for (auto i = _faqStartIndex; i < int(_entries.size()); ++i) {
		const auto &indexed = _entries[i];

		const auto cached = _buttonCache.find(i);
		if (cached != _buttonCache.end()) {
			addButton(cached->second);
			continue;
		}

		const auto subtitle = faqSubtitle + u" > "_q + indexed.faqSection;
		const auto button = CreateSearchResultButtonRaw(
			this,
			indexed.entry.title,
			subtitle,
			st::settingsSearchResultNoIcon,
			IconDescriptor{},
			Builder::SearchEntryCheckIcon::None);

		const auto url = indexed.faqUrl;
		button->addClickHandler([=] {
			UrlClickHandler::Open(
				url,
				QVariant::fromValue(ClickHandlerContext{
					.sessionWindow = weak,
				}));
		});

		_buttonCache.emplace(i, button);
		addButton(button);
	}
}

void Search::addButton(not_null<Ui::SettingsButton*> button) {
	button->show();
	_list->add(object_ptr<Ui::SettingsButton>::fromRaw(button));
	_visibleButtons.push_back(button);
	setupButtonMouseTracking(button);
}

} // namespace Settings
