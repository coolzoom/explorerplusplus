/******************************************************************
 *
 * Project: MyTreeView
 * File: DirectoryModificationHandler.cpp
 * License: GPL - See COPYING in the top level directory
 *
 * Handles directory modifications for the CMyTreeView class.
 *
 * Written by David Erceg
 * www.explorerplusplus.com
 *
 *****************************************************************/

#include "stdafx.h"
#include "../Helper/Helper.h"
#include "../Helper/Buffer.h"
#include "MyTreeView.h"
#include "MyTreeViewInternal.h"


void CMyTreeView::DirectoryAltered(void)
{
	EnterCriticalSection(&m_cs);

	KillTimer(m_hTreeView,0);

	/* Two basic situations to watch out for:
	 - File is created, then renamed. Both notifications
	 come in at the same time.
	 - File is renamed twice in a row. Both notifications
	 come in at the same time.
	 - File created or renamed, and then deleted. Both
	 notifications come in at the same time.
	These situations may also happen multiple times
	in one run. e.g. 5 files are created and renamed
	before the first notification somes in. Therefore,
	need to keep track of ALL events.
	Notifications:
	Created
	Created
	Renamed
	Renamed
	In this case, need to remember that the first two
	notifications referred to files that didn't exist. */
	for each(auto af in m_AlteredList)
	{
		switch(af.dwAction)
		{
			case FILE_ACTION_ADDED:
				DirectoryAlteredAddFile(af.szFileName);
				break;

			/* The modified notification does not need
			to be handled in any way. File size/date/attribute
			changes have no effect within the treeview. */
			case FILE_ACTION_MODIFIED:
				break;

			case FILE_ACTION_REMOVED:
				DirectoryAlteredRemoveFile(af.szFileName);
				break;

			case FILE_ACTION_RENAMED_OLD_NAME:
				/* Store the old name. Since
				FILE_ACTION_RENAMED_OLD_NAME/FILE_ACTION_RENAMED_NEW_NAME
				always come in in paris, we only need to be able to store
				one old filename. */
				StringCchCopy(m_szAlteredOldFileName,
					SIZEOF_ARRAY(m_szAlteredOldFileName),
					af.szFileName);
				break;

			case FILE_ACTION_RENAMED_NEW_NAME:
				DirectoryAlteredRenameFile(af.szFileName);
				break;
		}
	}

	m_AlteredList.clear();

	LeaveCriticalSection(&m_cs);
}

/* TODO: Will have to change this. If AddItem() fails for some
reason, then add the item to the tracking list. */
void CMyTreeView::DirectoryAlteredAddFile(TCHAR *szFullFileName)
{
	LPITEMIDLIST pidlComplete = NULL;
	HRESULT hr;

	/* We'll use this as a litmus test to check whether or not
	the file actually exists. */
	hr = GetIdlFromParsingName(szFullFileName,&pidlComplete);

	if(SUCCEEDED(hr))
	{
		AddItem(szFullFileName);
		CoTaskMemFree(pidlComplete);
	}
	else
	{
		/* The file doesn't exist, so keep a record of it. */
		AlteredFile_t af;

		StringCchCopy(af.szFileName,SIZEOF_ARRAY(af.szFileName),szFullFileName);
		af.dwAction = FILE_ACTION_ADDED;

		m_AlteredTrackingList.push_back(af);
	}
}

void CMyTreeView::DirectoryAlteredRemoveFile(TCHAR *szFullFileName)
{
	HTREEITEM hItem;

	hItem = LocateItemByPath(szFullFileName,FALSE);

	if(hItem != NULL)
	{
		RemoveItem(szFullFileName);
	}
	else
	{
		/* The file does not currently exist within
		the treeview. It should appear in the tracking
		list however. */
		for(auto itr = m_AlteredTrackingList.begin();itr != m_AlteredTrackingList.end();itr++)
		{
			if(lstrcmp(szFullFileName,itr->szFileName) == 0)
			{
				/* Found the deleted item. Remove it from the tracking list. */
				/* TODO: May need to do this multiple times.
				e.g.
				Created
				Renamed
				Removed

				Both the created and renamed notifications need to
				be removed from the queue. */
				m_AlteredTrackingList.erase(itr);
				break;
			}
		}
	}
}

