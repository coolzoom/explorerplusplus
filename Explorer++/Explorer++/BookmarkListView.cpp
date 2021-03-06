// Copyright (C) Explorer++ Project
// SPDX-License-Identifier: GPL-3.0-only
// See LICENSE in the top level directory

#include "stdafx.h"
#include "BookmarkListView.h"
#include "BookmarkDataExchange.h"
#include "MainResource.h"
#include "ResourceHelper.h"
#include "../Helper/iDropSource.h"
#include "../Helper/ListViewHelper.h"
#include "../Helper/Macros.h"
#include "../Helper/WindowHelper.h"
#include <boost/range/adaptor/filtered.hpp>
#include <boost/range/adaptor/indexed.hpp>

BookmarkListView::BookmarkListView(HWND hListView, HMODULE resourceModule,
	BookmarkTree *bookmarkTree, IExplorerplusplus *expp, const std::vector<Column> &initialColumns) :
	BookmarkDropTargetWindow(hListView, bookmarkTree),
	m_hListView(hListView),
	m_resourceModule(resourceModule),
	m_bookmarkTree(bookmarkTree),
	m_expp(expp),
	m_columns(initialColumns),
	m_sortMode(BookmarkHelper::SortMode::Default),
	m_sortAscending(true)
{
	SetWindowTheme(hListView, L"Explorer", NULL);
	ListView_SetExtendedListViewStyleEx(hListView,
		LVS_EX_DOUBLEBUFFER | LVS_EX_FULLROWSELECT,
		LVS_EX_DOUBLEBUFFER | LVS_EX_FULLROWSELECT);

	UINT dpi = m_dpiCompat.GetDpiForWindow(hListView);
	int iconWidth = m_dpiCompat.GetSystemMetricsForDpi(SM_CXSMICON, dpi);
	int iconHeight = m_dpiCompat.GetSystemMetricsForDpi(SM_CYSMICON, dpi);
	std::tie(m_imageList, m_imageListMappings) = ResourceHelper::CreateIconImageList(expp->GetIconResourceLoader(),
		iconWidth, iconHeight, {Icon::Folder, Icon::Bookmarks});
	ListView_SetImageList(hListView, m_imageList.get(), LVSIL_SMALL);

	InsertColumns(initialColumns);

	m_windowSubclasses.push_back(WindowSubclassWrapper(GetParent(m_hListView), ParentWndProcStub,
		PARENT_SUBCLASS_ID, reinterpret_cast<DWORD_PTR>(this)));

	m_connections.push_back(m_bookmarkTree->bookmarkItemAddedSignal.AddObserver(
		std::bind(&BookmarkListView::OnBookmarkItemAdded, this, std::placeholders::_1, std::placeholders::_2)));
	m_connections.push_back(m_bookmarkTree->bookmarkItemUpdatedSignal.AddObserver(
		std::bind(&BookmarkListView::OnBookmarkItemUpdated, this, std::placeholders::_1, std::placeholders::_2)));
	m_connections.push_back(m_bookmarkTree->bookmarkItemMovedSignal.AddObserver(
		std::bind(&BookmarkListView::OnBookmarkItemMoved, this, std::placeholders::_1, std::placeholders::_2,
			std::placeholders::_3, std::placeholders::_4, std::placeholders::_5)));
	m_connections.push_back(m_bookmarkTree->bookmarkItemPreRemovalSignal.AddObserver(
		std::bind(&BookmarkListView::OnBookmarkItemPreRemoval, this, std::placeholders::_1)));
}

void BookmarkListView::InsertColumns(const std::vector<Column> &columns)
{
	for (const auto &column : columns | boost::adaptors::filtered(IsColumnActive) | boost::adaptors::indexed(0))
	{
		InsertColumn(column.value(), static_cast<int>(column.index()));
	}
}

void BookmarkListView::InsertColumn(const Column &column, int index)
{
	std::wstring columnText = GetColumnText(column.columnType);

	LVCOLUMN lvColumn;
	lvColumn.mask = LVCF_TEXT | LVCF_WIDTH;
	lvColumn.pszText = columnText.data();
	lvColumn.cx = column.width;
	int insertedIndex = ListView_InsertColumn(m_hListView, index, &lvColumn);

	HWND header = ListView_GetHeader(m_hListView);

	HDITEM hdItem;
	hdItem.mask = HDI_LPARAM;
	hdItem.lParam = static_cast<LPARAM>(column.columnType);
	Header_SetItem(header, insertedIndex, &hdItem);
}

