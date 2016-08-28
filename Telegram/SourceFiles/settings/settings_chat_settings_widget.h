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
Copyright (c) 2014-2016 John Preston, https://desktop.telegram.org
*/
#pragma once

#include "settings/settings_block_widget.h"

class FlatLabel;

namespace Settings {

class LabeledLink : public TWidget {
public:
	enum class Type {
		Primary,
		Secondary,
	};
	LabeledLink(QWidget *parent, const QString &label, const QString &text, Type type, const char *slot);

	void setLink(const QString &text);

	LinkButton *link() {
		return _link;
	}

	int naturalWidth() const override;

protected:
	int resizeGetHeight(int newWidth) override;

private:
	ChildWidget<FlatLabel> _label;
	ChildWidget<LinkButton> _link;

};

class DownloadPathState : public TWidget, public base::Subscriber {
	Q_OBJECT

public:
	DownloadPathState(QWidget *parent);

protected:
	int resizeGetHeight(int newWidth) override;

	void paintEvent(QPaintEvent *e) override;

private slots:
	void onDownloadPath();
	void onClear();
	void onClearSure();
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

	ChildWidget<LabeledLink> _path;
	ChildWidget<LinkButton> _clear;

};

class ChatSettingsWidget : public BlockWidget {
	Q_OBJECT

public:
	ChatSettingsWidget(QWidget *parent, UserData *self);

private slots:
	void onReplaceEmoji();
	void onViewList();
	void onDontAskDownloadPath();
	void onSendByEnter();
	void onSendByCtrlEnter();
	void onAutomaticMediaDownloadSettings();
	void onManageStickerSets();

private:
	void createControls();

	ChildWidget<Checkbox> _replaceEmoji = { nullptr };
	ChildWidget<Ui::WidgetSlideWrap<LinkButton>> _viewList = { nullptr };
	ChildWidget<Checkbox> _dontAskDownloadPath = { nullptr };
	ChildWidget<Ui::WidgetSlideWrap<DownloadPathState>> _downloadPath = { nullptr };
	ChildWidget<Radiobutton> _sendByEnter = { nullptr };
	ChildWidget<Radiobutton> _sendByCtrlEnter = { nullptr };
	ChildWidget<LinkButton> _automaticMediaDownloadSettings = { nullptr };
	ChildWidget<LinkButton> _manageStickerSets = { nullptr };

};

} // namespace Settings