void CMyTreeView::DirectoryAlteredRenameFile(TCHAR *szFullFileName)
{
	HTREEITEM hItem;

	/* Check if the file currently exists in the treeview. */
	hItem = LocateItemByPath(m_szAlteredOldFileName,FALSE);

	if(hItem != NULL)
	{
		RenameItem(hItem,szFullFileName);
	}
	else
	{
		BOOL bFound = FALSE;

		/* Search through the queue of file addtions
		and modifications for this file. */
		for(auto itr = m_AlteredTrackingList.begin();itr != m_AlteredTrackingList.end();itr++)
		{
			if(lstrcmp(m_szAlteredOldFileName,itr->szFileName) == 0)
			{
				/* Item has been found. Change the name on the
				notification and push it back into the list.
				Note: The item must be pushed directly after the current item. */
				StringCchCopy(itr->szFileName,SIZEOF_ARRAY(itr->szFileName),
					szFullFileName);

				for(auto itrAltered = m_AlteredList.begin();itrAltered != m_AlteredList.end();itrAltered++)
				{
					if(itrAltered->dwAction == FILE_ACTION_RENAMED_NEW_NAME &&
						lstrcmp(itrAltered->szFileName,szFullFileName) == 0)
					{
						m_AlteredList.insert(++itrAltered,*itr);
						break;
					}
				}

				m_AlteredTrackingList.erase(itr);

				bFound = TRUE;
				break;
			}
		}

		/* This may happen if a file has been
		renamed twice in a row, or renamed then deleted.
		e.g.
		1.txt (Existing file) -> 2.txt
		2.txt -> 3.txt
		
		Notifications:
		old name/new name - 1.txt/2.txt
		old name/new name - 2.txt/3.txt
		Need to remember the original filename.
		e.g. Need to be able to go from 1.txt to 3.txt. */
		if(!bFound)
		{
			/* TODO: Change the filename on the renamed action and push it
			back into the queue. */
		}
	}
}

void CMyTreeView::DirectoryAlteredCallback(TCHAR *szFileName,DWORD dwAction,
void *pData)
{
	DirectoryAltered_t	*pDirectoryAltered = NULL;
	CMyTreeView			*pMyTreeView = NULL;
	TCHAR				szFullFileName[MAX_PATH];

	pDirectoryAltered = (DirectoryAltered_t *)pData;

	pMyTreeView = (CMyTreeView *)pDirectoryAltered->pMyTreeView;

	StringCchCopy(szFullFileName,MAX_PATH,pDirectoryAltered->szPath);
	PathAppend(szFullFileName,szFileName);

	pMyTreeView->DirectoryModified(dwAction,szFullFileName);
}

void CMyTreeView::DirectoryModified(DWORD dwAction,TCHAR *szFullFileName)
{
	EnterCriticalSection(&m_cs);

	SetTimer(m_hTreeView,DIRECTORYMODIFIED_TIMER_ID,
		DIRECTORYMODIFIED_TIMER_ELAPSE,NULL);

	AlteredFile_t af;

	StringCchCopy(af.szFileName,SIZEOF_ARRAY(af.szFileName),szFullFileName);
	af.dwAction = dwAction;

	m_AlteredList.push_back(af);

	LeaveCriticalSection(&m_cs);
}