std::wstring BookmarkListView::GetColumnText(ColumnType columnType)
{
	UINT resourceId = GetColumnTextResourceId(columnType);
	return ResourceHelper::LoadString(m_resourceModule, resourceId);
}

std::vector<BookmarkListView::Column> BookmarkListView::GetColumns()
{
	int index = 0;

	for (auto &column : m_columns | boost::adaptors::filtered(IsColumnActive))
	{
		column.width = ListView_GetColumnWidth(m_hListView, index++);
	}

	return m_columns;
}

bool BookmarkListView::IsColumnActive(const Column &column)
{
	return column.active;
}

std::optional<BookmarkListView::ColumnType> BookmarkListView::GetColumnTypeByIndex(int index) const
{
	HWND hHeader = ListView_GetHeader(m_hListView);

	HDITEM hdItem;
	hdItem.mask = HDI_LPARAM;
	BOOL res = Header_GetItem(hHeader, index, &hdItem);

	if (!res)
	{
		return std::nullopt;
	}

	return static_cast<ColumnType>(hdItem.lParam);
}

UINT BookmarkListView::GetColumnTextResourceId(ColumnType columnType)
{
	switch (columnType)
	{
	case ColumnType::Name:
		return IDS_BOOKMARKS_COLUMN_NAME;
		break;

	case ColumnType::Location:
		return IDS_BOOKMARKS_COLUMN_LOCATION;
		break;

	case ColumnType::DateCreated:
		return IDS_BOOKMARKS_COLUMN_DATE_CREATED;
		break;

	case ColumnType::DateModified:
		return IDS_BOOKMARKS_COLUMN_DATE_MODIFIED;
		break;
	}

	throw std::runtime_error("Bookmark column string resource not found");
}

LRESULT CALLBACK BookmarkListView::ParentWndProcStub(HWND hwnd, UINT uMsg, WPARAM wParam,
	LPARAM lParam, UINT_PTR uIdSubclass, DWORD_PTR dwRefData)
{
	UNREFERENCED_PARAMETER(uIdSubclass);

	BookmarkListView *listView = reinterpret_cast<BookmarkListView *>(dwRefData);
	return listView->ParentWndProc(hwnd, uMsg, wParam, lParam);
}

LRESULT CALLBACK BookmarkListView::ParentWndProc(HWND hwnd, UINT uMsg, WPARAM wParam, LPARAM lParam)
{
	switch (uMsg)
	{
	case WM_NOTIFY:
		if (reinterpret_cast<LPNMHDR>(lParam)->hwndFrom == m_hListView)
		{
			switch (reinterpret_cast<LPNMHDR>(lParam)->code)
			{
			case NM_DBLCLK:
				OnDblClk(reinterpret_cast<NMITEMACTIVATE *>(lParam));
				break;

			case NM_RCLICK:
				OnRClick(reinterpret_cast<NMITEMACTIVATE *>(lParam));
				return TRUE;
				break;

			case LVN_GETDISPINFO:
				OnGetDispInfo(reinterpret_cast<NMLVDISPINFO *>(lParam));
				break;

			case LVN_BEGINLABELEDIT:
				return OnBeginLabelEdit(reinterpret_cast<NMLVDISPINFO *>(lParam));
				break;

			case LVN_ENDLABELEDIT:
				return OnEndLabelEdit(reinterpret_cast<NMLVDISPINFO *>(lParam));
				break;

			case LVN_KEYDOWN:
				OnKeyDown(reinterpret_cast<NMLVKEYDOWN *>(lParam));
				break;

			case LVN_BEGINDRAG:
				OnBeginDrag(reinterpret_cast<NMLISTVIEW *>(lParam));
				break;
			}
		}
		else if (reinterpret_cast<LPNMHDR>(lParam)->hwndFrom == ListView_GetHeader(m_hListView))
		{
			switch (reinterpret_cast<LPNMHDR>(lParam)->code)
			{
			case NM_RCLICK:
			{
				POINT pt;
				DWORD messagePos = GetMessagePos();
				POINTSTOPOINT(pt, MAKEPOINTS(messagePos));
				OnHeaderRClick(pt);
				return TRUE;
			}
			break;
			}
		}
		break;
	}

	return DefSubclassProc(hwnd, uMsg, wParam, lParam);
}

