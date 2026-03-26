/*
This file is part of Telegram Desktop,
the official desktop application for the Telegram messaging service.

For license and copyright information please follow this link:
https://github.com/telegramdesktop/tdesktop/blob/master/LEGAL
*/
#include "history/view/history_view_draw_to_reply.h"

#include "data/data_document.h"
#include "data/data_document_media.h"
#include "data/data_photo_media.h"
#include "data/data_session.h"
#include "editor/editor_layer_widget.h"
#include "editor/photo_editor.h"
#include "mainwidget.h"
#include "ui/image/image.h"
#include "window/window_session_controller.h"

namespace HistoryView {

QImage ResolveDrawToReplyImage(
		not_null<Data::Session*> data,
		const Data::DrawToReplyRequest &request) {
	auto image = QImage();
	if (request.photoId) {
		const auto photo = data->photo(request.photoId);
		const auto media = photo->createMediaView();
		if (const auto large = media->image(Data::PhotoSize::Large)) {
			image = large->original();
		}
	}
	if (image.isNull() && request.documentId) {
		const auto document = data->document(request.documentId);
		if (!document->isImage()) {
			return QImage();
		}
		const auto media = document->createMediaView();
		document->saveFromDataSilent();
		auto &location = document->location(true);
		if (location.accessEnable()) {
			image = Images::Read({ .path = location.name() }).image;
		} else {
			image = Images::Read({ .content = media->bytes() }).image;
		}
		location.accessDisable();
	}
	return image;
}

void OpenDrawToReplyEditor(
		not_null<Window::SessionController*> controller,
		QImage image,
		Fn<void(QImage &&)> done) {
	if (image.isNull()) {
		return;
	}
	const auto parent = controller->content();
	const auto parentWidget = not_null<QWidget*>{ parent.get() };
	auto fileImage = std::make_shared<Image>(std::move(image));
	auto editor = base::make_unique_q<Editor::PhotoEditor>(
		parentWidget,
		&controller->window(),
		fileImage,
		Editor::PhotoModifications());
	const auto raw = editor.get();
	auto layer = std::make_unique<Editor::LayerWidget>(
		parentWidget,
		std::move(editor));
	Editor::InitEditorLayer(
		layer.get(),
		raw,
		[fileImage, done = std::move(done)](
				Editor::PhotoModifications mods) mutable {
			auto result = Editor::ImageModified(
				fileImage->original(),
				mods);
			if (result.isNull()) {
				return;
			}
			if (done) {
				done(std::move(result));
			}
		});
	controller->showLayer(std::move(layer), Ui::LayerOption::KeepOther);
}

} // namespace HistoryView
