/*
This file is part of Telegram Desktop,
the official desktop application for the Telegram messaging service.

For license and copyright information please follow this link:
https://github.com/telegramdesktop/tdesktop/blob/master/LEGAL
*/
#include "dialogs/ui/dialogs_stories_content.h"

#include <cstdio>

namespace {

using Dialogs::Stories::Content;
using Dialogs::Stories::Element;

constexpr auto kMine = uint64(1);

[[nodiscard]] Element AddStoryEntry() {
	return { .id = kMine, .skipSmall = 1, .mine = 1 };
}

[[nodiscard]] bool Check(bool condition, const char *scenario) {
	if (!condition) {
		std::fprintf(stderr, "FAIL: %s\n", scenario);
	}
	return condition;
}

} // namespace

int main() {
	const auto three = Content{
		.elements = {
			{ .id = 2, .count = 1 },
			{ .id = 3, .count = 1 },
			{ .id = 4, .count = 1 },
		},
		.total = 3,
	};
	auto posting = three;
	posting.placeMyStoryFirst(AddStoryEntry);
	posting.placeMyStoryFirst(AddStoryEntry);

	auto empty = Content();
	empty.placeMyStoryFirst(AddStoryEntry);
	auto restricted = three;
	restricted.placeMyStoryFirst(nullptr);

	auto uploading = three;
	const auto ownUpload = Element{
		.id = kMine,
		.skipSmall = 1,
		.mine = 1,
		.uploading = 1,
		.uploadProgress = 41,
	};
	uploading.elements.insert(begin(uploading.elements) + 2, ownUpload);
	++uploading.total;
	uploading.placeMyStoryFirst(AddStoryEntry);

	const auto order = std::vector<Element>{
		AddStoryEntry(),
		three.elements[0],
		three.elements[1],
		three.elements[2],
	};
	auto expectedUpload = order;
	expectedUpload.front() = ownUpload;
	auto ok = Check(
		posting.elements == order
			&& posting.total == 4
			&& posting.elements.front().canAddFirstStory(),
		"three foreign stories retain their order "
		"behind one posting entry");
	ok &= Check(empty == Content(), "empty bar keeps the toolbar entry");
	ok
		&= Check(
			restricted == three,
			"posting restrictions hide the entry");
	ok &= Check(
		uploading.elements == expectedUpload
			&& uploading.total == 4
			&& !uploading.elements.front().canAddFirstStory(),
		"upload updates keep the actual own entry first");
	return ok ? 0 : 1;
}
