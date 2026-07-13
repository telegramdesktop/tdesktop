/*
This file is part of Telegram Desktop,
the official desktop application for the Telegram messaging service.

For license and copyright information please follow this link:
https://github.com/telegramdesktop/tdesktop/blob/master/LEGAL
*/
#pragma once

#include "base/basic_types.h"
#include "iv/editor/iv_editor_state.h"
#include "ui/effects/animations.h"

#include <rpl/producer.h>

#include <QtCore/QPointer>
#include <QtCore/QString>
#include <QtGui/QImage>

#include <memory>
#include <optional>

class PeerData;
class QWidget;

namespace ChatHelpers {
class Show;
} // namespace ChatHelpers

namespace Main {
class Session;
} // namespace Main

namespace Ui {
struct PreparedList;
class RpWidget;
class IconButton;
} // namespace Ui

namespace Iv {
enum class RichMessageLimitError : unsigned char;
} // namespace Iv

namespace Iv::Editor {

class State;
class Widget;
struct PreparedMediaPasteTarget;

enum class ToolbarButtonState {
	Disabled,
	Inactive,
	Active,
};

void SetupToolbarButton(
	not_null<Ui::IconButton*> button,
	ToolbarButtonState state,
	anim::type animated = anim::type::normal);

struct ShowWindowDescriptor {
	enum class SubmitType {
		Send,
		Save,
	};

	not_null<Main::Session*> session;
	not_null<PeerData*> peer;
	std::shared_ptr<State> state;
	QString submitLabel;
	SubmitType submitType = SubmitType::Send;
	Fn<bool()> discarded;
	Fn<void(std::shared_ptr<ChatHelpers::Show>)> showCreated;
	Fn<void(not_null<Widget*>)> editorCreated;
	Fn<bool()> cancelled;
	Fn<bool()> changedCancelled;
	Fn<bool()> confirmed;
	Fn<void(not_null<Ui::RpWidget*>)> setupSubmitButton;
	Fn<void(
		not_null<Widget*>,
		QPointer<QWidget>,
		std::optional<State::ReplaceTarget>,
		RequestMediaType)> requestMedia;
	Fn<void(not_null<Widget*>, Ui::PreparedList, PreparedMediaPasteTarget)>
		applyPreparedMedia;
	Fn<void(uint64 /*photoId*/, Fn<void(QImage)>)> requestPhotoEditSource;
	Fn<void(not_null<Widget*>, Ui::PreparedList, State::ReplaceTarget)>
		replacePhotoWithList;
	Fn<MediaUploadState(uint64 /*mediaId*/)> mediaUploadState;
	Fn<void(not_null<Widget*>, uint64 /*mediaId*/)> cancelMediaUpload;
	Fn<void(not_null<Widget*>, State::BlockPath, QPointer<QWidget>)>
		addMediaAndGroupWithBlock;
	Fn<void(not_null<Widget*>, QPointer<QWidget>, rpl::producer<>)> requestMap;
	Fn<void()> closed;
	Fn<void(RichMessageLimitError)> showLimitToast;
};

class WindowHost final {
public:
	~WindowHost();
	void close();
	void activateClose();

private:
	friend std::unique_ptr<WindowHost> ShowWindow(
		ShowWindowDescriptor descriptor);

	explicit WindowHost(ShowWindowDescriptor descriptor);

	struct Impl;
	std::unique_ptr<Impl> _impl;

};

std::unique_ptr<WindowHost> ShowWindow(ShowWindowDescriptor descriptor);

} // namespace Iv::Editor
