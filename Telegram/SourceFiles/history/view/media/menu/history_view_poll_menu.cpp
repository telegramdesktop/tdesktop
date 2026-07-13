/*
This file is part of Telegram Desktop,
the official desktop application for the Telegram messaging service.

For license and copyright information please follow this link:
https://github.com/telegramdesktop/tdesktop/blob/master/LEGAL
*/
#include "history/view/media/menu/history_view_poll_menu.h"

#include "api/api_polls.h"
#include "api/api_toggling_media.h"
#include "apiwrap.h"
#include "boxes/sticker_set_box.h"
#include "data/data_cloud_file.h"
#include "data/data_document.h"
#include "data/data_document_media.h"
#include "data/data_file_origin.h"
#include "data/data_location.h"
#include "data/data_photo.h"
#include "data/data_photo_media.h"
#include "data/data_poll.h"
#include "data/data_session.h"
#include "data/data_statistics_chart.h"
#include "data/stickers/data_stickers.h"
#include "editor/editor_layer_widget.h"
#include "editor/photo_editor.h"
#include "history/history_location_manager.h"
#include "history/view/history_view_group_call_bar.h"
#include "history/view/history_view_reaction_preview.h"
#include "history/view/media/history_view_document.h"
#include "lang/lang_keys.h"
#include "layout/layout_document_generic_preview.h"
#include "lottie/lottie_icon.h"
#include "main/main_session.h"
#include "poll/poll_media_upload.h"
#include "statistics/chart_widget.h"
#include "mainwidget.h"
#include "settings/settings_common.h"
#include "storage/localimageloader.h"
#include "storage/storage_media_prepare.h"
#include "chat_helpers/tabbed_panel.h"
#include "chat_helpers/tabbed_selector.h"
#include "ui/chat/attach/attach_prepare.h"
#include "ui/layers/generic_box.h"
#include "ui/painter.h"
#include "ui/rect.h"
#include "ui/vertical_list.h"
#include "ui/text/format_song_name.h"
#include "ui/text/format_values.h"
#include "ui/widgets/dropdown_menu.h"
#include "ui/widgets/labels.h"
#include "ui/widgets/menu/menu_action.h"
#include "ui/widgets/shadow.h"
#include "ui/wrap/slide_wrap.h"
#include "window/window_session_controller.h"
#include "styles/style_boxes.h"
#include "styles/style_chat.h"
#include "styles/style_chat_helpers.h"
#include "styles/style_layers.h"
#include "styles/style_media_view.h"
#include "styles/style_menu_icons.h"
#include "styles/style_settings.h"
#include "styles/style_statistics.h"
#include "styles/style_widgets.h"

namespace HistoryView {
namespace {

class VotersItem final : public Ui::Menu::ItemBase {
public:
	VotersItem(
		not_null<Ui::Menu::Menu*> parent,
		const style::Menu &st,
		int votes,
		const std::vector<not_null<PeerData*>> &recentVoters);

	not_null<QAction*> action() const override;
	bool isEnabled() const override;

protected:
	int contentHeight() const override;

private:
	void paint(Painter &p);

