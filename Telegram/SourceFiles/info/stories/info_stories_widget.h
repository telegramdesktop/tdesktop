/*
This file is part of Telegram Desktop,
the official desktop application for the Telegram messaging service.

For license and copyright information please follow this link:
https://github.com/telegramdesktop/tdesktop/blob/master/LEGAL
*/
#pragma once

#include "info/info_content_widget.h"
#include "info/media/info_media_widget.h"
#include "info/stories/info_stories_common.h"

namespace Ui {
template <typename Widget>
class SlideWrap;
} // namespace Ui

namespace Info::Stories {

class InnerWidget;

class Memento final : public ContentMemento {
public:
	Memento(not_null<Controller*> controller);
	Memento(not_null<PeerData*> peer, int albumId, int addingToAlbumId);
	~Memento();

	object_ptr<ContentWidget> createWidget(
		QWidget *parent,
		not_null<Controller*> controller,
		const QRect &geometry) override;

	Section section() const override;

	[[nodiscard]] Media::Memento &media() {
		return _media;
	}
	[[nodiscard]] const Media::Memento &media() const {
		return _media;
	}

private:
	Media::Memento _media;
	int _addingToAlbumId = 0;

};

class Widget final : public ContentWidget {
public:
	Widget(QWidget *parent, not_null<Controller*> controller);

	void setIsStackBottom(bool isStackBottom) override;

	bool showInternal(
		not_null<ContentMemento*> memento) override;

	void setInternalState(
		const QRect &geometry,
		not_null<Memento*> memento);

	rpl::producer<SelectedItems> selectedListValue() const override;
	void selectionAction(SelectionAction action) override;

	rpl::producer<QString> title() override;

	rpl::producer<bool> desiredBottomShadowVisibility() override;

	void showFinished() override;

private:
	void saveState(not_null<Memento*> memento);
	void restoreState(not_null<Memento*> memento);

	void setupBottomButton(int wasBottomHeight);
	void refreshBottom();

	std::shared_ptr<ContentMemento> doCreateMemento() override;

	rpl::variable<int> _albumId;
	InnerWidget *_inner = nullptr;
	QPointer<Ui::SlideWrap<Ui::RpWidget>> _pinnedToBottom;
	rpl::variable<bool> _hasPinnedToBottom;
	rpl::variable<bool> _emptyAlbumShown;
	bool _shown = false;

};

[[nodiscard]] std::shared_ptr<Info::Memento> Make(
	not_null<PeerData*> peer,
	int albumId = 0);

} // namespace Info::Stories