void BookmarkListView::NavigateToBookmarkFolder(BookmarkItem *bookmarkFolder)
{
	assert(bookmarkFolder->IsFolder());

	m_currentBookmarkFolder = bookmarkFolder;

	ListView_DeleteAllItems(m_hListView);

	int position = 0;

	for (auto &childItem : bookmarkFolder->GetChildren())
	{
		InsertBookmarkItemIntoListView(childItem.get(), position);

		position++;
	}

	navigationSignal.m_signal(bookmarkFolder);
}

int BookmarkListView::InsertBookmarkItemIntoListView(BookmarkItem *bookmarkItem, int position)
{
	assert(position >= 0 && position <= ListView_GetItemCount(m_hListView));

	TCHAR szName[256];
	StringCchCopy(szName, SIZEOF_ARRAY(szName), bookmarkItem->GetName().c_str());

	int iImage;

	if (bookmarkItem->GetType() == BookmarkItem::Type::Folder)
	{
		iImage = m_imageListMappings.at(Icon::Folder);
	}
	else
	{
		iImage = m_imageListMappings.at(Icon::Bookmarks);
	}

	LVITEM lvi;
	lvi.mask = LVIF_TEXT | LVIF_IMAGE | LVIF_PARAM;
	lvi.iItem = position;
	lvi.iSubItem = 0;
	lvi.iImage = iImage;
	lvi.pszText = szName;
	lvi.lParam = reinterpret_cast<LPARAM>(bookmarkItem);
	int iItem = ListView_InsertItem(m_hListView, &lvi);

	return iItem;
}

BookmarkItem *BookmarkListView::GetBookmarkItemFromListView(int iItem)
{
	LVITEM lvi;
	lvi.mask = LVIF_PARAM;
	lvi.iItem = iItem;
	lvi.iSubItem = 0;
	ListView_GetItem(m_hListView, &lvi);

	return reinterpret_cast<BookmarkItem *>(lvi.lParam);
}

void BookmarkListView::SortItems()
{
	ListView_SortItems(m_hListView, SortBookmarksStub, reinterpret_cast<LPARAM>(this));
}

int CALLBACK BookmarkListView::SortBookmarksStub(LPARAM lParam1, LPARAM lParam2, LPARAM lParamSort)
{
	BookmarkListView *listView = reinterpret_cast<BookmarkListView *>(lParamSort);

	return listView->SortBookmarks(lParam1, lParam2);
}

int CALLBACK BookmarkListView::SortBookmarks(LPARAM lParam1, LPARAM lParam2)
{
	auto firstItem = reinterpret_cast<BookmarkItem *>(lParam1);
	auto secondItem = reinterpret_cast<BookmarkItem *>(lParam2);

	int iRes = BookmarkHelper::Sort(m_sortMode, firstItem, secondItem);

	// When using the default sort mode (in which items are sorted according to
	// their position within the parent folder), the items are always in
	// ascending order.
	if (!m_sortAscending && m_sortMode != BookmarkHelper::SortMode::Default)
	{
		iRes = -iRes;
	}

	return iRes;
}

BookmarkHelper::SortMode BookmarkListView::GetSortMode() const
{
	return m_sortMode;
}

void BookmarkListView::SetSortMode(BookmarkHelper::SortMode sortMode)
{
	m_sortMode = sortMode;

	SortItems();
}

bool BookmarkListView::GetSortAscending() const
{
	return m_sortAscending;
}

void BookmarkListView::SetSortAscending(bool sortAscending)
{
	m_sortAscending = sortAscending;

	SortItems();
}

void BookmarkListView::OnDblClk(const NMITEMACTIVATE *itemActivate)
{
	if (itemActivate->iItem == -1)
	{
		return;
	}

	auto bookmarkItem = GetBookmarkItemFromListView(itemActivate->iItem);

	if (bookmarkItem->IsFolder())
	{
		NavigateToBookmarkFolder(bookmarkItem);
	}
	else
	{
		Tab &selectedTab = m_expp->GetTabContainer()->GetSelectedTab();
		selectedTab.GetShellBrowser()->GetNavigationController()->BrowseFolder(bookmarkItem->GetLocation().c_str());
	}
}

