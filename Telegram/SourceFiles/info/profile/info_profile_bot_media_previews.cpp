/*
This file is part of Telegram Desktop,
the official desktop application for the Telegram messaging service.

For license and copyright information please follow this link:
https://github.com/telegramdesktop/tdesktop/blob/master/LEGAL
*/
#include "info/profile/info_profile_bot_media_previews.h"

#include "api/api_bot_media_preview_upload.h"
#include "api/api_bot_media_previews.h"
#include "boxes/peers/bot_info_language_button.h"
#include "data/data_bot_info_localization.h"
#include "data/data_changes.h"
#include "data/data_file_origin.h"
#include "data/data_session.h"
#include "data/data_user.h"
#include "history/history_item.h"
#include "info/media/info_media_list_widget.h"
#include "info/profile/tabs/adapters/info_profile_tab_sub_controller.h"
#include "info/profile/tabs/info_profile_tab_skeleton.h"
#include "info/profile/info_profile_bot_preview_provider.h"
#include "info/info_controller.h"
#include "lang/lang_keys.h"
#include "main/main_app_config.h"
#include "main/main_session.h"
#include "media/stories/media_stories_post.h"
#include "media/view/media_view_open_common.h"
#include "spellcheck/spellcheck_types.h"
#include "ui/boxes/choose_language_box.h"
#include "ui/boxes/confirm_box.h"
#include "ui/controls/sub_tabs.h"
#include "ui/layers/generic_box.h"
#include "ui/widgets/menu/menu_add_action_callback.h"
#include "ui/widgets/menu/menu_add_action_callback_factory.h"
#include "ui/widgets/buttons.h"
#include "ui/widgets/labels.h"
#include "ui/widgets/popup_menu.h"
#include "ui/wrap/slide_wrap.h"
#include "ui/wrap/vertical_layout.h"
#include "ui/vertical_list.h"
#include "window/window_controller.h"
#include "window/window_session_controller.h"

#include "styles/style_info.h"
#include "styles/style_info_profile_bot_media_previews.h"
#include "styles/style_layers.h"
#include "styles/style_menu_icons.h"

namespace Info::Profile {
namespace {

const auto kGeneral = u"general"_q;
const auto kAddLanguage = u"add"_q;
using Medium = Api::BotPreviewMedia;
using MediaList = std::vector<Medium>;

[[nodiscard]] bool CanManage(not_null<UserData*> bot) {
	const auto info = bot->botInfo.get();
	return info && info->hasMainApp && info->canEditInformation;
}

class PreviewPage final : public Ui::VerticalLayout {
public:
	PreviewPage(
		QWidget *parent,
		MediaTabContext context,
		not_null<UserData*> bot);
	~PreviewPage();

	[[nodiscard]] TabTopBarBindings bindings();
	void setViewport(int top, int bottom);
	void setOverlay(int height);
	void deactivate();

private:
	enum class Status { Loading, Failed, Ready, Uploading, Saving };

	void load();
	void loaded(Api::BotMediaPreviewList list);
	void refresh(std::optional<Medium> published = std::nullopt);
	void refreshLanguages();
	void refreshFooter();
	void addLabel(
		rpl::producer<QString> text,
		const style::FlatLabel &style,
		const QMargins &margins);
	void addButton(rpl::producer<QString> text, Fn<void()> click);
	void addSecondary(rpl::producer<QString> text, Fn<void()> click);
	void fillMenu(const Ui::Menu::MenuCallback &add);
	void fillTileMenu(not_null<Ui::PopupMenu*> menu, MsgId id);
	void onSelection(SelectedItems items);
	void selectionAction(SelectionAction action);

	void chooseLanguage();
	void switchLanguage(QString language, bool created = false);
	void open(int index);
	void setCover(not_null<DocumentData*> document, crl::time position);
	void chooseMedia(std::optional<Medium> replaced = std::nullopt);
	void upload(
		Api::BotMediaPreviewUpload::Media media,
		std::optional<Medium> replaced);
	void finishUpload(
		std::optional<Medium> published,
		std::optional<int> replacing);
	void confirmDelete(MediaList items, bool translation = false);
	void remove(MediaList items);
	void setReordering(bool enabled);
	void reorder(int from, int to);
	void saveOrder();
	void changeFailed(rpl::producer<QString> error);

