/*
This file is part of Telegram Desktop,
the official desktop application for the Telegram messaging service.

For license and copyright information please follow this link:
https://github.com/telegramdesktop/tdesktop/blob/master/LEGAL
*/
#include "menu/menu_item_download_files.h"

#include "core/application.h"
#include "core/core_settings.h"
#include "core/file_utilities.h"
#include "data/data_document.h"
#include "data/data_document_media.h"
#include "data/data_file_click_handler.h"
#include "data/data_photo.h"
#include "data/data_photo_media.h"
#include "data/data_session.h"
#include "history/history_inner_widget.h"
#include "history/history_item.h"
#include "history/view/history_view_list_widget.h" // HistoryView::SelectedItem.
#include "lang/lang_keys.h"
#include "main/main_session.h"
#include "storage/storage_account.h"
#include "ui/text/text_utilities.h"
#include "ui/toast/toast.h"
#include "ui/widgets/popup_menu.h"
#include "window/window_session_controller.h"
#include "styles/style_menu_icons.h"
#include "styles/style_widgets.h"

namespace Menu {
namespace {

using DocumentViewPtr = std::shared_ptr<Data::DocumentMedia>;
using Documents = std::vector<std::pair<DocumentViewPtr, FullMsgId>>;
using Photos = std::vector<std::shared_ptr<Data::PhotoMedia>>;

[[nodiscard]] bool Added(
		HistoryItem *item,
		Documents &documents,
		Photos &photos) {
	if (item && !item->forbidsForward()) {
		if (const auto media = item->media()) {
			if (const auto photo = media->photo()) {
				if (const auto view = photo->activeMediaView()) {
					if (view->loaded()) {
						photos.push_back(view);
						return true;
					}
				}
			} else if (const auto document = media->document()) {
				if (const auto view = document->activeMediaView()) {
					if (!view->loaded()) {
						documents.emplace_back(view, item->fullId());
						return true;
					}
				}
			}
		}
	}
	return false;
}

void AddAction(
		not_null<Ui::PopupMenu*> menu,
		not_null<Window::SessionController*> controller,
		Documents &&documents,
		Photos &&photos,
		Fn<void()> callback) {
	const auto text = documents.empty()
		? tr::lng_context_save_images_selected(tr::now)
		: tr::lng_context_save_documents_selected(tr::now);
	const auto icon = documents.empty()
		? &st::menuIconSaveImage
		: &st::menuIconDownload;
	const auto showToast = documents.empty();

	const auto saveImages = [=](const QString &folderPath) {
		const auto session = &controller->session();
		const auto downloadPath = folderPath.isEmpty()
			? Core::App().settings().downloadPath()
			: folderPath;
		const auto path = downloadPath.isEmpty()
			? File::DefaultDownloadPath(session)
			: (downloadPath == FileDialog::Tmp())
			? session->local().tempDirectory()
			: downloadPath;
		if (path.isEmpty()) {
			return;
		}

		const auto fullPath = [&](int i) {
			return filedialogDefaultName(
				u"photo_"_q + QString::number(i),
				u".jpg"_q,
				path);
		};
		auto lastPath = QString();
		for (auto i = 0; i < photos.size(); i++) {
			lastPath = fullPath(i + 1);
			photos[i]->saveToFile(lastPath);
		}

		if (showToast) {
			const auto filter = [lastPath](const auto ...) {
				File::ShowInFolder(lastPath);
				return false;
			};
			const auto config = Ui::Toast::Config{
				.text = (photos.size() > 1
						? tr::lng_mediaview_saved_images_to
						: tr::lng_mediaview_saved_to)(
					tr::now,
					lt_downloads,
					Ui::Text::Link(
						tr::lng_mediaview_downloads(tr::now),
						"internal:show_saved_message"),
					Ui::Text::WithEntities),
				.st = &st::defaultToast,
				.filter = filter,
			};
			Ui::Toast::Show(Window::Show(controller).toastParent(), config);
		}
	};
	const auto saveDocuments = [=](const QString &folderPath) {
		for (const auto &pair : documents) {
			const auto &document = pair.first->owner();
			const auto &origin = pair.second;
			if (!folderPath.isEmpty()) {
				document->save(origin, folderPath + document->filename());
			} else {
				DocumentSaveClickHandler::SaveAndTrack(origin, document);
			}
		}
	};

	menu->addAction(text, [=] {
		const auto save = [=](const QString &folderPath) {
			saveImages(folderPath);
			saveDocuments(folderPath);
			callback();
		};
		if (Core::App().settings().askDownloadPath()) {
			const auto initialPath = [] {
				const auto path = Core::App().settings().downloadPath();
				if (!path.isEmpty() && path != FileDialog::Tmp()) {
					return path.left(path.size()
						- (path.endsWith('/') ? 1 : 0));
				}
				return QString();
			}();
			const auto handleFolder = [=](const QString &result) {
				if (!result.isEmpty()) {
					const auto folderPath = result.endsWith('/')
						? result
						: (result + '/');
					save(folderPath);
				}
			};
			FileDialog::GetFolder(
				nullptr,
				tr::lng_download_path_choose(tr::now),
				initialPath,
				handleFolder);
		} else {
			save(QString());
		}
	}, icon);
}

} // namespace

void AddDownloadFilesAction(
		not_null<Ui::PopupMenu*> menu,
		not_null<Window::SessionController*> window,
		const std::vector<HistoryView::SelectedItem> &selectedItems,
		not_null<HistoryView::ListWidget*> list) {
	if (selectedItems.empty()) {
		return;
	}
	auto docs = Documents();
	auto photos = Photos();
	for (const auto &selectedItem : selectedItems) {
		const auto &id = selectedItem.msgId;
		const auto item = window->session().data().message(id);

		if (!Added(item, docs, photos)) {
			return;
		}
	}
	const auto done = [weak = Ui::MakeWeak(list)] {
		if (const auto strong = weak.data()) {
			strong->cancelSelection();
		}
	};
	AddAction(menu, window, std::move(docs), std::move(photos), done);
}

void AddDownloadFilesAction(
		not_null<Ui::PopupMenu*> menu,
		not_null<Window::SessionController*> window,
		const std::map<HistoryItem*, TextSelection, std::less<>> &items,
		not_null<HistoryInner*> list) {
	if (items.empty()) {
		return;
	}
	auto docs = Documents();
	auto photos = Photos();
	for (const auto &pair : items) {
		if (!Added(pair.first, docs, photos)) {
			return;
		}
	}
	const auto done = [weak = Ui::MakeWeak(list)] {
		if (const auto strong = weak.data()) {
			strong->clearSelected();
		}
	};
	AddAction(menu, window, std::move(docs), std::move(photos), done);
}

} // namespace Menu