void BookmarkListView::OnRClick(const NMITEMACTIVATE *itemActivate)
{
	HMENU hMenu = LoadMenu(m_resourceModule, MAKEINTRESOURCE(IDR_MANAGEBOOKMARKS_BOOKMARK_RCLICK_MENU));
	SetMenuDefaultItem(GetSubMenu(hMenu, 0), IDM_MB_BOOKMARK_OPEN, FALSE);

	POINT pt = itemActivate->ptAction;
	ClientToScreen(m_hListView, &pt);

	TrackPopupMenu(GetSubMenu(hMenu, 0), TPM_LEFTALIGN, pt.x, pt.y, 0, m_hListView, NULL);
	DestroyMenu(hMenu);
}

void BookmarkListView::OnGetDispInfo(NMLVDISPINFO *dispInfo)
{
	if (WI_IsFlagSet(dispInfo->item.mask, LVIF_TEXT))
	{
		auto bookmarkItem = GetBookmarkItemFromListView(dispInfo->item.iItem);

		auto columnType = GetColumnTypeByIndex(dispInfo->item.iSubItem);
		assert(columnType);

		std::wstring columnText = GetBookmarkItemColumnInfo(bookmarkItem, *columnType);

		StringCchCopy(dispInfo->item.pszText, dispInfo->item.cchTextMax, columnText.c_str());

		WI_SetFlag(dispInfo->item.mask, LVIF_DI_SETITEM);
	}
}

std::wstring BookmarkListView::GetBookmarkItemColumnInfo(const BookmarkItem *bookmarkItem,
	ColumnType columnType)
{
	switch (columnType)
	{
	case ColumnType::Name:
		return bookmarkItem->GetName();
		break;

	case ColumnType::Location:
		if (bookmarkItem->IsBookmark())
		{
			return bookmarkItem->GetLocation();
		}
		else
		{
			return std::wstring();
		}
		break;

	case ColumnType::DateCreated:
	{
		FILETIME dateCreated = bookmarkItem->GetDateCreated();
		return FormatDate(&dateCreated);
	}
	break;

	case ColumnType::DateModified:
	{
		FILETIME dateModified = bookmarkItem->GetDateModified();
		return FormatDate(&dateModified);
	}
	break;
	}

	throw std::runtime_error("Bookmark column type not found");
}

std::wstring BookmarkListView::FormatDate(const FILETIME *date)
{
	/* TODO: Friendly dates. */
	TCHAR formattedDate[256];
	BOOL res = CreateFileTimeString(date, formattedDate, std::size(formattedDate), FALSE);

	if (res)
	{
		return formattedDate;
	}

	return std::wstring();
}

BOOL BookmarkListView::OnBeginLabelEdit(const NMLVDISPINFO *dispInfo)
{
	auto bookmarkItem = GetBookmarkItemFromListView(dispInfo->item.iItem);

	if (m_bookmarkTree->IsPermanentNode(bookmarkItem))
	{
		return TRUE;
	}

	return FALSE;
}

BOOL BookmarkListView::OnEndLabelEdit(const NMLVDISPINFO *dispInfo)
{
	if (dispInfo->item.pszText == nullptr && lstrlen(dispInfo->item.pszText) == 0)
	{
		return FALSE;
	}

	auto bookmarkItem = GetBookmarkItemFromListView(dispInfo->item.iItem);

	if (m_bookmarkTree->IsPermanentNode(bookmarkItem))
	{
		return FALSE;
	}

	bookmarkItem->SetName(dispInfo->item.pszText);

	return TRUE;
}

void BookmarkListView::OnKeyDown(const NMLVKEYDOWN *keyDown)
{
	switch (keyDown->wVKey)
	{
	case VK_F2:
		OnRename();
		break;

	case 'A':
		if (IsKeyDown(VK_CONTROL) &&
			!IsKeyDown(VK_SHIFT) &&
			!IsKeyDown(VK_MENU))
		{
			NListView::ListView_SelectAllItems(m_hListView, TRUE);
		}
		break;

	/* TODO: */
	case VK_RETURN:
		break;

	case VK_DELETE:
		OnDelete();
		break;
	}
}