	[[nodiscard]] MediaList &items();
	[[nodiscard]] const MediaList &items() const;
	[[nodiscard]] bool canChange() const;
	[[nodiscard]] bool canAdd() const;
	[[nodiscard]] int limit() const;
	[[nodiscard]] QString currentTab() const;
	[[nodiscard]] Data::FileOrigin origin() const;

	const not_null<Window::SessionController*> _window;
	const not_null<UserData*> _bot;
	const bool _editable;
	MediaSubController _gridController;
	Api::BotMediaPreviews _api;
	std::unique_ptr<Api::BotMediaPreviewUpload> _upload;
	std::optional<QString> _language;
	std::vector<QString> _languages;
	base::flat_set<QString> _draftLanguages;
	base::flat_map<QString, MediaList> _lists;
	std::shared_ptr<const Data::FileOrigin> _origin;
	Status _status = Status::Loading;
	SelectedItems _selection;
	rpl::variable<int> _count;
	int _uploadFooterHeight = 0;
	bool _reordering = false;
	bool _savingOrder = false;

	Ui::SlideWrap<Ui::SubTabs> *_tabs = nullptr;
	Ui::VerticalLayout *_footer = nullptr;
	Media::ListWidget *_grid = nullptr;
	BotPreviewProvider *_provider = nullptr;

};

class PreviewTab final : public MediaTabContent {
public:
	PreviewTab(MediaTabContext context, not_null<UserData*> bot);

