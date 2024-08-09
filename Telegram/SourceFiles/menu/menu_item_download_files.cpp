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
#include "mainwindow.h"
#include "storage/storage_account.h"
#include "ui/text/text_utilities.h"
#include "ui/toast/toast.h"
#include "ui/widgets/popup_menu.h"
#include "window/window_session_controller.h"
#include "window/window_controller.h"
#include "styles/style_menu_icons.h"
#include "styles/style_widgets.h"

namespace Menu {
namespace {

using Documents = std::vector<std::pair<not_null<DocumentData*>, FullMsgId>>;
using Photos = std::vector<std::pair<not_null<PhotoData*>, FullMsgId>>;

[[nodiscard]] bool Added(
		HistoryItem *item,
		Documents &documents,
		Photos &photos) {
	if (item && !item->forbidsForward()) {
		if (const auto media = item->media()) {
			if (const auto photo = media->photo()) {
				photos.emplace_back(photo, item->fullId());
				return true;
			} else if (const auto document = media->document()) {
				documents.emplace_back(document, item->fullId());
				return true;
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
	const auto shouldShowToast = documents.empty();

	const auto weak = base::make_weak(controller);
	const auto saveImages = [=](const QString &folderPath) {
		const auto controller = weak.get();
		if (!controller) {
			return;
		}
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
		QDir().mkpath(path);

		const auto showToast = !shouldShowToast
			? Fn<void(const QString &)>(nullptr)
			: [=](const QString &lastPath) {
				const auto filter = [lastPath](const auto ...) {
					File::ShowInFolder(lastPath);
					return false;
				};
				controller->showToast({
					.text = (photos.size() > 1
							? tr::lng_mediaview_saved_images_to
							: tr::lng_mediaview_saved_to)(
						tr::now,
						lt_downloads,
						Ui::Text::Link(
							tr::lng_mediaview_downloads(tr::now),
							"internal:show_saved_message"),
						Ui::Text::WithEntities),
					.filter = filter,
					.st = &st::defaultToast,
				});
			};

		auto views = std::vector<std::shared_ptr<Data::PhotoMedia>>();
		for (const auto &[photo, fullId] : photos) {
			if (const auto view = photo->createMediaView()) {
				view->wanted(Data::PhotoSize::Large, fullId);
				views.push_back(view);
			}
		}

		const auto finalCheck = [=] {
			for (const auto &[photo, _] : photos) {
				if (photo->loading()) {
					return false;
				}
			}
			return true;
		};

		const auto saveToFiles = [=] {
			const auto fullPath = [&](int i) {
				return filedialogDefaultName(
					u"photo_"_q + QString::number(i),
					u".jpg"_q,
					path);
			};
			auto lastPath = QString();
			for (auto i = 0; i < views.size(); i++) {
				lastPath = fullPath(i + 1);
				views[i]->saveToFile(lastPath);
			}
			if (showToast) {
				showToast(lastPath);
			}
		};

		if (finalCheck()) {
			saveToFiles();
		} else {
			auto lifetime = std::make_shared<rpl::lifetime>();
			session->downloaderTaskFinished(
			) | rpl::start_with_next([=]() mutable {
				if (finalCheck()) {
					saveToFiles();
					base::take(lifetime)->destroy();
				}
			}, *lifetime);
		}
	};
	const auto saveDocuments = [=](const QString &folderPath) {
		for (const auto &[document, origin] : documents) {
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
		const auto controller = weak.get();
		if (!controller) {
			return;
		}
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
				controller->window().widget().get(),
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
