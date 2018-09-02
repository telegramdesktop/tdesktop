/*
This file is part of Telegram Desktop,
the official desktop application for the Telegram messaging service.

For license and copyright information please follow this link:
https://github.com/telegramdesktop/tdesktop/blob/master/LEGAL
*/
#pragma once

#include "old_settings/settings_block_widget.h"
#include "ui/rp_widget.h"

namespace Ui {
class FlatLabel;
} // namespace Ui

namespace OldSettings {

class LabeledLink : public Ui::RpWidget {
public:
	enum class Type {
		Primary,
		Secondary,
	};
	LabeledLink(QWidget *parent, const QString &label, const QString &text, Type type, const char *slot);

	Ui::LinkButton *link() const;

	int naturalWidth() const override;

protected:
	int resizeGetHeight(int newWidth) override;

private:
	object_ptr<Ui::FlatLabel> _label;
	object_ptr<Ui::LinkButton> _link;

};

#ifndef OS_WIN_STORE
class DownloadPathState : public Ui::RpWidget, private base::Subscriber {
	Q_OBJECT

public:
	DownloadPathState(QWidget *parent);

protected:
	int resizeGetHeight(int newWidth) override;

	void paintEvent(QPaintEvent *e) override;

private slots:
	void onDownloadPath();
	void onClear();
	void onTempDirCleared(int task);
	void onTempDirClearFailed(int task);

private:
	QString downloadPathText() const;
	void updateControls();

	enum class State {
		Empty,
		Exists,
		Clearing,
		Cleared,
		ClearFailed,
	};
	State _state = State::Empty;

	object_ptr<LabeledLink> _path;
	object_ptr<Ui::LinkButton> _clear;

};
#endif // OS_WIN_STORE

class ChatSettingsWidget : public BlockWidget {
	Q_OBJECT

public:
	ChatSettingsWidget(QWidget *parent, UserData *self);

private slots:
	void onDontAskDownloadPath();
	void onAutomaticMediaDownloadSettings();
	void onManageStickerSets();

private:
	enum class SendByType {
		Enter,
		CtrlEnter,
	};
	void sendByChanged(SendByType value);
	void createControls();

	void toggleReplaceEmoji();
	void toggleSuggestEmoji();
	void toggleSuggestStickersByEmoji();

	Ui::Checkbox *_replaceEmoji = nullptr;
	Ui::Checkbox *_suggestEmoji = nullptr;
	Ui::Checkbox *_suggestByEmoji = nullptr;
	Ui::Checkbox *_dontAskDownloadPath = nullptr;

#ifndef OS_WIN_STORE
	Ui::SlideWrap<DownloadPathState> *_downloadPath = nullptr;
#endif // OS_WIN_STORE

	Ui::Radioenum<SendByType> *_sendByEnter = nullptr;
	Ui::Radioenum<SendByType> *_sendByCtrlEnter = nullptr;
	Ui::LinkButton *_automaticMediaDownloadSettings = nullptr;
	Ui::LinkButton *_manageStickerSets = nullptr;

};

} // namespace Settings
