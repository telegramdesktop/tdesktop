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

namespace Info::Saved {

class MusicInner;

class MusicMemento final : public ContentMemento {
public:
	MusicMemento(not_null<Controller*> controller);
	MusicMemento(not_null<PeerData*> peer);
	~MusicMemento();

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

class MusicWidget final : public ContentWidget {
public:
	MusicWidget(QWidget *parent, not_null<Controller*> controller);

	void setIsStackBottom(bool isStackBottom) override;

	bool showInternal(
		not_null<ContentMemento*> memento) override;

	void setInternalState(
		const QRect &geometry,
		not_null<MusicMemento*> memento);

	rpl::producer<SelectedItems> selectedListValue() const override;
	void selectionAction(SelectionAction action) override;

	rpl::producer<QString> title() override;

private:
	void saveState(not_null<MusicMemento*> memento);
	void restoreState(not_null<MusicMemento*> memento);

	std::shared_ptr<ContentMemento> doCreateMemento() override;

	MusicInner *_inner = nullptr;
	bool _shown = false;

};

[[nodiscard]] std::shared_ptr<Info::Memento> MakeMusic(
	not_null<PeerData*> peer);

} // namespace Info::Saved