void CMyTreeView::AddDrive(TCHAR *szDrive)
{
	LPITEMIDLIST	pidlMyComputer = NULL;
	HTREEITEM		hMyComputer;
	HRESULT			hr;

	hr = SHGetFolderLocation(NULL,CSIDL_DRIVES,NULL,0,&pidlMyComputer);

	if(hr == S_OK)
	{
		hMyComputer = LocateExistingItem(pidlMyComputer);

		if(hMyComputer != NULL)
		{
			AddItemInternal(hMyComputer,szDrive);
		}

		CoTaskMemFree(pidlMyComputer);
	}
}

void CMyTreeView::AddItem(TCHAR *szFullFileName)
{
	TCHAR			szDirectory[MAX_PATH];
	TCHAR			szDesktop[MAX_PATH];
	HTREEITEM		hParent;
	BOOL			bDesktop;

	/* If the specified item is a drive, it
	will need to be handled differently,
	as it is a child of my computer (and
	as such is not a regular file). */
	if(PathIsRoot(szFullFileName))
	{
		AddDrive(szFullFileName);
	}
	else
	{
		StringCchCopy(szDirectory,MAX_PATH,szFullFileName);
		PathRemoveFileSpec(szDirectory);

		SHGetFolderPath(NULL,CSIDL_DESKTOP,NULL,
			SHGFP_TYPE_CURRENT,szDesktop);

		bDesktop = (lstrcmp(szDirectory,szDesktop) == 0);

		hParent = LocateExistingItem(szDirectory);

		/* If this items' parent isn't currently
		shown on the treeview and the item is not
		on the desktop, exit without doing anything
		further. */
		if(hParent == NULL && !bDesktop)
			return;

		if(bDesktop)
			hParent = TreeView_GetRoot(m_hTreeView);

		AddItemInternal(hParent,szFullFileName);
	}
}

void CMyTreeView::AddItemInternal(HTREEITEM hParent,TCHAR *szFullFileName)
{
	IShellFolder	*pShellFolder = NULL;
	LPITEMIDLIST	pidlComplete = NULL;
	LPITEMIDLIST	pidlRelative = NULL;
	HTREEITEM		hItem;
	TVITEMEX		tvItem;
	TVINSERTSTRUCT	tvis;
	SHFILEINFO		shfi;
	SFGAOF			Attributes;
	TCHAR			szDisplayName[MAX_PATH];
	HRESULT			hr;
	BOOL			res;
	int				iItemId;
	int				nChildren = 0;

	hr = GetIdlFromParsingName(szFullFileName,&pidlComplete);

	if(!SUCCEEDED(hr))
		return;

	tvItem.mask		= TVIF_CHILDREN | TVIF_STATE;
	tvItem.hItem	= hParent;
	res = TreeView_GetItem(m_hTreeView,&tvItem);

	if(res)
	{
		/* If the parent node is currently collapsed,
		simply indicate that it has children (i.e. a
		plus sign will be shown next to the parent node). */
		if((tvItem.cChildren == 0) ||
			((tvItem.state & TVIS_EXPANDED) != TVIS_EXPANDED))
		{
			tvItem.mask			= TVIF_CHILDREN;
			tvItem.hItem		= hParent;
			tvItem.cChildren	= 1;
			TreeView_SetItem(m_hTreeView,&tvItem);
		}
		else
		{
			SHGetFileInfo(szFullFileName,NULL,&shfi,
				sizeof(shfi),SHGFI_SYSICONINDEX);

			hr = SHBindToParent(pidlComplete,IID_IShellFolder,
				(void **)&pShellFolder,(LPCITEMIDLIST *)&pidlRelative);

			if(SUCCEEDED(hr))
			{
				Attributes = SFGAO_HASSUBFOLDER;

				/* Only retrieve the attributes for this item. */
				hr = pShellFolder->GetAttributesOf(1,
					(LPCITEMIDLIST *)&pidlRelative,&Attributes);

				if(SUCCEEDED(hr))
				{
					if((Attributes & SFGAO_HASSUBFOLDER) != SFGAO_HASSUBFOLDER)
						nChildren = 0;
					else
						nChildren = 1;

					iItemId = GenerateUniqueItemId();

					m_pItemInfo[iItemId].pidl = ILClone(pidlComplete);

					GetDisplayName(szFullFileName,szDisplayName,SHGDN_NORMAL);

					tvItem.mask				= TVIF_TEXT|TVIF_IMAGE|TVIF_SELECTEDIMAGE|TVIF_PARAM|TVIF_CHILDREN;
					tvItem.pszText			= szDisplayName;
					tvItem.iImage			= shfi.iIcon;
					tvItem.iSelectedImage	= shfi.iIcon;
					tvItem.lParam			= (LPARAM)iItemId;
					tvItem.cChildren		= nChildren;

					if(hParent != NULL)
					{
						tvis.hParent			= hParent;
						tvis.hInsertAfter		= DetermineItemSortedPosition(hParent,szFullFileName);
						tvis.itemex				= tvItem;

						hItem = TreeView_InsertItem(m_hTreeView,&tvis);
					}
				}

				pShellFolder->Release();
			}
		}
	}

	CoTaskMemFree(pidlComplete);
}

