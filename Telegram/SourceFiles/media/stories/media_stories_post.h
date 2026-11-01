/*
This file is part of Telegram Desktop,
the official desktop application for the Telegram messaging service.

For license and copyright information please follow this link:
https://github.com/telegramdesktop/tdesktop/blob/master/LEGAL
*/
#pragma once

#include "api/api_stories.h"
#include "data/data_story_edit_sources.h"
#include "editor/editor_link_pill.h"
#include "editor/photo_editor_common.h"

#include <QtCore/QJsonArray>

class QMimeData;

namespace ChatHelpers {
class Show;
} // namespace ChatHelpers

namespace Data {
class Story;
} // namespace Data

namespace Window {
class SessionController;
} // namespace Window

namespace Media::Stories {

using StoryMedia = Api::SendStoryMedia;

struct StoryEditorSetup {
	TextWithTags caption;
	int period = 0;
	bool editing = false;
	PeerId peer;
	uint64 draftId = 0;
	QJsonArray sceneItems;
	QStringList sceneFiles;
	Editor::AudioTrack storyAudio;
	bool storyAudioSelected = true;
	std::optional<Editor::LinkPreview> storyLink;
};

struct StoryEditorResult {
	StoryMedia media;
	TextWithTags caption;
	int period = 0;
	std::vector<Data::SuggestedReaction> reactions;
	std::vector<Data::UrlArea> urls;
	std::vector<Data::StoryLocation> locations;
	std::vector<Data::WeatherArea> weather;
	std::vector<Data::ChannelPost> channelPosts;
	bool mediaChanged = true;
	std::shared_ptr<Data::StorySourceSave> source;
};

using StoryEditorDone = Fn<void(
	StoryEditorResult result,
	Fn<void()> closeEditor)>;

void ChooseStoryMedia(
	not_null<Window::SessionController*> controller,
	not_null<PeerData*> peer,
	StoryEditorSetup setup,
	StoryEditorDone done);

struct CanSendStoryErrorContext {
	Fn<void(bool)> setLoading;
	Fn<void()> openBoosts;
};

struct WebAppStoryShare {
	QString mediaUrl;
	TextWithTags caption;
	QString widgetLinkUrl;
	QString widgetLinkName;
};

void PostStory(
	not_null<Window::SessionController*> controller,
	not_null<PeerData*> peer,
	CanSendStoryErrorContext ctx = {});
void PostStoryFromWebApp(
	not_null<Window::SessionController*> controller,
	WebAppStoryShare args);
void PostStoryWithFile(
	not_null<Window::SessionController*> controller,
	not_null<PeerData*> peer,
	const QString &path,
	StoryEditorSetup setup = {});
void PostStoryWithMimeData(
	not_null<Window::SessionController*> controller,
	not_null<PeerData*> peer,
	not_null<const QMimeData*> data);
void EditStory(
	not_null<Window::SessionController*> controller,
	not_null<Data::Story*> story);
void EditStoryPrivacy(
	std::shared_ptr<ChatHelpers::Show> show,
	not_null<Data::Story*> story);
void RepostStory(
	not_null<Window::SessionController*> controller,
	not_null<Data::Story*> story);
void PostStoryDraft(
	not_null<Window::SessionController*> controller,
	uint64 draftId);
void RepostMessageToStory(
	not_null<Window::SessionController*> controller,
	not_null<HistoryItem*> item);

[[nodiscard]] bool CanPostStory(not_null<PeerData*> peer);
[[nodiscard]] bool CanRepostStory(not_null<Data::Story*> story);
[[nodiscard]] bool CanRepostMessageToStory(not_null<HistoryItem*> item);
[[nodiscard]] bool CanEditStory(not_null<Data::Story*> story);
[[nodiscard]] bool CanEditStoryPrivacy(not_null<Data::Story*> story);

void ShowCanSendStoryError(
	not_null<Window::SessionController*> controller,
	not_null<PeerData*> peer,
	const Api::CanSendStory &result,
	CanSendStoryErrorContext ctx = {});

void SetupStoryPostToasts(not_null<Window::SessionController*> controller);

} // namespace Media::Stories