void BookmarkListView::OnBeginDrag(const NMLISTVIEW *listView)
{
	wil::com_ptr<IDropSource> dropSource;
	HRESULT hr = CreateDropSource(&dropSource, DragType::LeftClick);

	if (FAILED(hr))
	{
		return;
	}

	BookmarkItem *bookmarkItem = GetBookmarkItemFromListView(listView->iItem);
	auto &ownedPtr = bookmarkItem->GetParent()->GetChildOwnedPtr(bookmarkItem);
	auto dataObject = BookmarkDataExchange::CreateDataObject(ownedPtr);

	wil::com_ptr<IDragSourceHelper> dragSourceHelper;
	hr = CoCreateInstance(CLSID_DragDropHelper, nullptr, CLSCTX_ALL,
		IID_PPV_ARGS(&dragSourceHelper));

	if (SUCCEEDED(hr))
	{
		dragSourceHelper->InitializeFromWindow(m_hListView, nullptr, dataObject.get());
	}

	DWORD effect;
	DoDragDrop(dataObject.get(), dropSource.get(), DROPEFFECT_MOVE, &effect);
}

void BookmarkListView::OnRename()
{
	int item = ListView_GetNextItem(m_hListView, -1, LVNI_SELECTED);

	if (item != -1)
	{
		ListView_EditLabel(m_hListView, item);
	}
}

void BookmarkListView::OnDelete()
{
	std::vector<BookmarkItem *> bookmarkItems = GetSelectedBookmarkItems();

	for (BookmarkItem *bookmarkItem : bookmarkItems)
	{
		m_bookmarkTree->RemoveBookmarkItem(bookmarkItem);
	}
}

std::vector<BookmarkItem *> BookmarkListView::GetSelectedBookmarkItems()
{
	std::vector<BookmarkItem *> bookmarksItems;
	int index = -1;

	while ((index = ListView_GetNextItem(m_hListView, index, LVNI_SELECTED)) != -1)
	{
		BookmarkItem *bookmarkItem = GetBookmarkItemFromListView(index);
		bookmarksItems.push_back(bookmarkItem);
	}

	return bookmarksItems;
}

void BookmarkListView::OnHeaderRClick(const POINT &pt)
{
	auto menu = BuildHeaderContextMenu();

	if (!menu)
	{
		return;
	}

	int cmd = TrackPopupMenu(menu.get(), TPM_LEFTALIGN | TPM_RETURNCMD, pt.x, pt.y, 0, m_hListView, nullptr);

	if (cmd != 0)
	{
		OnHeaderContextMenuItemSelected(cmd);
	}
}

wil::unique_hmenu BookmarkListView::BuildHeaderContextMenu()
{
	wil::unique_hmenu menu(CreatePopupMenu());

	if (!menu)
	{
		return nullptr;
	}

	int index = 0;

	for (const auto &column : m_columns)
	{
		std::wstring columnText = GetColumnText(column.columnType);

		MENUITEMINFO mii;
		mii.cbSize = sizeof(mii);
		mii.fMask = MIIM_ID | MIIM_STRING | MIIM_STATE;
		mii.wID = static_cast<int>(column.columnType);
		mii.dwTypeData = columnText.data();
		mii.fState = 0;

		if (column.active)
		{
			mii.fState |= MFS_CHECKED;
		}

		/* The name column cannot be removed. */
		if (column.columnType == ColumnType::Name)
		{
			mii.fState |= MFS_DISABLED;
		}

		InsertMenuItem(menu.get(), index++, TRUE, &mii);
	}

	return menu;
}

void BookmarkListView::OnHeaderContextMenuItemSelected(int menuItemId)
{
	ColumnType columnType = static_cast<ColumnType>(menuItemId);
	Column &column = GetColumnByType(columnType);
	int columnIndex = GetColumnIndexByType(columnType);

	bool nowActive = !column.active;

	if (nowActive)
	{
		InsertColumn(column, columnIndex);

		// TODO: Update.
		/*for (const auto &childItem : bookmarkFolder->GetChildren())
		{
			TCHAR szColumn[256];
			GetBookmarkItemColumnInfo(childItem.get(), itr->ColumnType, szColumn, SIZEOF_ARRAY(szColumn));
			ListView_SetItemText(hListView, iBookmarkItem, iColumn, szColumn);

			++iBookmarkItem;
		}*/
	}
	else
	{
		column.width = ListView_GetColumnWidth(m_hListView, columnIndex);
		ListView_DeleteColumn(m_hListView, columnIndex);
	}

	column.active = nowActive;
}