/* Renames an item in the treeview. This essentially involves changing
the items display text, and updating it's pidl. Note that the children of
this item MUST ALL BE UPDATED as well, since their pidl's will also
change. */
void CMyTreeView::RenameItem(HTREEITEM hItem,TCHAR *szFullFileName)
{
	TVITEMEX	tvItem;
	ItemInfo_t	*pItemInfo = NULL;
	LPITEMIDLIST	pidlParent;
	SHFILEINFO	shfi;
	TCHAR		szFileName[MAX_PATH];
	HRESULT		hr;
	BOOL		res;

	if(hItem == NULL)
		return;

	tvItem.mask		= TVIF_PARAM;
	tvItem.hItem	= hItem;
	res = TreeView_GetItem(m_hTreeView,&tvItem);

	if(res)
	{
		pItemInfo = &m_pItemInfo[(int)tvItem.lParam];

		CoTaskMemFree(pItemInfo->pidl);

		StringCchCopy(szFileName,MAX_PATH,szFullFileName);
		PathStripPath(szFileName);

		hr = GetIdlFromParsingName(szFullFileName,&pItemInfo->pidl);

		pidlParent = pItemInfo->pidl;

		if(SUCCEEDED(hr))
		{
			SHGetFileInfo(szFullFileName,NULL,&shfi,sizeof(shfi),SHGFI_SYSICONINDEX);

			tvItem.mask				= TVIF_HANDLE|TVIF_TEXT|TVIF_IMAGE|TVIF_SELECTEDIMAGE;
			tvItem.hItem			= hItem;
			tvItem.pszText			= szFileName;
			tvItem.iImage			= shfi.iIcon;
			tvItem.iSelectedImage	= shfi.iIcon;
			TreeView_SetItem(m_hTreeView,&tvItem);

			/* Now recursively go through each of this items children and
			update their pidl's. */
			UpdateChildren(hItem,pidlParent);
		}
	}
}

void CMyTreeView::UpdateChildren(HTREEITEM hParent,LPITEMIDLIST pidlParent)
{
	HTREEITEM hChild;
	TVITEMEX tvItem;
	LPITEMIDLIST pidl = NULL;
	BOOL bRes;

	hChild = TreeView_GetChild(m_hTreeView,hParent);

	if(hChild != NULL)
	{
		tvItem.mask		= TVIF_PARAM;
		tvItem.hItem	= hChild;
		bRes = TreeView_GetItem(m_hTreeView,&tvItem);

		if(bRes)
		{
			pidl = UpdateItemInfo(pidlParent,(int)tvItem.lParam);

			UpdateChildren(hChild,pidl);

			while((hChild = TreeView_GetNextItem(m_hTreeView,hChild,TVGN_NEXT)) != NULL)
			{
				tvItem.mask		= TVIF_PARAM;
				tvItem.hItem	= hChild;
				bRes = TreeView_GetItem(m_hTreeView,&tvItem);

				if(bRes)
				{
					pidl = UpdateItemInfo(pidlParent,(int)tvItem.lParam);

					UpdateChildren(hChild,pidl);
				}
			}
		}
	}
}

