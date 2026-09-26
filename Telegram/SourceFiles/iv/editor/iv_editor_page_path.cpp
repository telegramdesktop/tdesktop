/*
This file is part of Telegram Desktop,
the official desktop application for the Telegram messaging service.

For license and copyright information please follow this link:
https://github.com/telegramdesktop/tdesktop/blob/master/LEGAL
*/
#include "iv/editor/iv_editor_page_path.h"

namespace Iv::Editor {
namespace {

using PreparedBlockContainerKind = Markdown::PreparedEditBlockContainerKind;
using PreparedBlockContainerPath = Markdown::PreparedEditBlockContainerPath;
using PreparedBlockContainerStep = Markdown::PreparedEditBlockContainerStep;
using PreparedBlockPath = Markdown::PreparedEditBlockPath;

} // namespace

BlockContainerPath BlockChildrenContainer(BlockPath path) {
	auto result = std::move(path.container);
	result.steps.push_back({
		.kind = BlockContainerKind::BlockChildren,
		.blockIndex = path.index,
	});
	return result;
}

BlockContainerPath ListItemChildrenContainer(
		BlockPath path,
		int itemIndex) {
	auto result = std::move(path.container);
	result.steps.push_back({
		.kind = BlockContainerKind::ListItemChildren,
		.blockIndex = path.index,
		.listItemIndex = itemIndex,
	});
	return result;
}

PreparedBlockContainerPath ToPreparedBlockContainerPath(
		const BlockContainerPath &path) {
	auto result = PreparedBlockContainerPath();
	result.steps.reserve(path.steps.size());
	for (const auto &step : path.steps) {
		auto converted = PreparedBlockContainerStep();
		converted.blockIndex = step.blockIndex;
		converted.listItemIndex = step.listItemIndex;
		switch (step.kind) {
		case BlockContainerKind::Root:
			continue;
		case BlockContainerKind::BlockChildren:
			converted.kind = PreparedBlockContainerKind::BlockChildren;
			break;
		case BlockContainerKind::ListItemChildren:
			converted.kind = PreparedBlockContainerKind::ListItemChildren;
			break;
		}
		result.steps.push_back(converted);
	}
	return result;
}

PreparedBlockPath ToPreparedBlockPath(
		const BlockPath &path) {
	return {
		.container = ToPreparedBlockContainerPath(path.container),
		.index = path.index,
	};
}

BlockContainerPath ToStateBlockContainerPath(
		const PreparedBlockContainerPath &path) {
	auto result = BlockContainerPath();
	result.steps.reserve(path.steps.size());
	for (const auto &step : path.steps) {
		auto converted = BlockContainerStep();
		converted.blockIndex = step.blockIndex;
		converted.listItemIndex = step.listItemIndex;
		switch (step.kind) {
		case PreparedBlockContainerKind::Root:
			continue;
		case PreparedBlockContainerKind::BlockChildren:
			converted.kind = BlockContainerKind::BlockChildren;
			break;
		case PreparedBlockContainerKind::ListItemChildren:
			converted.kind = BlockContainerKind::ListItemChildren;
			break;
		}
		result.steps.push_back(converted);
	}
	return result;
}

BlockPath ToStateBlockPath(
		const PreparedBlockPath &path) {
	return {
		.container = ToStateBlockContainerPath(path.container),
		.index = path.index,
	};
}

} // namespace Iv::Editor