void BookmarkListView::OnBookmarkItemAdded(BookmarkItem &bookmarkItem, size_t index)
{
	if (bookmarkItem.GetParent() == m_currentBookmarkFolder)
	{
		InsertBookmarkItemIntoListView(&bookmarkItem, static_cast<int>(index));
	}
}

void BookmarkListView::OnBookmarkItemUpdated(BookmarkItem &bookmarkItem, BookmarkItem::PropertyType propertyType)
{
	if (bookmarkItem.GetParent() != m_currentBookmarkFolder)
	{
		return;
	}

	auto index = GetBookmarkItemIndex(&bookmarkItem);
	assert(index);

	ColumnType columnType = MapPropertyTypeToColumnType(propertyType);
	Column &column = GetColumnByType(columnType);

	if (!column.active)
	{
		return;
	}

	int columnIndex = GetColumnIndexByType(columnType);
	std::wstring columnText = GetBookmarkItemColumnInfo(&bookmarkItem, columnType);
	ListView_SetItemText(m_hListView, *index, columnIndex, columnText.data());
}

void BookmarkListView::OnBookmarkItemMoved(BookmarkItem *bookmarkItem, const BookmarkItem *oldParent,
	size_t oldIndex, const BookmarkItem *newParent, size_t newIndex)
{
	UNREFERENCED_PARAMETER(oldIndex);

	if (oldParent == m_currentBookmarkFolder)
	{
		RemoveBookmarkItem(bookmarkItem);
	}

	if (newParent == m_currentBookmarkFolder)
	{
		InsertBookmarkItemIntoListView(bookmarkItem, static_cast<int>(newIndex));
	}
}

void BookmarkListView::OnBookmarkItemPreRemoval(BookmarkItem &bookmarkItem)
{
	if (bookmarkItem.GetParent() == m_currentBookmarkFolder)
	{
		RemoveBookmarkItem(&bookmarkItem);
	}
}

void BookmarkListView::RemoveBookmarkItem(const BookmarkItem *bookmarkItem)
{
	auto index = GetBookmarkItemIndex(bookmarkItem);
	assert(index);

	ListView_DeleteItem(m_hListView, *index);
}

std::optional<int> BookmarkListView::GetBookmarkItemIndex(const BookmarkItem *bookmarkItem) const
{
	LVFINDINFO findInfo;
	findInfo.flags = LVFI_PARAM;
	findInfo.lParam = reinterpret_cast<LPARAM>(bookmarkItem);
	int index = ListView_FindItem(m_hListView, -1, &findInfo);

	if (index == -1)
	{
		return std::nullopt;
	}

	return index;
}

BookmarkListView::ColumnType BookmarkListView::MapPropertyTypeToColumnType(BookmarkItem::PropertyType propertyType) const
{
	switch (propertyType)
	{
	case BookmarkItem::PropertyType::Name:
		return ColumnType::Name;

	case BookmarkItem::PropertyType::Location:
		return ColumnType::Location;

	case BookmarkItem::PropertyType::DateCreated:
		return ColumnType::DateCreated;

	case BookmarkItem::PropertyType::DateModified:
		return ColumnType::DateModified;

	default:
		throw std::runtime_error("Bookmark column not found");
		break;
	}
}

BookmarkListView::Column &BookmarkListView::GetColumnByType(ColumnType columnType)
{
	auto itr = std::find_if(m_columns.begin(), m_columns.end(), [columnType] (const Column &column) {
		return column.columnType == columnType;
	});

	assert(itr != m_columns.end());

	return *itr;
}

// If the specified column is active, this function returns the index of the
// column (as it appears in the listview). If it's not active, the function will
// return the index the column would be shown at, if it were active.
int BookmarkListView::GetColumnIndexByType(ColumnType columnType) const
{
	auto itr = std::find_if(m_columns.begin(), m_columns.end(), [columnType] (const Column &column) {
		return column.columnType == columnType;
	});

	assert(itr != m_columns.end());

	auto columnIndex = std::count_if(m_columns.begin(), itr, [] (const Column &column) {
		return column.active;
	});

	return static_cast<int>(columnIndex);
}

