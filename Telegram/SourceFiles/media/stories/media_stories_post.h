/*
This file is part of Telegram Desktop,
the official desktop application for the Telegram messaging service.

For license and copyright information please follow this link:
https://github.com/telegramdesktop/tdesktop/blob/master/LEGAL
*/
#pragma once

#include "api/api_stories.h"

class QMimeData;

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
};

struct StoryEditorResult {
	StoryMedia media;
	TextWithTags caption;
	int period = 0;
	std::vector<Data::SuggestedReaction> reactions;
	std::vector<Data::UrlArea> urls;
	std::vector<Data::StoryLocation> locations;
	std::vector<Data::WeatherArea> weather;
	DocumentData *music = nullptr;
	bool mediaChanged = true;
};

using StoryEditorDone = Fn<void(
	StoryEditorResult result,
	Fn<void()> closeEditor)>;

void ChooseStoryMedia(
	not_null<Window::SessionController*> controller,
	not_null<PeerData*> peer,
	StoryEditorSetup setup,
	StoryEditorDone done);

void PostStory(
	not_null<Window::SessionController*> controller,
	not_null<PeerData*> peer);
void PostStoryWithFile(
	not_null<Window::SessionController*> controller,
	not_null<PeerData*> peer,
	const QString &path);
void PostStoryWithMimeData(
	not_null<Window::SessionController*> controller,
	not_null<PeerData*> peer,
	not_null<const QMimeData*> data);
void EditStory(
	not_null<Window::SessionController*> controller,
	not_null<Data::Story*> story);
void EditStoryPrivacy(
	not_null<Window::SessionController*> controller,
	not_null<Data::Story*> story);
void RepostStory(
	not_null<Window::SessionController*> controller,
	not_null<Data::Story*> story);
void PostStoryDraft(
	not_null<Window::SessionController*> controller,
	uint64 draftId);

[[nodiscard]] bool CanPostStory(not_null<PeerData*> peer);
[[nodiscard]] bool CanRepostStory(not_null<Data::Story*> story);
[[nodiscard]] bool CanEditStory(not_null<Data::Story*> story);
[[nodiscard]] bool CanEditStoryPrivacy(not_null<Data::Story*> story);

void ShowCanSendStoryError(
	not_null<Window::SessionController*> controller,
	not_null<PeerData*> peer,
	const Api::CanSendStory &result);

void SetupStoryPostToasts(not_null<Window::SessionController*> controller);

} // namespace Media::Stories
