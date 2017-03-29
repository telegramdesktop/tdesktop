/*
This file is part of Telegram Desktop,
the official desktop version of Telegram messaging app, see https://telegram.org

Telegram Desktop is free software: you can redistribute it and/or modify
it under the terms of the GNU General Public License as published by
the Free Software Foundation, either version 3 of the License, or
(at your option) any later version.

It is distributed in the hope that it will be useful,
but WITHOUT ANY WARRANTY; without even the implied warranty of
MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE. See the
GNU General Public License for more details.

In addition, as a special exception, the copyright holders give permission
to link the code of portions of this program with the OpenSSL library.

Full license: https://github.com/telegramdesktop/tdesktop/blob/master/LICENSE
Copyright (c) 2014-2017 John Preston, https://desktop.telegram.org
*/
#pragma once

#include "stickers/emoji_panel.h"

namespace ChatHelpers {

class StickersListWidget : public EmojiPanel::Inner, private base::Subscriber {
	Q_OBJECT

public:
	StickersListWidget(QWidget *parent);

	void refreshRecent() override;
	void preloadImages() override;
	void hideFinish(bool completely) override;
	void clearSelection() override;
	object_ptr<TWidget> createController() override;

	void showStickerSet(uint64 setId);

	void refreshStickers();
	void refreshRecentStickers(bool resize = true);

	void fillIcons(QList<StickerIcon> &icons);

	void setVisibleTopBottom(int visibleTop, int visibleBottom) override;

	uint64 currentSet(int yOffset) const;

	void installedLocally(uint64 setId);
	void notInstalledLocally(uint64 setId);
	void clearInstalledLocally();

protected:
	void mousePressEvent(QMouseEvent *e) override;
	void mouseReleaseEvent(QMouseEvent *e) override;
	void mouseMoveEvent(QMouseEvent *e) override;
	void resizeEvent(QResizeEvent *e) override;
	void paintEvent(QPaintEvent *e) override;
	void leaveEventHook(QEvent *e) override;
	void leaveToChildEvent(QEvent *e, QWidget *child) override;
	void enterFromChildEvent(QEvent *e, QWidget *child) override;
	int countHeight() override;

private slots:
	void onSettings();
	void onPreview();

signals:
	void selected(DocumentData *sticker);

	void displaySet(quint64 setId);
	void installSet(quint64 setId);
	void removeSet(quint64 setId);

	void refreshIcons(bool scrollAnimation);
	void scrollUpdated();

private:
	enum class Section {
		Featured,
		Stickers,
	};
	class Controller;

	static constexpr auto kRefreshIconsScrollAnimation = true;
	static constexpr auto kRefreshIconsNoAnimation = false;

	struct SectionInfo {
		int section = 0;
		int count = 0;
		int top = 0;
		int rowsCount = 0;
		int rowsTop = 0;
		int rowsBottom = 0;
	};
	template <typename Callback>
	bool enumerateSections(Callback callback) const;
	SectionInfo sectionInfo(int section) const;
	SectionInfo sectionInfoByOffset(int yOffset) const;

	void updateSelected();
	void setSelected(int newSelected, int newSelectedFeaturedSet, int newSelectedFeaturedSetAdd);

	void setPressedFeaturedSetAdd(int newPressedFeaturedSetAdd);

	struct Set {
		Set(uint64 id, MTPDstickerSet::Flags flags, const QString &title, int32 hoversSize, const StickerPack &pack = StickerPack()) : id(id), flags(flags), title(title), pack(pack) {
		}
		uint64 id;
		MTPDstickerSet::Flags flags;
		QString title;
		StickerPack pack;
		QSharedPointer<Ui::RippleAnimation> ripple;
	};
	using Sets = QList<Set>;
	Sets &shownSets() {
		return (_section == Section::Featured) ? _featuredSets : _mySets;
	}
	const Sets &shownSets() const {
		return (_section == Section::Featured) ? _featuredSets : _mySets;
	}
	int featuredRowHeight() const;
	void readVisibleSets();

	void paintFeaturedStickers(Painter &p, QRect clip);
	void paintStickers(Painter &p, QRect clip);
	void paintSticker(Painter &p, Set &set, int y, int index, bool selected, bool deleteSelected);
	bool featuredHasAddButton(int index) const;
	int featuredContentWidth() const;
	QRect featuredAddRect(int y) const;

	enum class AppendSkip {
		Archived,
		Installed,
	};
	void appendSet(Sets &to, uint64 setId, AppendSkip skip);

	void selectEmoji(EmojiPtr emoji);
	int stickersLeft() const;
	QRect stickerRect(int section, int sel);

	Sets _mySets;
	Sets _featuredSets;
	OrderedSet<uint64> _installedLocallySets;
	QList<bool> _custom;

	Section _section = Section::Stickers;

	void removeRecentSticker(int section, int index);

	int _selected = -1;
	int _pressed = -1;
	int _selectedFeaturedSet = -1;
	int _pressedFeaturedSet = -1;
	int _selectedFeaturedSetAdd = -1;
	int _pressedFeaturedSetAdd = -1;
	QPoint _lastMousePos;

	QString _addText;
	int _addWidth;

	object_ptr<Ui::LinkButton> _settings;

	QTimer _previewTimer;
	bool _previewShown = false;

};

} // namespace ChatHelpers