	not_null<Ui::RpWidget*> widget() override;
	TabTopBarBindings topBarBindings() override;
	void resizeToWidth(int newWidth) override;
	void setVisibleRegion(int top, int bottom) override;
	void setTopOverlay(int height) override;
	void deactivated() override;

private:
	object_ptr<PreviewPage> _page;

};

PreviewPage::PreviewPage(
	QWidget *parent,
	MediaTabContext context,
	not_null<UserData*> bot)
: VerticalLayout(parent)
, _window(context.controller->parentController())
, _bot(bot)
, _editable(CanManage(bot))
, _gridController(
	context.controller,
	Storage::SharedMediaType::PhotoVideo,
	nullptr,
	false)
, _api(bot)
, _language(_editable ? std::optional<QString>(QString()) : std::nullopt)
, _origin(std::make_shared<Data::FileOrigin>(origin())) {
	auto languages = object_ptr<Ui::SubTabs>(
		this,
		st::collectionSubTabs,
		Ui::SubTabs::Options{ .centered = true });
	const auto raw = languages.data();
	_tabs = add(
		object_ptr<Ui::SlideWrap<Ui::SubTabs>>(
			this,
			std::move(languages)));
	_tabs->toggle(false, anim::type::instant);
	const auto activate = [=](const QString &id) {
		if (!canChange()) {
			raw->setActiveTab(currentTab());
		} else if (id == kAddLanguage) {
			raw->setActiveTab(currentTab());
			chooseLanguage();
		} else {
			switchLanguage(id == kGeneral ? QString() : id);
		}
	};
	raw->activated() | rpl::on_next(activate, lifetime());

	auto provider = std::make_unique<BotPreviewProvider>(
		bot,
		_editable,
		[=](int index) { open(index); });
	_provider = provider.get();
	_grid = add(
		object_ptr<Media::ListWidget>(
			this,
			&_gridController,
			std::move(provider)));
	_grid->setPreloadEnabled(false);
	const auto menu = [=](not_null<Ui::PopupMenu*> menu, auto item) {
		fillTileMenu(menu, item->id);
	};
	_grid->setContextMenuFactory(menu);
	_footer = add(
		object_ptr<Ui::VerticalLayout>(this),
		style::al_justify);
	_lists.emplace(_language.value_or(QString()));
	const auto selection = [=](SelectedItems items) {
		onSelection(std::move(items));
	};
	_grid->selectedListValue() | rpl::on_next(selection, lifetime());
	load();
}

PreviewPage::~PreviewPage() {
	lifetime().destroy();
	clear();
}

TabTopBarBindings PreviewPage::bindings() {
	auto count = _count.value() | rpl::map([](int count) {
		return count
			? tr::lng_bot_media_previews_count(
				tr::now,
				lt_count,
				count,
				tr::marked)
			: tr::marked();
	});
	const auto action = [=](SelectionAction action) {
		selectionAction(action);
	};
	auto result = TabTopBarBindings{
		.title = tr::lng_bot_media_previews(tr::marked),
		.subtitle = std::move(count),
		.selectedItems = _grid->selectedListValue(),
		.selectionAction = crl::guard(this, action),
	};
	if (_editable) {
		const auto fill = [=](const Ui::Menu::MenuCallback &add) {
			fillMenu(add);
		};
		result.fillMenu = crl::guard(this, fill);
	}
	return result;
}

void PreviewPage::setViewport(int top, int bottom) {
	setVisibleTopBottom(top, bottom);
	_grid->setExternalViewportHeight(bottom - top);
}

void PreviewPage::setOverlay(int height) {
	_grid->setTopOverlayHeight(height);
}

void PreviewPage::deactivate() {
	_grid->selectionAction(SelectionAction::Clear);
	setReordering(false);
}

void PreviewPage::load() {
	_status = Status::Loading;
	refreshFooter();
	const auto done = [=](Api::BotMediaPreviewList list) {
		loaded(std::move(list));
	};
	const auto fail = [=](QString) {
		if (base::take(_uploadFooterHeight)) {
			_provider->setPending({});
		}
		_status = Status::Failed;
		refreshFooter();
	};
	_api.load(_language, done, fail);
}

void PreviewPage::loaded(Api::BotMediaPreviewList list) {
	if (_language && _language->isEmpty()) {
		auto languages = std::vector<QString>();
		for (auto &code : list.languages) {
			if (!code.isEmpty()
				&& !ranges::contains(languages, code)) {
				_draftLanguages.remove(code);
				languages.push_back(std::move(code));
			}
		}
		languages.insert(
			end(languages),
			begin(_draftLanguages),
			end(_draftLanguages));
		for (const auto &code : _languages) {
			if (!ranges::contains(languages, code)) {
				_lists.remove(code);
			}
		}
		_languages = std::move(languages);
	}
	items() = std::move(list.media);
	_uploadFooterHeight = 0;
	_status = Status::Ready;
	refresh();
}

void PreviewPage::refresh(std::optional<Medium> published) {
	_provider->setMedia(items(), _origin, published);
	_count = int(items().size());
	refreshLanguages();
	refreshFooter();
}

void PreviewPage::refreshLanguages() {
	const auto shown = _editable && !_languages.empty();
	if (shown) {
		const auto fallback = tr::lng_bot_languages_default(tr::now);
		const auto add = tr::lng_bot_languages_add(tr::now);
		auto tabs = std::vector<Ui::SubTabs::Tab>();
		tabs.push_back(
			{
				.id = kGeneral,
				.text = tr::marked(fallback),
			});
		for (const auto &code : _languages) {
			tabs.push_back(
				{
					.id = code,
					.text = tr::marked(
						BotInfoLanguageName(code)),
				});
		}
		tabs.push_back(
			{
				.id = kAddLanguage,
				.text = tr::marked(u"+ "_q + add),
			});
		_tabs->entity()->setTabs(std::move(tabs));
		_tabs->entity()->setActiveTab(currentTab());
	}
	_tabs->toggle(shown, anim::type::instant);
}

void PreviewPage::refreshFooter() {
	_footer->setMinimumHeight(_footer->height());
	const auto settle = gsl::finally([=] {
		_footer->setMinimumHeight(_uploadFooterHeight);
		_footer->resizeToWidth(_footer->widthNoMargins());
	});
	_footer->clear();
	if (_status == Status::Loading
		&& items().empty()
		&& !_uploadFooterHeight) {
		auto skeleton = CreateTabSkeleton(
			_footer,
			Storage::SharedMediaType::PhotoVideo);
		_footer->add(std::move(skeleton));
		return;
	}
	if (_status == Status::Failed) {
		addLabel(
			tr::lng_bot_media_previews_load_failed(),
			st::botPreviewText,
			st::botPreviewEmptyMargin);
		addButton(tr::lng_bot_download_retry(), [=] { load(); });
		return;
	}
	if (_status != Status::Ready || _savingOrder) {
		addLabel(
			(_status == Status::Uploading)
				? tr::lng_bot_media_previews_uploading()
				: tr::lng_bot_media_previews_saving(),
			st::botPreviewText,
			st::botPreviewFooterMargin);
		if (_status == Status::Uploading) {
			const auto cancel = [=] {
				_upload.reset();
				_uploadFooterHeight = 0;
				_provider->setPending({});
				_status = Status::Ready;
				refreshFooter();
			};
			addSecondary(tr::lng_context_cancel_upload(), cancel);
		}
		return;
	}
	if (_reordering) {
		addLabel(
			tr::lng_bot_media_previews_reorder_hint(),
			st::botPreviewText,
			st::botPreviewFooterMargin);
		addButton(tr::lng_box_done(), [=] { setReordering(false); });
		return;
	}
	if (!_selection.list.empty()) {
		return;
	}
	const auto empty = items().empty();
	if (!_editable) {
		if (empty) {
			addLabel(
				tr::lng_bot_media_previews_empty(),
				st::botPreviewTitle,
				st::botPreviewEmptyMargin);
		}
		return;
	}
	const auto general = _language->isEmpty();
	if (empty && general) {
		addLabel(
			tr::lng_bot_media_previews_empty(),
			st::botPreviewTitle,
			st::botPreviewEmptyMargin);
		addLabel(
			tr::lng_bot_media_previews_intro(
				lt_count,
				rpl::single(float64(limit()))),
			st::botPreviewText,
			st::botPreviewTextMargin);
	} else {
		auto text = general
			? tr::lng_bot_media_previews_general_about()
			: tr::lng_bot_media_previews_language_about(
				lt_language,
				rpl::single(BotInfoLanguageName(*_language)));
		const auto &margin = empty
			? st::botPreviewEmptyMargin
			: st::botPreviewFooterMargin;
		addLabel(
			std::move(text),
			st::botPreviewText,
			margin);
	}
	if (canAdd()) {
		addButton(
			tr::lng_bot_media_previews_add(),
			[=] { chooseMedia(); });
	}
	if (general && !empty) {
		addSecondary(
			tr::lng_bot_languages_add(),
			[=] { chooseLanguage(); });
	} else if (!general && empty) {
		addSecondary(
			tr::lng_bot_media_previews_delete_language(),
			[=] { confirmDelete({}, true); });
	}
}

void PreviewPage::addLabel(
		rpl::producer<QString> text,
		const style::FlatLabel &style,
		const QMargins &margins) {
	auto label = object_ptr<Ui::FlatLabel>(
		_footer,
		std::move(text),
		style);
	_footer->add(std::move(label), margins, style::al_top);
}

void PreviewPage::addButton(rpl::producer<QString> text, Fn<void()> click) {
	auto button = object_ptr<Ui::RoundButton>(
		_footer,
		std::move(text),
		st::botPreviewButton);
	const auto raw = _footer->add(
		std::move(button),
		st::botPreviewButtonMargin,
		style::al_top);
	raw->setClickedCallback(std::move(click));
}

void PreviewPage::addSecondary(
		rpl::producer<QString> text,
		Fn<void()> click) {
	const auto raw = _footer->add(
		object_ptr<Ui::LinkButton>(_footer, QString()),
		st::botPreviewButtonMargin,
		style::al_top);
	const auto setText = [=](QString value) {
		raw->setText(std::move(value));
	};
	std::move(text) | rpl::on_next(setText, raw->lifetime());
	raw->setClickedCallback(std::move(click));
}

void PreviewPage::fillMenu(const Ui::Menu::MenuCallback &add) {
	if (_reordering) {
		add(
			tr::lng_box_done(tr::now),
			[=] { setReordering(false); },
			nullptr);
		return;
	}
	if (!canChange()) {
		return;
	}
	if (canAdd()) {
		add(
			tr::lng_bot_media_previews_add(tr::now),
			[=] { chooseMedia(); },
			&st::menuIconAdd);
	}
	if (items().size() > 1) {
		add(
			tr::lng_gift_collection_reorder(tr::now),
			[=] { setReordering(true); },
			&st::menuIconReorder);
	}
	if (!items().empty()) {
		add(
			tr::lng_context_select_msg(tr::now),
			[=] { _grid->selectAll(); },
			&st::menuIconSelect);
	}
	if (!_language->isEmpty()) {
		const auto text = tr::lng_bot_media_previews_delete_language(
			tr::now);
		add({
			.text = text,
			.handler = [=] { confirmDelete(items(), true); },
			.icon = &st::menuIconDeleteAttention,
			.isAttention = true,
		});
	}
}

void PreviewPage::fillTileMenu(not_null<Ui::PopupMenu*> menu, MsgId id) {
	const auto index = _provider->indexOf(id);
	if (!canChange() || index < 0) {
		return;
	}
	const auto item = items()[index];
	menu->addAction(
		tr::lng_context_edit_msg(tr::now),
		[=] { chooseMedia(item); },
		&st::menuIconEdit);
	menu->addAction(
		tr::lng_context_select_msg(tr::now),
		[=] {
			const auto message = _bot->owner().message(
				_bot->id,
				id);
			if (message) {
				_grid->selectItem(message);
			}
		},
		&st::menuIconSelect);
	if (items().size() > 1) {
		menu->addAction(
			tr::lng_gift_collection_reorder(tr::now),
			[=] { setReordering(true); },
			&st::menuIconReorder);
	}
	const auto add = Ui::Menu::CreateAddActionCallback(menu);
	add(
		{
			.text = tr::lng_context_delete_msg(tr::now),
			.handler = [=] { confirmDelete({ item }); },
			.icon = &st::menuIconDeleteAttention,
			.isAttention = true,
		});
}

void PreviewPage::onSelection(SelectedItems selection) {
	_selection = std::move(selection);
	_grid->setSelectOnClick(!_selection.list.empty());
	refreshFooter();
}

void PreviewPage::selectionAction(SelectionAction action) {
	if (action == SelectionAction::Clear) {
		_grid->selectionAction(action);
	} else if (action == SelectionAction::Delete) {
		auto selected = MediaList();
		for (const auto &entry : _selection.list) {
			const auto id = entry.globalId.itemId.msg;
			const auto index = _provider->indexOf(id);
			if (index >= 0) {
				selected.push_back(items()[index]);
			}
		}
		if (!selected.empty()) {
			confirmDelete(std::move(selected));
		}
	}
}

void PreviewPage::chooseLanguage() {
	const auto chosen = [=](std::vector<LanguageId> ids) {
		if (!ids.empty() && canChange()) {
			const auto code = ids.front().twoLetterCode();
			if (Data::ValidBotInfoLangCode(code)) {
				switchLanguage(code, true);
			}
		}
	};
	_window->show(
		Box(
			Ui::ChooseLanguageBox,
			tr::lng_bot_languages_add(),
			crl::guard(this, chosen),
			std::vector<LanguageId>(),
			false,
			nullptr));
}

void PreviewPage::switchLanguage(QString language, bool created) {
	if (*_language == language && _status != Status::Failed) {
		return;
	}
	if (created && !ranges::contains(_languages, language)) {
		_draftLanguages.emplace(language);
		_languages.push_back(language);
	}
	_language = std::move(language);
	_origin = std::make_shared<Data::FileOrigin>(origin());
	_status = Status::Loading;
	refresh();
	load();
}

void PreviewPage::open(int index) {
	using namespace ::Media::View;
	const auto language = _language;
	const auto valid = [=](const Medium &item) {
		return canChange()
			&& _language == language
			&& ranges::contains(items(), item);
	};
	auto gallery = BotPreviewGallery{ .bot = _bot, .media = items() };
	if (_editable) {
		const auto edit = [=](Medium item) {
			if (valid(item)) {
				chooseMedia(item);
			}
		};
		const auto remove = [=](Medium item) {
			if (valid(item)) {
				confirmDelete({ item });
			}
		};
		gallery.edit = crl::guard(this, edit);
		gallery.remove = crl::guard(this, remove);
		const auto cover = [=](
				not_null<DocumentData*> document,
				crl::time position) {
			if (valid(document)) {
				setCover(document, position);
			}
		};
		gallery.setCover = crl::guard(this, cover);
	}
	_window->window().openInMediaView(
		OpenRequest(
			_window,
			std::move(gallery),
			index,
			origin()));
}

void PreviewPage::setCover(
		not_null<DocumentData*> document,
		crl::time position) {
	const auto cover = Api::BotMediaPreviewUpload::VideoCover{
		.document = document,
		.origin = origin(),
		.position = position,
	};
	upload(cover, document);
}

void PreviewPage::chooseMedia(std::optional<Medium> replaced) {
	if (!canChange() || (!replaced && !canAdd())) {
		return;
	}
	using namespace ::Media::Stories;
	const auto language = _language;
	const auto valid = [=, weak = base::make_weak(this)] {
		return weak && canChange() && language == _language;
	};
	const auto setup = StoryEditorSetup{
		.editing = replaced.has_value(),
		.peer = _bot->id,
		.preview = true,
	};
	const auto done = [=](StoryEditorResult result, Fn<void()> close) {
		close();
		if (valid() && (!replaced || result.mediaChanged)) {
			upload(std::move(result.media), replaced);
		}
	};
	if (replaced) {
		EditStoryMedia(
			_window,
			*replaced,
			origin(),
			setup,
			done,
			valid);
	} else {
		ChooseStoryMedia(_window, _bot, setup, done, nullptr, valid);
	}
}

void PreviewPage::upload(
		Api::BotMediaPreviewUpload::Media media,
		std::optional<Medium> replaced) {
	if (!_upload) {
		_upload = std::make_unique<Api::BotMediaPreviewUpload>(_bot);
	}
	const auto story = std::get_if<Api::SendStoryMedia>(&media);
	const auto photo = std::get_if<Api::SendStoryPhoto>(story);
	const auto video = std::get_if<Api::SendStoryVideo>(story);
	const auto cover = !story;
	const auto thumbnail = photo
		? photo->image
		: video ? video->cover : QImage();
	const auto &list = items();
	const auto at = replaced ? ranges::find(list, *replaced) : end(list);
	const auto replacing = (at == end(list))
		? std::nullopt
		: std::optional(int(at - begin(list)));
	_uploadFooterHeight = _footer->height();
	_status = Status::Uploading;
	_provider->setPending(thumbnail, replacing);
	refreshFooter();
	_uploadFooterHeight = _footer->height();
	const auto done = [=](MTPInputMedia media) {
		_status = Status::Saving;
		refreshFooter();
		const auto fail = [=](QString) {
			auto error = tr::lng_bot_media_previews_save_failed();
			changeFailed(std::move(error));
		};
		const auto saved = [=](std::optional<Medium> published) {
			if (cover) {
				_origin = std::make_shared<Data::FileOrigin>(
					origin());
			}
			finishUpload(published, replacing);
		};
		_api.save(
			*_language,
			replaced,
			std::move(media),
			saved,
			fail);
	};
	const auto fail = [=] {
		LOG(("Bot preview failed before messages.uploadMedia"));
		changeFailed(tr::lng_bot_media_previews_upload_failed());
	};
	_upload->start(std::move(media), done, fail);
}

void PreviewPage::finishUpload(
		std::optional<Medium> published,
		std::optional<int> replacing) {
	_draftLanguages.remove(*_language);
	if (published) {
		auto &list = items();
		if (replacing) {
			list[*replacing] = *published;
		} else {
			list.insert(begin(list), *published);
		}
		_status = Status::Ready;
		_uploadFooterHeight = 0;
		refresh(published);
	} else {
		load();
	}
	const auto phrase = replacing
		? tr::lng_bot_media_previews_updated
		: tr::lng_bot_media_previews_published;
	_window->showToast(phrase(tr::now));
}

void PreviewPage::confirmDelete(MediaList selected, bool translation) {
	const auto language = _language;
	const auto weak = base::make_weak(this);
	const auto confirm = [=](Fn<void()> close) {
		close();
		if (weak && _language == language && CanManage(_bot)) {
			remove(selected);
		}
	};
	auto args = Ui::ConfirmBoxArgs{
		.text = translation
			? tr::lng_bot_media_previews_delete_language_sure()
			: tr::lng_bot_media_previews_delete_many(
				lt_count,
				rpl::single(float64(selected.size()))),
		.confirmed = confirm,
		.confirmText = tr::lng_context_delete_msg(),
		.confirmStyle = &st::attentionBoxButton,
	};
	_window->show(Ui::MakeConfirmBox(std::move(args)));
}

void PreviewPage::remove(MediaList selected) {
	const auto leave = !_language->isEmpty()
		&& selected.size() == items().size();
	const auto done = [=] {
		_grid->selectionAction(SelectionAction::Clear);
		if (leave) {
			const auto code = *_language;
			_lists.remove(code);
			_draftLanguages.remove(code);
			const auto tail = ranges::remove(_languages, code);
			_languages.erase(tail, end(_languages));
			switchLanguage(QString());
		} else {
			load();
		}
	};
	if (selected.empty()) {
		done();
		return;
	}
	_status = Status::Saving;
	refreshFooter();
	const auto fail = [=](QString) {
		changeFailed(tr::lng_bot_media_previews_save_failed());
	};
	_api.remove(*_language, std::move(selected), done, fail);
}

void PreviewPage::setReordering(bool enabled) {
	_reordering = enabled;
	auto descriptor = Media::ListWidget::ReorderDescriptor();
	if (enabled) {
		descriptor.save = [=](
				int from,
				int to,
				Fn<void()>,
				Fn<void()>) {
			reorder(from, to);
		};
	}
	_grid->setReorderDescriptor(std::move(descriptor));
	_grid->resizeToWidth(_grid->width());
	refreshFooter();
}

void PreviewPage::reorder(int from, int to) {
	base::reorder(items(), from, to);
	_provider->move(from, to);
	if (!_savingOrder) {
		saveOrder();
	}
}

void PreviewPage::saveOrder() {
	_savingOrder = true;
	const auto sent = items();
	const auto done = [=] {
		_savingOrder = false;
		if (sent != items()) {
			saveOrder();
		} else {
			refreshFooter();
		}
	};
	const auto fail = [=](QString) {
		_savingOrder = false;
		setReordering(false);
		const auto text = tr::lng_bot_media_previews_save_failed(
			tr::now);
		_window->showToast(text);
		load();
	};
	_api.reorder(*_language, sent, done, fail);
}

void PreviewPage::changeFailed(rpl::producer<QString> error) {
	_uploadFooterHeight = 0;
	_provider->setPending({});
	_status = Status::Ready;
	refreshFooter();
	const auto show = [=](QString text) { _window->showToast(text); };
	std::move(error) | rpl::take(1) | rpl::on_next(show, lifetime());
}

MediaList &PreviewPage::items() {
	return _lists[_language.value_or(QString())];
}

const MediaList &PreviewPage::items() const {
	const auto i = _lists.find(_language.value_or(QString()));
	Ensures(i != end(_lists));
	return i->second;
}

bool PreviewPage::canChange() const {
	return _editable
		&& CanManage(_bot)
		&& !_bot->session().frozen()
		&& !_reordering
		&& !_savingOrder
		&& _selection.list.empty()
		&& (_status == Status::Ready || _status == Status::Failed);
}

bool PreviewPage::canAdd() const {
	return canChange()
		&& _status == Status::Ready
		&& int(items().size()) < limit();
}

int PreviewPage::limit() const {
	return _bot->session().appConfig().get<int>(
		u"bot_preview_medias_max"_q,
		10);
}

QString PreviewPage::currentTab() const {
	return (_language && !_language->isEmpty()) ? *_language : kGeneral;
}

Data::FileOrigin PreviewPage::origin() const {
	return Data::FileOriginBotMediaPreview{
		.botId = peerToUser(_bot->id),
		.language = _language,
	};
}

PreviewTab::PreviewTab(MediaTabContext context, not_null<UserData*> bot)
: _page(context.parent, context, bot) {
}

not_null<Ui::RpWidget*> PreviewTab::widget() {
	return _page.data();
}

TabTopBarBindings PreviewTab::topBarBindings() {
	return _page->bindings();
}

void PreviewTab::resizeToWidth(int newWidth) {
	_page->resizeToWidth(newWidth);
}

void PreviewTab::setVisibleRegion(int top, int bottom) {
	_page->setViewport(top, bottom);
}

void PreviewTab::setTopOverlay(int height) {
	_page->setOverlay(height);
}

void PreviewTab::deactivated() {
	_page->deactivate();
}

} // namespace

MediaTabDescriptor MakeBotMediaPreviewsTab(not_null<UserData*> bot) {
	using Flag = Data::PeerUpdate::Flag;
	return {
		.id = u"bot-media-previews"_q,
		.title = tr::lng_bot_media_previews(tr::marked),
		.shown = bot->session().changes().peerFlagsValue(
			bot,
			Flag::FullInfo | Flag::ManagedBot
		) | rpl::map([=] {
			const auto info = bot->botInfo.get();
			const auto hasMedia = info && info->hasPreviewMedias;
			return hasMedia || CanManage(bot);
		}) | rpl::distinct_until_changed(),
		.factory = [=](MediaTabContext context) {
			return std::make_unique<PreviewTab>(context, bot);
		},
	};
}

} // namespace Info::Profile