	const not_null<QAction*> _dummyAction;
	const style::Menu &_st;
	const int _votes = 0;
	const int _height = 0;
	QImage _userpics;
	int _userpicsWidth = 0;
};

VotersItem::VotersItem(
	not_null<Ui::Menu::Menu*> parent,
	const style::Menu &st,
	int votes,
	const std::vector<not_null<PeerData*>> &recentVoters)
: ItemBase(parent, st)
, _dummyAction(Ui::CreateChild<QAction>(parent.get()))
, _st(st)
, _votes(votes)
, _height(st.itemPadding.top()
	+ st.itemStyle.font->height
	+ st.itemPadding.bottom()) {
	auto prepared = PrepareUserpicsInRow(
		recentVoters,
		st::historyCommentsUserpics);
	_userpics = std::move(prepared.image);
	_userpicsWidth = prepared.width;

	const auto votesText = tr::lng_polls_votes_count(
		tr::now,
		lt_count,
		_votes);
	const auto spacing = _userpicsWidth > 0
		? st::normalFont->spacew * 2
		: 0;
	const auto textWidth = st.itemStyle.font->width(votesText);
	const auto &textPadding = st::defaultMenu.itemPadding;
	const auto minWidth = textPadding.left()
		+ textWidth
		+ spacing
		+ _userpicsWidth
		+ st.itemPadding.right();
	setMinWidth(minWidth);

	paintRequest() | rpl::on_next([=] {
		Painter p(this);
		paint(p);
	}, lifetime());

	fitToMenuWidth();
}

not_null<QAction*> VotersItem::action() const {
	return _dummyAction;
}

bool VotersItem::isEnabled() const {
	return false;
}

int VotersItem::contentHeight() const {
	return _height;
}

void VotersItem::paint(Painter &p) {
	p.fillRect(0, 0, width(), _height, _st.itemBg);

	const auto votesText = tr::lng_polls_votes_count(
		tr::now,
		lt_count,
		_votes);

	p.setPen(_st.itemFg);
	p.setFont(_st.itemStyle.font);
	const auto &textPadding = st::defaultMenu.itemPadding;
	const auto textY = _st.itemPadding.top()
		+ _st.itemStyle.font->ascent;
	p.drawText(textPadding.left(), textY, votesText);

	if (!_userpics.isNull()) {
		const auto userpicsHeight
			= _userpics.height() / style::DevicePixelRatio();
		const auto x = width() - _st.itemPadding.right() - _userpicsWidth;
		const auto y = (_height - userpicsHeight) / 2;
		p.drawImage(x, y, _userpics);
	}
}

} // namespace

void FillPollAnswerMenu(
		not_null<Ui::DropdownMenu*> menu,
		not_null<PollData*> poll,
		const QByteArray &option,
		not_null<DocumentData*> document,
		FullMsgId itemId,
		not_null<Window::SessionController*> controller) {
	const auto session = &controller->session();
	auto addedVotersItem = false;
	if (const auto answer = poll->answerByOption(option)) {
		const auto canVote = !option.isEmpty()
			&& !poll->closed()
			&& !poll->quiz()
			&& !poll->voted();
		if (!canVote && answer->votes > 0) {
			menu->addAction(
				base::make_unique_q<VotersItem>(
					menu->menu(),
					menu->menu()->st(),
					answer->votes,
					answer->recentVoters));
			menu->addSeparator(&st::expandedMenuSeparator);
			addedVotersItem = true;
		}
	}
	if (!option.isEmpty()
		&& !poll->closed()
		&& !poll->quiz()) {
		if (poll->voted()
			&& !poll->revotingDisabled()) {
			menu->addAction(
				tr::lng_polls_retract(tr::now),
				[=] {
					session->api().polls().sendVotes(
						itemId,
						{});
				},
				&st::menuIconRetractVote);
		} else if (!poll->voted()
			&& poll->sendingVotes.empty()) {
			menu->addAction(
				tr::lng_polls_submit_votes(tr::now),
				[=] {
					session->api().polls().sendVotes(
						itemId,
						{ option });
				},
				&st::menuIconSelect);
			if (!addedVotersItem) {
				menu->addSeparator(
					&st::expandedMenuSeparator);
			}
		}
	}
	const auto show = controller->uiShow();
	const auto isFaved
		= document->owner().stickers().isFaved(document);
	menu->addAction(
		(isFaved
			? tr::lng_faved_stickers_remove
			: tr::lng_faved_stickers_add)(tr::now),
		[=] {
			Api::ToggleFavedSticker(
				show,
				document,
				Data::FileOriginStickerSet(
					Data::Stickers::FavedSetId,
					0));
		},
		isFaved
			? &st::menuIconUnfave
			: &st::menuIconFave);
	if (const auto sticker = document->sticker()) {
		const auto setId = sticker->set;
		if (setId.id) {
			menu->addAction(
				tr::lng_view_button_stickerset(tr::now),
				[=] {
					show->show(Box<StickerSetBox>(
						show,
						setId,
						Data::StickersType::Stickers));
				},
				&st::menuIconStickers);
		}
	}
}

void ShowPollStatsBox(
		not_null<Window::SessionController*> controller,
		FullMsgId itemId) {
	controller->show(Box([=](not_null<Ui::GenericBox*> box) {
		box->setTitle(tr::lng_polls_stats_title());
		box->setWidth(st::boxWideWidth);

		const auto content = box->addRow(
			object_ptr<Ui::VerticalLayout>(box),
			Margins(0));
		const auto loadingWrap = content->add(
			object_ptr<Ui::SlideWrap<Ui::VerticalLayout>>(
				content,
				object_ptr<Ui::VerticalLayout>(content)));
		loadingWrap->toggle(true, anim::type::instant);
		const auto loading = loadingWrap->entity();
		const auto resultWrap = content->add(
			object_ptr<Ui::SlideWrap<Ui::VerticalLayout>>(
				content,
				object_ptr<Ui::VerticalLayout>(content)));
		resultWrap->toggle(false, anim::type::instant);
		const auto result = resultWrap->entity();

		auto icon = ::Settings::CreateLottieIcon(
			loading,
			{ .name = u"stats"_q, .sizeOverride = st::normalBoxLottieSize },
			st::settingsBlockedListIconPadding);
		loading->add(std::move(icon.widget));
		auto startAnimation = std::move(icon.animate);
		box->showFinishes(
		) | rpl::take(1) | rpl::on_next([=]() mutable {
			startAnimation(anim::repeat::loop);
		}, loading->lifetime());
		loading->add(
			object_ptr<Ui::FlatLabel>(
				loading,
				tr::lng_stats_loading(),
				st::changePhoneTitle),
			st::changePhoneTitlePadding + st::boxRowPadding,
			style::al_top);
		loading->add(
			object_ptr<Ui::FlatLabel>(
				loading,
				tr::lng_stats_loading_subtext(),
				st::statisticsLoadingSubtext),
			st::changePhoneDescriptionPadding + st::boxRowPadding,
			style::al_top
		)->setTryMakeSimilarLines(true);
		Ui::AddSkip(loading, st::settingsBlockedListIconPadding.top());
		const auto finishLoading = [=] {
			loading->clear();
			loadingWrap->toggle(false, anim::type::instant);
			resultWrap->toggle(true, anim::type::instant);
		};
		const auto showError = [=](QString error) {
			finishLoading();
			result->clear();
			result->add(
				object_ptr<Ui::FlatLabel>(
					result,
					error.isEmpty()
						? tr::lng_polls_votes_none(tr::now)
						: std::move(error),
					st::defaultFlatLabel),
				st::boxRowPadding + st::statisticsLayerMargins,
				style::al_center);
		};

		controller->session().api().polls().requestStats(
			itemId,
			crl::guard(box, [=](Data::StatisticalGraph graph) {
				if (graph.chart) {
					finishLoading();
					result->clear();
					const auto chart = result->add(
						object_ptr<Statistic::ChartWidget>(result),
						st::statisticsLayerMargins);
					chart->setChartData(
						std::move(graph.chart),
						Statistic::ChartViewType::Linear);
					chart->setTitle(tr::lng_notification_reactions_poll_votes());
					Statistic::FixCacheForHighDPIChartWidget(result);
				} else {
					showError(!graph.error.isEmpty()
						? graph.error
						: tr::lng_polls_votes_none(tr::now));
				}
			}),
			crl::guard(box, [=](QString error) {
				showError(std::move(error));
			}));

		box->addButton(tr::lng_box_ok(), [=] {
			box->closeBox();
		});
	}));
}

namespace {

void AddRemoveAction(
		not_null<Ui::DropdownMenu*> menu,
		Fn<void()> remove) {
	menu->addAction(
		base::make_unique_q<Ui::Menu::Action>(
			menu->menu(),
			st::menuWithIconsAttention,
			Ui::Menu::CreateAction(
				menu->menu().get(),
				tr::lng_box_remove(tr::now),
				std::move(remove)),
			&st::menuIconDeleteAttention,
			&st::menuIconDeleteAttention));
}

} // namespace

void ShowPollStickerPreview(
		not_null<Window::SessionController*> controller,
		not_null<DocumentData*> document,
		Fn<void()> replace,
		Fn<void()> remove) {
	ShowStickerPreview(controller, FullMsgId(), document, [=](
			not_null<Ui::DropdownMenu*> menu) {
		menu->addAction(
			tr::lng_attach_replace(tr::now),
			replace,
			&st::menuIconReplace);
		AddRemoveAction(menu, remove);
	});
}

void ShowPollPhotoPreview(
		not_null<Window::SessionController*> controller,
		not_null<PhotoData*> photo,
		Fn<void()> replace,
		Fn<void()> edit,
		Fn<void()> remove) {
	ShowPhotoPreview(controller, FullMsgId(), photo, [=](
			not_null<Ui::DropdownMenu*> menu) {
		menu->addAction(
			tr::lng_attach_replace(tr::now),
			replace,
			&st::menuIconReplace);
		menu->addAction(
			tr::lng_context_draw(tr::now),
			edit,
			&st::menuIconDraw);
		AddRemoveAction(menu, remove);
	});
}

void ShowPollDocumentPreview(
		not_null<Window::SessionController*> controller,
		not_null<DocumentData*> document,
		Fn<void()> replace,
		Fn<void()> remove) {
	const auto isSong = document->isSong();
	const auto songData = isSong ? document->song() : nullptr;
	const auto media = isSong
		? document->createMediaView()
		: nullptr;
	if (media) {
		media->thumbnailWanted(Data::FileOrigin());
	}

	const auto docGeneric = Layout::DocumentGenericPreview::Create(
		document);
	const auto docIcon = docGeneric.icon();
	const auto docIconColor = docGeneric.color;
	const auto docExt = [&] {
		auto ext = docGeneric.ext;
		const auto maxW = st::mediaviewFileIconSize
			- st::mediaviewFileExtPadding * 2;
		if (st::mediaviewFileExtFont->width(ext) > maxW) {
			ext = st::mediaviewFileExtFont->elided(ext, maxW, Qt::ElideMiddle);
		}
		return ext;
	}();

	const auto maxTextW = st::mediaviewFileSize.width()
		- st::mediaviewFileIconSize
		- st::mediaviewFilePadding * 3;
	const auto docName = [&] {
		auto name = (isSong && songData)
			? Ui::Text::FormatSongName(
				document->filename(),
				songData->title,
				songData->performer).string()
			: document->filename().isEmpty()
			? tr::lng_mediaview_doc_image(tr::now)
			: document->filename();
		if (st::mediaviewFileNameFont->width(name) > maxTextW) {
			name = st::mediaviewFileNameFont->elided(
				name,
				maxTextW,
				Qt::ElideMiddle);
		}
		return name;
	}();
	const auto docSize = [&] {
		auto text = Ui::FormatSizeText(document->size);
		if (st::mediaviewFont->width(text) > maxTextW) {
			text = st::mediaviewFont->elided(text, maxTextW);
		}
		return text;
	}();

	ShowWidgetPreview(controller, [=](not_null<Ui::RpWidget*> preview) {
		const auto shadowExtend = st::boxRoundShadow.extend;
		const auto fullW = st::mediaviewFileSize.width()
			+ rect::m::sum::h(shadowExtend);
		const auto fullH = st::mediaviewFileSize.height()
			+ rect::m::sum::v(shadowExtend);
		preview->resize(fullW, fullH);

		if (media) {
			document->session().downloaderTaskFinished(
			) | rpl::on_next([=] {
				preview->update();
			}, preview->lifetime());
		}

		preview->paintRequest() | rpl::on_next([=] {
			auto p = Painter(preview);
			const auto outer = preview->rect() - shadowExtend;

			Ui::Shadow::paint(
				p,
				outer,
				preview->width(),
				st::boxRoundShadow);
			{
				auto hq = PainterHighQualityEnabler(p);
				p.setPen(Qt::NoPen);
				p.setBrush(st::windowBg);
				p.drawRoundedRect(outer, st::boxRadius, st::boxRadius);
			}

			const auto padding = st::mediaviewFilePadding;
			const auto iconSize = st::mediaviewFileIconSize;
			const auto iconRect = QRect(
				outer.x() + padding,
				outer.y() + padding,
				iconSize,
				iconSize);

			const auto coverDrawn = isSong
				&& DrawThumbnailAsSongCover(
					p,
					st::songCoverOverlayFg,
					media,
					iconRect);
			if (!coverDrawn) {
				p.setPen(Qt::NoPen);
				p.fillRect(iconRect, docIconColor);
				if (docIcon) {
					docIcon->paint(
						p,
						iconRect.x()
							+ (iconRect.width() - docIcon->width()),
						iconRect.y(),
						preview->width());
				}
				if (!docExt.isEmpty()) {
					p.setPen(st::activeButtonFg);
					p.setFont(st::mediaviewFileExtFont);
					const auto extW
						= st::mediaviewFileExtFont->width(docExt);
					p.drawText(
						iconRect.x()
							+ (iconRect.width() - extW) / 2,
						iconRect.y()
							+ st::mediaviewFileExtTop
							+ st::mediaviewFileExtFont->ascent,
						docExt);
				}
			}
			if (isSong && !coverDrawn) {
				auto hq = PainterHighQualityEnabler(p);
				p.setPen(Qt::NoPen);
				p.setBrush(st::msgFileInBg);
				p.drawEllipse(iconRect);
				st::historyFileInPlay.paintInCenter(p, iconRect);
			}

			const auto textX = outer.x() + 2 * padding + iconSize;
			p.setPen(st::windowFg);
			p.setFont(st::mediaviewFileNameFont);
			p.drawText(
				textX,
				outer.y() + padding
					+ st::mediaviewFileNameTop
					+ st::mediaviewFileNameFont->ascent,
				docName);

			p.setPen(st::windowSubTextFg);
			p.setFont(st::mediaviewFont);
			p.drawText(
				textX,
				outer.y() + padding
					+ st::mediaviewFileSizeTop
					+ st::mediaviewFont->ascent,
				docSize);
		}, preview->lifetime());
	}, [=](not_null<Ui::DropdownMenu*> menu) {
		menu->addAction(
			tr::lng_attach_replace(tr::now),
			replace,
			&st::menuIconReplace);
		AddRemoveAction(menu, remove);
	});
}

void ShowPollGeoPreview(
		not_null<Window::SessionController*> controller,
		Data::LocationPoint point,
		Fn<void()> replace,
		Fn<void()> remove) {
	const auto session = &controller->session();
	const auto cloudImage = session->data().location(point);
	const auto view = cloudImage->createView();
	cloudImage->load(session, Data::FileOrigin());

	ShowWidgetPreview(controller, [=](
			not_null<Ui::RpWidget*> preview) {
		const auto body = preview->parentWidget()->size();
		const auto skip = st::mediaPreviewPhotoSkip;
		const auto maxSide = std::min(
			body.width() - 2 * skip,
			body.height() - 2 * skip);
		const auto side = std::min(maxSide, st::locationSize.width());
		const auto scaled = QSize(
			side,
			(side * st::locationSize.height()
				/ st::locationSize.width()));
		const auto shadowExtend = st::boxRoundShadow.extend;
		const auto fullW = scaled.width()
			+ rect::m::sum::h(shadowExtend);
		const auto fullH = scaled.height()
			+ rect::m::sum::v(shadowExtend);
		preview->resize(fullW, fullH);

		session->downloaderTaskFinished(
		) | rpl::on_next([=] {
			preview->update();
		}, preview->lifetime());

		preview->paintRequest() | rpl::on_next([=] {
			auto p = Painter(preview);
			const auto outer = preview->rect() - shadowExtend;

			Ui::Shadow::paint(
				p,
				outer,
				preview->width(),
				st::boxRoundShadow);
			auto hq = PainterHighQualityEnabler(p);
			p.setPen(Qt::NoPen);
			p.setBrush(st::windowBg);
			const auto radius = st::boxRadius;
			p.drawRoundedRect(outer, radius, radius);

			if (!view->isNull()) {
				auto path = QPainterPath();
				path.addRoundedRect(outer, radius, radius);
				p.setClipPath(path);
				p.drawImage(outer, view->scaled(
					outer.size() * style::DevicePixelRatio(),
					Qt::KeepAspectRatioByExpanding,
					Qt::SmoothTransformation));
				p.setClipping(false);
				const auto paintMarker = [&](const style::icon &icon) {
					icon.paint(
						p,
						outer.x()
							+ (outer.width() - icon.width()) / 2,
						outer.y()
							+ (outer.height() / 2) - icon.height(),
						preview->width());
				};
				paintMarker(st::historyMapPoint);
				paintMarker(st::historyMapPointInner);
			}
		}, preview->lifetime());
	}, [=](not_null<Ui::DropdownMenu*> menu) {
		menu->addAction(
			tr::lng_open_link(tr::now),
			[=] { LocationClickHandler(point).onClick({}); },
			&st::menuIconAddress);
		if (replace) {
			menu->addAction(
				tr::lng_attach_replace(tr::now),
				replace,
				&st::menuIconReplace);
		}
		if (remove) {
			AddRemoveAction(menu, remove);
		}
	});
}

void EditPollPhoto(
		not_null<Window::SessionController*> controller,
		not_null<PhotoData*> photo,
		Fn<void(Ui::PreparedList)> done) {
	const auto photoMedia = photo->createMediaView();
	const auto large = photoMedia->image(Data::PhotoSize::Large);
	if (!large) {
		return;
	}
	const auto previewWidth = st::sendMediaPreviewSize;
	const auto fileImage = std::make_shared<Image>(*large);
	auto callback = [=](const Editor::PhotoModifications &mods) {
		if (!mods) {
			return;
		}
		const auto large = photoMedia->image(Data::PhotoSize::Large);
		if (!large) {
			return;
		}
		auto copy = large->original();
		auto list = Storage::PrepareMediaFromImage(
			std::move(copy),
			QByteArray(),
			previewWidth);

		using ImageInfo = Ui::PreparedFileInformation::Image;
		auto &file = list.files.front();
		const auto image = std::get_if<ImageInfo>(
			&file.information->media);
		image->modifications = mods;
		Storage::UpdateImageDetails(
			file,
			previewWidth,
			PhotoSideLimit(true));
		Storage::ApplyModifications(list);
		done(std::move(list));
	};
	const auto parent = controller->content().get();
	auto editor = base::make_unique_q<Editor::PhotoEditor>(
		parent,
		&controller->window(),
		fileImage,
		Editor::PhotoModifications());
	const auto raw = editor.get();
	auto layer = std::make_unique<Editor::LayerWidget>(
		parent,
		std::move(editor));
	Editor::InitEditorLayer(layer.get(), raw, std::move(callback));
	controller->showLayer(
		std::move(layer),
		Ui::LayerOption::KeepOther);
}

bool ShowPollMediaPreview(
		not_null<Window::SessionController*> controller,
		const std::shared_ptr<PollMediaUpload::PollMediaState> &media,
		PollMediaActions actions) {
	if (media->uploading) {
		actions.remove();
		return true;
	}
	const auto document = media->media.document;
	const auto photo = media->media.photo;
	if (document && document->sticker()) {
		ShowPollStickerPreview(
			controller,
			document,
			std::move(actions.chooseSticker),
			std::move(actions.remove));
		return true;
	} else if (photo) {
		ShowPollPhotoPreview(
			controller,
			photo,
			std::move(actions.choosePhotoOrVideo),
			[controller, photo, editPhoto = std::move(actions.editPhoto)] {
				EditPollPhoto(
					controller,
					photo,
					std::move(editPhoto));
			},
			std::move(actions.remove));
		return true;
	} else if (document && document->isVideoFile()) {
		ShowPollDocumentPreview(
			controller,
			document,
			std::move(actions.choosePhotoOrVideo),
			std::move(actions.remove));
		return true;
	} else if (document) {
		ShowPollDocumentPreview(
			controller,
			document,
			std::move(actions.chooseDocument),
			std::move(actions.remove));
		return true;
	} else if (media->media.geo) {
		ShowPollGeoPreview(
			controller,
			*media->media.geo,
			nullptr,
			std::move(actions.remove));
		return true;
	}
	return false;
}

base::unique_qptr<ChatHelpers::TabbedPanel> CreatePollStickerPanel(
		not_null<QWidget*> parent,
		not_null<Window::SessionController*> controller) {
	using Selector = ChatHelpers::TabbedSelector;
	using Descriptor = ChatHelpers::TabbedSelectorDescriptor;

	auto panel = base::make_unique_q<ChatHelpers::TabbedPanel>(
		parent,
		controller,
		object_ptr<Selector>(
			nullptr,
			Descriptor{
				.show = controller->uiShow(),
				.st = st::backgroundEmojiPan,
				.level = Window::GifPauseReason::Layer,
				.mode = Selector::Mode::StickersOnly,
				.features = {
					.megagroupSet = false,
					.stickersSettings = false,
					.openStickerSets = false,
				},
			}));
	panel->setDesiredHeightValues(
		0.,
		st::emojiPanMinHeight,
		st::emojiPanMinHeight);
	panel->hide();
	return panel;
}

} // namespace HistoryView