/* Updates an items pidl, returning a pointer to the new idl. */
LPITEMIDLIST CMyTreeView::UpdateItemInfo(LPITEMIDLIST pidlParent,int iItemId)
{
	ItemInfo_t *pItemInfo = NULL;

	pItemInfo = &m_pItemInfo[iItemId];

	CoTaskMemFree(pItemInfo->pidl);

	m_pItemInfo[iItemId].pidl = ILCombine(pidlParent,m_pItemInfo[iItemId].pridl);

	return m_pItemInfo[iItemId].pidl;
}

void CMyTreeView::RemoveItem(TCHAR *szFullFileName)
{
	HTREEITEM		hItem;
	TVITEM			tvItem;
	SFGAOF			Attributes;
	LPITEMIDLIST	pidl = NULL;
	WCHAR			szParentW[MAX_PATH];
	TCHAR			szParent[MAX_PATH];
	TCHAR			szDesktop[MAX_PATH];
	TCHAR			szFileName[MAX_PATH];
	HRESULT			hr;
	BOOL			bDesktop = FALSE;

	StringCchCopy(szParent,MAX_PATH,szFullFileName);
	PathRemoveFileSpec(szParent);

	StringCchCopy(szFileName,MAX_PATH,szFullFileName);
	PathStripPath(szFileName);

	SHGetFolderPath(NULL,CSIDL_DESKTOP,NULL,SHGFP_TYPE_CURRENT,szDesktop);

	bDesktop = !lstrcmp(szParent,szDesktop);

	hItem = LocateExistingItem(szParent);

	if(hItem != NULL)
	{
		#ifndef UNICODE
		MultiByteToWideChar(CP_ACP,0,szParent,-1,szParentW,SIZEOF_ARRAY(szParentW));
		#else
		StringCchCopy(szParentW,SIZEOF_ARRAY(szParentW),szParent);
		#endif

		hr = SHParseDisplayName(szParentW,NULL,&pidl,SFGAO_HASSUBFOLDER,&Attributes);

		if(SUCCEEDED(hr))
		{
			tvItem.mask		= TVIF_CHILDREN;

			/* If the parent folder no longer has any sub-folders,
			set its number of children to 0. */
			if((Attributes & SFGAO_HASSUBFOLDER) != SFGAO_HASSUBFOLDER)
			{
				tvItem.cChildren	= 0;
				TreeView_Expand(m_hTreeView,hItem,TVE_COLLAPSE);
			}
			else
			{
				tvItem.cChildren	= 1;
			}

			tvItem.mask		= TVIF_CHILDREN;
			tvItem.hItem	= hItem;
			TreeView_SetItem(m_hTreeView,&tvItem);

			CoTaskMemFree(pidl);
		}
	}

    /* File has been deleted. Can't use its PIDL to locate it
    in the treeview. Parse the items path to find it. */
	hItem = LocateItemByPath(szFullFileName,FALSE);

	if(hItem != NULL)
	{
		EraseItems(hItem);

		TreeView_DeleteItem(m_hTreeView,hItem);
	}

	/* If the item is on the desktop, it will need to
	be deleted twice. */
	hItem = CheckAgainstDesktop(szFullFileName);

	if(hItem != NULL)
	{
		EraseItems(hItem);

		TreeView_DeleteItem(m_hTreeView,hItem);
	}
}

void CALLBACK Timer_DirectoryModified(HWND hwnd,UINT uMsg,UINT_PTR idEvent,DWORD dwTime)
{
	KillTimer(hwnd,idEvent);

	return;
}