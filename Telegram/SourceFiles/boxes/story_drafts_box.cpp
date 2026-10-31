/*
This file is part of Telegram Desktop,
the official desktop application for the Telegram messaging service.

For license and copyright information please follow this link:
https://github.com/telegramdesktop/tdesktop/blob/master/LEGAL
*/
#include "boxes/story_drafts_box.h"

#include "base/unixtime.h"
#include "data/data_peer.h"
#include "data/data_session.h"
#include "data/data_stories.h"
#include "data/data_story_drafts.h"
#include "lang/lang_keys.h"
#include "main/main_session.h"
#include "media/stories/media_stories_post.h"
#include "ui/image/image_prepare.h"
#include "ui/layers/generic_box.h"
#include "ui/widgets/labels.h"
#include "ui/painter.h"
#include "ui/text/text.h"
#include "ui/ui_utility.h"
#include "ui/widgets/buttons.h"
#include "ui/wrap/vertical_layout.h"
#include "window/window_session_controller.h"
#include "styles/style_boxes.h"
#include "styles/style_chat_helpers.h"
#include "styles/style_layers.h"

namespace {

class DraftRow final : public Ui::RippleButton {
public:
	DraftRow(
		not_null<QWidget*> parent,
		const Data::StoryDraft &draft,
		const QString &peerName);

	[[nodiscard]] rpl::producer<> removeRequests() const;

private:
	void paintEvent(QPaintEvent *e) override;
	int resizeGetHeight(int newWidth) override;

	[[nodiscard]] static QPixmap PrepareThumbnail(const QImage &image);

	const QPixmap _thumbnail;
	const base::unique_qptr<Ui::IconButton> _remove;
	Ui::Text::String _title;
	Ui::Text::String _subtitle;

};

DraftRow::DraftRow(
	not_null<QWidget*> parent,
	const Data::StoryDraft &draft,
	const QString &peerName)
: RippleButton(parent, st::defaultRippleAnimation)
, _thumbnail(PrepareThumbnail(draft.thumbnail))
, _remove(base::make_unique_q<Ui::IconButton>(this, st::stickersRemove))
, _title(
	st::storyRowTitleStyle,
	(draft.caption.text.isEmpty()
		? (draft.video
			? tr::lng_story_draft_video(tr::now)
			: tr::lng_story_draft_photo(tr::now))
		: draft.caption.text))
, _subtitle(
	st::storyRowSubtitleStyle,
	(peerName.isEmpty()
		? langDateTime(base::unixtime::parse(draft.date))
		: langDateTime(base::unixtime::parse(draft.date))
		+ u" \u00B7 "_q
		+ peerName)) {
	_remove->show();
}

rpl::producer<> DraftRow::removeRequests() const {
	return _remove->clicks() | rpl::to_empty;
}

QPixmap DraftRow::PrepareThumbnail(const QImage &image) {
	const auto side = st::storyRowIconSize;
	const auto ratio = style::DevicePixelRatio();
	if (image.isNull()) {
		return QPixmap();
	}
	auto scaled = image.scaled(
		side * ratio,
		side * ratio,
		Qt::KeepAspectRatioByExpanding,
		Qt::SmoothTransformation);
	const auto crop = QRect(
		(scaled.width() - side * ratio) / 2,
		(scaled.height() - side * ratio) / 2,
		side * ratio,
		side * ratio);
	auto result = Images::Round(
		scaled.copy(crop),
		ImageRoundRadius::Small);
	result.setDevicePixelRatio(ratio);
	return Ui::PixmapFromImage(std::move(result));
}

int DraftRow::resizeGetHeight(int newWidth) {
	const auto height = st::storyRowSendAsHeight;
	_remove->moveToRight(
		st::storyRowTextRight - st::stickersRemoveSkip,
		(height - _remove->height()) / 2,
		newWidth);
	return height;
}

void DraftRow::paintEvent(QPaintEvent *e) {
	auto p = Painter(this);
	paintRipple(p, 0, 0);

	const auto left = st::storyRowIconLeftNoRadio;
	const auto side = st::storyRowIconSize;
	const auto top = (height() - side) / 2;
	if (!_thumbnail.isNull()) {
		p.drawPixmap(left, top, _thumbnail);
	} else {
		p.setPen(Qt::NoPen);
		p.setBrush(st::windowBgOver);
		p.drawRoundedRect(
			QRect(left, top, side, side),
			st::roundRadiusSmall,
			st::roundRadiusSmall);
	}

	const auto textLeft = st::storyRowTextLeftNoRadio;
	const auto textRight = _remove->width() + st::storyRowTextRight;
	const auto available = width() - textLeft - textRight;
	p.setPen(st::windowFg);
	_title.drawLeftElided(
		p,
		textLeft,
		st::storyRowTitleTop,
		available,
		width());
	p.setPen(st::windowSubTextFg);
	_subtitle.drawLeftElided(
		p,
		textLeft,
		st::storyRowSubtitleTop,
		available,
		width());
}

void StoryDraftsBox(
		not_null<Ui::GenericBox*> box,
		not_null<Window::SessionController*> controller) {
	box->setTitle(tr::lng_story_drafts_title());
	box->setWidth(st::boxWideWidth);

	const auto drafts = &controller->session().data().stories().drafts();
	const auto self = controller->session().userPeerId();
	const auto content = box->addRow(
		object_ptr<Ui::VerticalLayout>(box),
		QMargins());
	const auto hadDrafts = box->lifetime().make_state<bool>(true);
	const auto rebuild = [=] {
		content->clear();
		auto &list = drafts->list();
		for (const auto &draft : list) {
			const auto id = draft.id;
			const auto peerName = (draft.peer && draft.peer != self)
				? controller->session().data().peer(draft.peer)->name()
				: QString();
			const auto row = content->add(
				object_ptr<DraftRow>(content, draft, peerName));
			row->setClickedCallback([=] {
				box->closeBox();
				Media::Stories::PostStoryDraft(controller, id);
			});
			row->removeRequests() | rpl::on_next([=] {
				drafts->remove(id);
			}, row->lifetime());
		}
		if (list.empty()) {
			if (*hadDrafts) {
				box->closeBox();
				return;
			}
			content->add(
				object_ptr<Ui::FlatLabel>(
					content,
					tr::lng_story_drafts_empty(),
					st::boxDividerLabel),
				st::boxRowPadding);
		}
		*hadDrafts = !list.empty();
	};
	*hadDrafts = !drafts->list().empty();
	rebuild();
	drafts->changes() | rpl::on_next([=] {
		crl::on_main(box, rebuild);
	}, box->lifetime());

	box->addButton(tr::lng_close(), [=] { box->closeBox(); });
}

} // namespace

void ShowStoryDraftsBox(not_null<Window::SessionController*> controller) {
	controller->show(Box(StoryDraftsBox, controller));
}