BookmarkListView::DropLocation BookmarkListView::GetDropLocation(const POINT &pt)
{
	POINT ptClient = pt;
	ScreenToClient(m_hListView, &ptClient);

	LVHITTESTINFO hitTestInfo;
	hitTestInfo.pt = ptClient;
	ListView_HitTest(m_hListView, &hitTestInfo);

	BookmarkItem *parentFolder = nullptr;
	size_t position;
	bool parentFolderSelected = false;

	if (WI_IsFlagSet(hitTestInfo.flags, LVHT_NOWHERE))
	{
		parentFolder = m_currentBookmarkFolder;
		position = FindNextItemIndex(ptClient);
	}
	else
	{
		auto bookmarkItem = GetBookmarkItemFromListView(hitTestInfo.iItem);

		RECT itemRect;
		ListView_GetItemRect(m_hListView, hitTestInfo.iItem, &itemRect, LVIR_BOUNDS);

		if (bookmarkItem->IsFolder())
		{
			RECT folderCentralRect = itemRect;
			int indent = static_cast<int>(FOLDER_CENTRAL_RECT_INDENT_PERCENTAGE * GetRectHeight(&itemRect));
			InflateRect(&folderCentralRect, 0, -indent);

			if (ptClient.y < folderCentralRect.top)
			{
				parentFolder = m_currentBookmarkFolder;
				position = hitTestInfo.iItem;
			}
			else if (ptClient.y > folderCentralRect.bottom)
			{
				parentFolder = m_currentBookmarkFolder;
				position = hitTestInfo.iItem + 1;
			}
			else
			{
				parentFolder = bookmarkItem;
				position = bookmarkItem->GetChildren().size();
				parentFolderSelected = true;
			}
		}
		else
		{
			parentFolder = m_currentBookmarkFolder;
			position = hitTestInfo.iItem;

			if (ptClient.y > (itemRect.top + GetRectHeight(&itemRect) / 2))
			{
				position++;
			}
		}
	}

	return { parentFolder, position, parentFolderSelected };
}

int BookmarkListView::FindNextItemIndex(const POINT &ptClient)
{
	int numItems = ListView_GetItemCount(m_hListView);
	int nextIndex = 0;

	for (int i = 0; i < numItems; i++)
	{
		RECT rc;
		ListView_GetItemRect(m_hListView, i, &rc, LVIR_BOUNDS);

		if (ptClient.y < rc.top)
		{
			break;
		}

		nextIndex = i + 1;
	}

	return nextIndex;
}

void BookmarkListView::UpdateUiForDropLocation(const DropLocation &dropLocation)
{
	RemoveDropHighlight();

	if (dropLocation.parentFolder == m_currentBookmarkFolder)
	{
		DWORD flags;
		int numItems = ListView_GetItemCount(m_hListView);
		size_t finalPosition = dropLocation.position;

		if (finalPosition == static_cast<size_t>(numItems))
		{
			finalPosition--;
			flags = LVIM_AFTER;
		}
		else
		{
			flags = 0;
		}

		LVINSERTMARK insertMark;
		insertMark.cbSize = sizeof(insertMark);
		insertMark.dwFlags = flags;
		insertMark.iItem = static_cast<int>(finalPosition);
		ListView_SetInsertMark(m_hListView, &insertMark);
	}
	else
	{
		RemoveInsertionMark();

		auto selectedItemIndex = GetBookmarkItemIndex(dropLocation.parentFolder);
		assert(selectedItemIndex);

		ListView_SetItemState(m_hListView, *selectedItemIndex, LVIS_DROPHILITED, LVIS_DROPHILITED);
		m_previousDropItem = *selectedItemIndex;
	}
}

void BookmarkListView::ResetDropUiState()
{
	RemoveInsertionMark();
	RemoveDropHighlight();
}

void BookmarkListView::RemoveInsertionMark()
{
	LVINSERTMARK insertMark;
	insertMark.cbSize = sizeof(insertMark);
	insertMark.dwFlags = 0;
	insertMark.iItem = -1;
	ListView_SetInsertMark(m_hListView, &insertMark);
}

void BookmarkListView::RemoveDropHighlight()
{
	if (m_previousDropItem)
	{
		ListView_SetItemState(m_hListView, *m_previousDropItem, 0, LVIS_DROPHILITED);
		m_previousDropItem.reset();
	}
}