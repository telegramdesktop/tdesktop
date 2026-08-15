/*
This file is part of Telegram Desktop,
the official desktop application for the Telegram messaging service.

For license and copyright information please follow this link:
https://github.com/telegramdesktop/tdesktop/blob/master/LEGAL
*/
#include "iv/editor/iv_editor_structure_menu.h"

#include "iv/editor/iv_editor_state.h"
#include "lang/lang_keys.h"
#include "menu/menu_checked_action.h"
#include "ui/widgets/popup_menu.h"

#include "styles/style_iv.h"
#include "styles/style_menu_icons.h"

namespace Iv::Editor {
namespace {

using PreparedListItemRange = Markdown::PreparedEditListItemRange;
using PreparedOrderedListType = Markdown::PreparedOrderedListType;
using PreparedEditTableCellRange = Markdown::PreparedEditTableCellRange;

[[nodiscard]] QString OrderedListTypeText(PreparedOrderedListType type) {
	switch (type) {
	case PreparedOrderedListType::LowerAlpha:
		return tr::lng_article_list_lowercase_letters(tr::now);
	case PreparedOrderedListType::UpperAlpha:
		return tr::lng_article_list_uppercase_letters(tr::now);
	case PreparedOrderedListType::LowerRoman:
		return tr::lng_article_list_lowercase_roman(tr::now);
	case PreparedOrderedListType::UpperRoman:
		return tr::lng_article_list_uppercase_roman(tr::now);
	case PreparedOrderedListType::Decimal:
		return tr::lng_article_list_numbers(tr::now);
	}
	return QString();
}

[[nodiscard]] const style::icon *OrderedListTypeIcon(
		PreparedOrderedListType type) {
	switch (type) {
	case PreparedOrderedListType::LowerAlpha:
		return &st::ivEditorToolbarOrderedListAlphaLowerIcon;
	case PreparedOrderedListType::UpperAlpha:
		return &st::ivEditorToolbarOrderedListAlphaUpperIcon;
	case PreparedOrderedListType::LowerRoman:
		return &st::ivEditorToolbarOrderedListRomanLowerIcon;
	case PreparedOrderedListType::UpperRoman:
		return &st::ivEditorToolbarOrderedListRomanUpperIcon;
	case PreparedOrderedListType::Decimal:
		return &st::ivEditorToolbarOrderedListIcon;
	}
	return nullptr;
}

[[nodiscard]] bool OrderedListTypeChecked(
		const State::ListSelectionInfo &info,
		PreparedOrderedListType type) {
	switch (type) {
	case PreparedOrderedListType::LowerAlpha:
		return info.allOrderedLowerAlpha;
	case PreparedOrderedListType::UpperAlpha:
		return info.allOrderedUpperAlpha;
	case PreparedOrderedListType::LowerRoman:
		return info.allOrderedLowerRoman;
	case PreparedOrderedListType::UpperRoman:
		return info.allOrderedUpperRoman;
	case PreparedOrderedListType::Decimal:
		return info.allOrderedDecimal;
	}
	return false;
}

} // namespace

void FillListChangeMenu(
		not_null<Ui::PopupMenu*> menu,
		not_null<State*> state,
		const PreparedListItemRange &range,
		Fn<void(Fn<bool()>)> applyChange) {
	const auto info = state->listSelectionInfo(range);
	if (!info.valid) {
		return;
	}
	const auto ordered = (info.listKind == RichPage::ListKind::Ordered);
	const auto task = !ordered && info.taskList;
	const auto bullet = !ordered && !task;
	Menu::AddCheckedAction(
		menu,
		tr::lng_article_insert_ordered_list(tr::now),
		[=] {
			applyChange([=] {
				return state->setListStyle(range, State::ListStyle::Ordered);
			});
		},
		&st::ivEditorToolbarOrderedListIcon,
		ordered);
	Menu::AddCheckedAction(
		menu,
		tr::lng_article_insert_bullet_list(tr::now),
		[=] {
			applyChange([=] {
				return state->setListStyle(range, State::ListStyle::Bullet);
			});
		},
		&st::ivEditorToolbarBulletListIcon,
		bullet);
	Menu::AddCheckedAction(
		menu,
		tr::lng_article_insert_task_list(tr::now),
		[=] {
			applyChange([=] {
				return state->setListStyle(range, State::ListStyle::Task);
			});
		},
		&st::ivEditorToolbarTaskListIcon,
		task);
	if (!ordered) {
		return;
	}
	const auto addOrderedTypeAction = [&](PreparedOrderedListType type) {
		Menu::AddCheckedAction(
			menu,
			OrderedListTypeText(type),
			[=] {
				applyChange([=] {
					return state->setListOrderedType(range, type);
				});
			},
			OrderedListTypeIcon(type),
			// Checked when every item already renders with this type.
			info.listOrderedUniform && (info.listOrderedType == type));
	};
	menu->addSeparator();
	addOrderedTypeAction(PreparedOrderedListType::Decimal);
	addOrderedTypeAction(PreparedOrderedListType::LowerAlpha);
	addOrderedTypeAction(PreparedOrderedListType::UpperAlpha);
	addOrderedTypeAction(PreparedOrderedListType::LowerRoman);
	addOrderedTypeAction(PreparedOrderedListType::UpperRoman);
	menu->addSeparator();
	Menu::AddCheckedAction(
		menu,
		tr::lng_article_list_reversed(tr::now),
		[=] {
			applyChange([=] {
				return state->setListOrderedReversed(range, !info.reversed);
			});
		},
		&st::menuIconChangeOrder,
		info.reversed);
}

void FillListItemChangeMenu(
		not_null<Ui::PopupMenu*> menu,
		not_null<State*> state,
		const PreparedListItemRange &range,
		Fn<void(Fn<bool()>)> applyChange) {
	const auto info = state->listSelectionInfo(range);
	if (!info.valid
		|| info.taskList
		|| info.listKind != RichPage::ListKind::Ordered) {
		return;
	}
	const auto addOrderedTypeAction = [&](PreparedOrderedListType type) {
		Menu::AddCheckedAction(
			menu,
			OrderedListTypeText(type),
			[=] {
				applyChange([=] {
					return state->setListItemOrderedType(range, type);
				});
			},
			OrderedListTypeIcon(type),
			OrderedListTypeChecked(info, type));
	};
	// Decimal for one item is dropped on save unless the list is decimal too.
	if (info.listOrderedType == PreparedOrderedListType::Decimal) {
		addOrderedTypeAction(PreparedOrderedListType::Decimal);
	}
	addOrderedTypeAction(PreparedOrderedListType::LowerAlpha);
	addOrderedTypeAction(PreparedOrderedListType::UpperAlpha);
	addOrderedTypeAction(PreparedOrderedListType::LowerRoman);
	addOrderedTypeAction(PreparedOrderedListType::UpperRoman);
}

void FillTableChangeMenu(
		not_null<Ui::PopupMenu*> menu,
		not_null<State*> state,
		const PreparedEditTableCellRange &range,
		Fn<void(Fn<bool()>)> applyChange) {
	const auto info = state->tableSelectionInfo(range);
	if (!info.valid) {
		return;
	}
	auto addCells = std::make_unique<Ui::PopupMenu>(
		menu,
		st::popupMenuWithIcons);
	addCells->addAction(
		tr::lng_article_table_add_row_above(tr::now),
		[=] {
			applyChange([=] {
				return state->addTableRow(range, false);
			});
		},
		&st::ivEditorTableAddRowAboveIcon);
	addCells->addAction(
		tr::lng_article_table_add_row_below(tr::now),
		[=] {
			applyChange([=] {
				return state->addTableRow(range, true);
			});
		},
		&st::ivEditorTableAddRowBelowIcon);
	addCells->addSeparator();
	addCells->addAction(
		tr::lng_article_table_add_column_left(tr::now),
		[=] {
			applyChange([=] {
				return state->addTableColumn(range, false);
			});
		},
		&st::ivEditorTableAddColumnLeftIcon);
	addCells->addAction(
		tr::lng_article_table_add_column_right(tr::now),
		[=] {
			applyChange([=] {
				return state->addTableColumn(range, true);
			});
		},
		&st::ivEditorTableAddColumnRightIcon);
	menu->addAction(
		tr::lng_article_table_add_cells(tr::now),
		std::move(addCells),
		&st::ivEditorTableAddCellsIcon,
		&st::ivEditorTableAddCellsIcon);
	auto alignment = std::make_unique<Ui::PopupMenu>(
		menu,
		st::popupMenuWithIcons);
	const auto raw = not_null<Ui::PopupMenu*>(alignment.get());
	Menu::AddCheckedAction(
		raw,
		tr::lng_article_table_align_left(tr::now),
		[=] {
			applyChange([=] {
				return state->setTableAlignment(
					range,
					RichPage::TableAlignment::Left);
			});
		},
		&st::ivEditorTableAlignLeftIcon,
		info.allAlignLeft);
	Menu::AddCheckedAction(
		raw,
		tr::lng_article_table_align_center(tr::now),
		[=] {
			applyChange([=] {
				return state->setTableAlignment(
					range,
					RichPage::TableAlignment::Center);
			});
		},
		&st::ivEditorTableAlignCenterIcon,
		info.allAlignCenter);
	Menu::AddCheckedAction(
		raw,
		tr::lng_article_table_align_right(tr::now),
		[=] {
			applyChange([=] {
				return state->setTableAlignment(
					range,
					RichPage::TableAlignment::Right);
			});
		},
		&st::ivEditorTableAlignRightIcon,
		info.allAlignRight);
	raw->addSeparator();
	Menu::AddCheckedAction(
		raw,
		tr::lng_article_table_align_top(tr::now),
		[=] {
			applyChange([=] {
				return state->setTableVerticalAlignment(
					range,
					RichPage::TableVerticalAlignment::Top);
			});
		},
		&st::ivEditorTableAlignTopIcon,
		info.allAlignTop);
	Menu::AddCheckedAction(
		raw,
		tr::lng_article_table_align_middle(tr::now),
		[=] {
			applyChange([=] {
				return state->setTableVerticalAlignment(
					range,
					RichPage::TableVerticalAlignment::Middle);
			});
		},
		&st::ivEditorTableAlignMiddleIcon,
		info.allAlignMiddle);
	Menu::AddCheckedAction(
		raw,
		tr::lng_article_table_align_bottom(tr::now),
		[=] {
			applyChange([=] {
				return state->setTableVerticalAlignment(
					range,
					RichPage::TableVerticalAlignment::Bottom);
			});
		},
		&st::ivEditorTableAlignBottomIcon,
		info.allAlignBottom);
	menu->addAction(
		tr::lng_article_table_alignment(tr::now),
		std::move(alignment),
		&st::ivEditorTableAlignmentIcon,
		&st::ivEditorTableAlignmentIcon);
	auto rowsRange = range;
	rowsRange.columnFrom = 0;
	rowsRange.columnTill = info.totalColumns;
	auto columnsRange = range;
	columnsRange.rowFrom = 0;
	columnsRange.rowTill = info.totalRows;
	const auto allRows = (info.selectedRows == info.totalRows);
	const auto allColumns = (info.selectedColumns == info.totalColumns);
	if (allRows && allColumns) {
		menu->addAction(
			tr::lng_article_table_delete_table(tr::now),
			[=] {
				applyChange([=] {
					return state->removeTable(range);
				});
			},
			&st::menuIconTableSubmenuDelete);
	} else {
		auto deleteCells = std::make_unique<Ui::PopupMenu>(
			menu,
			st::popupMenuWithIcons);
		if (allRows) {
			deleteCells->addAction(
				tr::lng_article_table_delete_table(tr::now),
				[=] {
					applyChange([=] {
						return state->removeTable(rowsRange);
					});
				},
				&st::menuIconTableSubmenuDelete);
		} else {
			deleteCells->addAction(
				(info.selectedRows == 1)
					? tr::lng_article_table_delete_row(tr::now)
					: tr::lng_article_table_delete_rows(tr::now),
				[=] {
					applyChange([=] {
						return state->removeTableRows(rowsRange);
					});
				},
				&st::ivEditorTableDeleteRowsIcon);
		}
		if (allColumns) {
			deleteCells->addAction(
				tr::lng_article_table_delete_table(tr::now),
				[=] {
					applyChange([=] {
						return state->removeTable(columnsRange);
					});
				},
				&st::menuIconTableSubmenuDelete);
		} else {
			deleteCells->addAction(
				(info.selectedColumns == 1)
					? tr::lng_article_table_delete_column(tr::now)
					: tr::lng_article_table_delete_columns(tr::now),
				[=] {
					applyChange([=] {
						return state->removeTableColumns(columnsRange);
					});
				},
				&st::ivEditorTableDeleteColumnsIcon);
		}
		menu->addAction(
			tr::lng_article_table_delete_cells(tr::now),
			std::move(deleteCells),
			&st::ivEditorTableDeleteCellsIcon,
			&st::ivEditorTableDeleteCellsIcon);
	}
	if (info.canSplitCell) {
		menu->addSeparator();
		menu->addAction(
			tr::lng_article_table_split_cell(tr::now),
			[=] {
				applyChange([=] {
					return state->splitTableCell(range);
				});
			},
			&st::ivEditorTableSplitIcon);
	} else if (info.canUniteCells) {
		menu->addSeparator();
		menu->addAction(
			tr::lng_article_table_unite_cells(tr::now),
			[=] {
				applyChange([=] {
					return state->uniteTableCells(range);
				});
			},
			&st::ivEditorTableMergeIcon);
	}
	menu->addSeparator();
	Menu::AddCheckedAction(
		menu,
		info.singleCell
			? tr::lng_article_table_header_cell(tr::now)
			: tr::lng_article_table_header_cells(tr::now),
		[=] {
			applyChange([=] {
				return state->setTableHeader(range, !info.allHeader);
			});
		},
		info.allHeader
			? &st::ivEditorTableHeaderOffIcon
			: &st::ivEditorTableHeaderIcon,
		info.allHeader);
	menu->addSeparator();
	Menu::AddCheckedAction(
		menu,
		tr::lng_article_table_borderless(tr::now),
		[=] {
			applyChange([=] {
				return state->setTableBordered(range, !info.bordered);
			});
		},
		&st::ivEditorTableBorderlessIcon,
		!info.bordered);
	Menu::AddCheckedAction(
		menu,
		tr::lng_article_table_striped(tr::now),
		[=] {
			applyChange([=] {
				return state->setTableStriped(range, !info.striped);
			});
		},
		&st::ivEditorTableStripedIcon,
		info.striped);
	Menu::AddCheckedAction(
		menu,
		tr::lng_article_table_compact(tr::now),
		[=] {
			applyChange([=] {
				return state->setTableCompact(range, !info.compact);
			});
		},
		&st::ivEditorTableCompactIcon,
		info.compact);
}

} // namespace Iv::Editor
