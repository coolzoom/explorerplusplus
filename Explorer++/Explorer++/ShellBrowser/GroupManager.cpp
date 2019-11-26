// Copyright (C) Explorer++ Project
// SPDX-License-Identifier: GPL-3.0-only
// See LICENSE in the top level directory

#include "stdafx.h"
#include "ShellBrowser.h"
#include "Config.h"
#include "MainResource.h"
#include "ResourceHelper.h"
#include "SortModes.h"
#include "../Helper/Helper.h"
#include "../Helper/Macros.h"
#include "../Helper/ShellHelper.h"
#include "../Helper/TimeHelper.h"
#include <boost/date_time/gregorian/gregorian.hpp>
#include <boost/date_time/posix_time/posix_time.hpp>
#include <iphlpapi.h>
#include <propkey.h>
#include <cassert>
#include <list>

namespace
{
	static const UINT KBYTE = 1024;
	static const UINT MBYTE = 1024 * 1024;
	static const UINT GBYTE = 1024 * 1024 *1024;
}

#define GROUP_BY_DATECREATED	0
#define GROUP_BY_DATEMODIFIED	1
#define GROUP_BY_DATEACCESSED	2

#define GROUP_BY_SIZE			0
#define GROUP_BY_TOTALSIZE		1

BOOL CShellBrowser::GetShowInGroups(void) const
{
	return m_folderSettings.showInGroups;
}

/* Simply sets the grouping flag, without actually moving
items into groups. */
void CShellBrowser::SetShowInGroupsFlag(BOOL bShowInGroups)
{
	m_folderSettings.showInGroups = bShowInGroups;
}

void CShellBrowser::SetShowInGroups(BOOL bShowInGroups)
{
	m_folderSettings.showInGroups = bShowInGroups;

	if(!m_folderSettings.showInGroups)
	{
		ListView_EnableGroupView(m_hListView,FALSE);
		SortFolder(m_folderSettings.sortMode);
		return;
	}
	else
	{
		MoveItemsIntoGroups();
	}
}

INT CALLBACK CShellBrowser::GroupNameComparisonStub(INT Group1_ID, INT Group2_ID, void *pvData)
{
	CShellBrowser *shellBrowser = reinterpret_cast<CShellBrowser *>(pvData);
	return shellBrowser->GroupNameComparison(Group1_ID, Group2_ID);
}

INT CALLBACK CShellBrowser::GroupNameComparison(INT Group1_ID, INT Group2_ID)
{
	int iReturnValue;

	std::wstring groupHeader1 = RetrieveGroupHeader(Group1_ID);
	std::wstring groupHeader2 = RetrieveGroupHeader(Group2_ID);

	if (groupHeader1 == L"Other" && groupHeader2 != L"Other")
	{
		iReturnValue = 1;
	}
	else if (groupHeader1 != L"Other" && groupHeader2 == L"Other")
	{
		iReturnValue = -1;
	}
	else if (groupHeader1 == L"Other" && groupHeader2 == L"Other")
	{
		iReturnValue = 0;
	}
	else
	{
		iReturnValue = groupHeader1.compare(groupHeader2);
	}

	if (!m_folderSettings.sortAscending)
	{
		iReturnValue = -iReturnValue;
	}

	return iReturnValue;
}

INT CALLBACK CShellBrowser::GroupFreeSpaceComparisonStub(INT Group1_ID, INT Group2_ID, void *pvData)
{
	CShellBrowser *shellBrowser = reinterpret_cast<CShellBrowser *>(pvData);
	return shellBrowser->GroupFreeSpaceComparison(Group1_ID, Group2_ID);
}

INT CALLBACK CShellBrowser::GroupFreeSpaceComparison(INT Group1_ID, INT Group2_ID)
{
	int iReturnValue;

	std::wstring groupHeader1 = RetrieveGroupHeader(Group1_ID);
	std::wstring groupHeader2 = RetrieveGroupHeader(Group2_ID);

	if (groupHeader1 == L"Unspecified" && groupHeader2 != L"Unspecified")
	{
		iReturnValue = 1;
	}
	else if (groupHeader1 != L"Unspecified" && groupHeader2 == L"Unspecified")
	{
		iReturnValue = -1;
	}
	else if (groupHeader1 == L"Unspecified" && groupHeader2 == L"Unspecified")
	{
		iReturnValue = 0;
	}
	else
	{
		iReturnValue = groupHeader1.compare(groupHeader2);
	}

	if (!m_folderSettings.sortAscending)
	{
		iReturnValue = -iReturnValue;
	}

	return iReturnValue;
}

std::wstring CShellBrowser::RetrieveGroupHeader(int groupId)
{
	auto itr = std::find_if(m_GroupList.begin(), m_GroupList.end(), [groupId] (const TypeGroup_t &group) {
		return group.iGroupId == groupId;
	});

	return itr->header;
}

/*
 * Determines the id of the group the specified
 * item belongs to.
 */
int CShellBrowser::DetermineItemGroup(int iItemInternal)
{
	BasicItemInfo_t basicItemInfo = getBasicItemInfo(iItemInternal);
	PFNLVGROUPCOMPARE groupComparison = nullptr;
	std::wstring groupHeader;

	switch(m_folderSettings.sortMode)
	{
		case SortMode::Name:
			groupHeader = DetermineItemNameGroup(iItemInternal);
			groupComparison = GroupNameComparisonStub;
			break;

		case SortMode::Type:
			groupHeader = DetermineItemTypeGroupVirtual(iItemInternal);
			groupComparison = GroupNameComparisonStub;
			break;

		case SortMode::Size:
			groupHeader = DetermineItemSizeGroup(iItemInternal);
			groupComparison = GroupNameComparisonStub;
			break;

		case SortMode::DateModified:
			groupHeader = DetermineItemDateGroup(iItemInternal,GROUP_BY_DATEMODIFIED);
			groupComparison = GroupNameComparisonStub;
			break;

		case SortMode::TotalSize:
			groupHeader = DetermineItemTotalSizeGroup(iItemInternal);
			groupComparison = GroupNameComparisonStub;
			break;

		case SortMode::FreeSpace:
			groupHeader = DetermineItemFreeSpaceGroup(iItemInternal);
			groupComparison = GroupFreeSpaceComparisonStub;
			break;

		case SortMode::DateDeleted:
			break;

		case SortMode::OriginalLocation:
			groupHeader = DetermineItemSummaryGroup(basicItemInfo, &SCID_ORIGINAL_LOCATION, m_config->globalFolderSettings);
			groupComparison = GroupNameComparisonStub;
			break;

		case SortMode::Attributes:
			groupHeader = DetermineItemAttributeGroup(iItemInternal);
			groupComparison = GroupNameComparisonStub;
			break;

		case SortMode::ShortName:
			groupHeader = DetermineItemNameGroup(iItemInternal);
			groupComparison = GroupNameComparisonStub;
			break;

		case SortMode::Owner:
			groupHeader = DetermineItemOwnerGroup(iItemInternal);
			groupComparison = GroupNameComparisonStub;
			break;

		case SortMode::ProductName:
			groupHeader = DetermineItemVersionGroup(iItemInternal,_T("ProductName"));
			groupComparison = GroupNameComparisonStub;
			break;

		case SortMode::Company:
			groupHeader = DetermineItemVersionGroup(iItemInternal,_T("CompanyName"));
			groupComparison = GroupNameComparisonStub;
			break;

		case SortMode::Description:
			groupHeader = DetermineItemVersionGroup(iItemInternal,_T("FileDescription"));
			groupComparison = GroupNameComparisonStub;
			break;

		case SortMode::FileVersion:
			groupHeader = DetermineItemVersionGroup(iItemInternal,_T("FileVersion"));
			groupComparison = GroupNameComparisonStub;
			break;

		case SortMode::ProductVersion:
			groupHeader = DetermineItemVersionGroup(iItemInternal,_T("ProductVersion"));
			groupComparison = GroupNameComparisonStub;
			break;

		case SortMode::ShortcutTo:
			break;

		case SortMode::HardLinks:
			break;

		case SortMode::Extension:
			groupHeader = DetermineItemExtensionGroup(iItemInternal);
			groupComparison = GroupNameComparisonStub;
			break;

		case SortMode::Created:
			groupHeader = DetermineItemDateGroup(iItemInternal,GROUP_BY_DATECREATED);
			groupComparison = GroupNameComparisonStub;
			break;

		case SortMode::Accessed:
			groupHeader = DetermineItemDateGroup(iItemInternal,GROUP_BY_DATEACCESSED);
			groupComparison = GroupNameComparisonStub;
			break;

		case SortMode::Title:
			groupHeader = DetermineItemSummaryGroup(basicItemInfo,&PKEY_Title, m_config->globalFolderSettings);
			groupComparison = GroupNameComparisonStub;
			break;

		case SortMode::Subject:
			groupHeader = DetermineItemSummaryGroup(basicItemInfo,&PKEY_Subject, m_config->globalFolderSettings);
			groupComparison = GroupNameComparisonStub;
			break;

		case SortMode::Authors:
			groupHeader = DetermineItemSummaryGroup(basicItemInfo,&PKEY_Author, m_config->globalFolderSettings);
			groupComparison = GroupNameComparisonStub;
			break;

		case SortMode::Keywords:
			groupHeader = DetermineItemSummaryGroup(basicItemInfo,&PKEY_Keywords, m_config->globalFolderSettings);
			groupComparison = GroupNameComparisonStub;
			break;

		case SortMode::Comments:
			groupHeader = DetermineItemSummaryGroup(basicItemInfo,&PKEY_Comment, m_config->globalFolderSettings);
			groupComparison = GroupNameComparisonStub;
			break;


		case SortMode::CameraModel:
			groupHeader = DetermineItemCameraPropertyGroup(iItemInternal,PropertyTagEquipModel);
			groupComparison = GroupNameComparisonStub;
			break;

		case SortMode::DateTaken:
			groupHeader = DetermineItemCameraPropertyGroup(iItemInternal,PropertyTagDateTime);
			groupComparison = GroupNameComparisonStub;
			break;

		case SortMode::Width:
			groupHeader = DetermineItemCameraPropertyGroup(iItemInternal,PropertyTagImageWidth);
			groupComparison = GroupNameComparisonStub;
			break;

		case SortMode::Height:
			groupHeader = DetermineItemCameraPropertyGroup(iItemInternal,PropertyTagImageHeight);
			groupComparison = GroupNameComparisonStub;
			break;


		case SortMode::VirtualComments:
			break;

		case SortMode::FileSystem:
			groupHeader = DetermineItemFileSystemGroup(iItemInternal);
			groupComparison = GroupNameComparisonStub;
			break;

		case SortMode::NumPrinterDocuments:
			break;

		case SortMode::PrinterStatus:
			break;

		case SortMode::PrinterComments:
			break;

		case SortMode::PrinterLocation:
			break;

		case SortMode::NetworkAdapterStatus:
			groupHeader = DetermineItemNetworkStatus(iItemInternal);
			groupComparison = GroupNameComparisonStub;
			break;

		default:
			assert(false);
			break;
	}

	return CheckGroup(groupHeader, groupComparison);
}

/*
 * Checks if a group with specified id is already
 * in the listview. If not, the group is inserted
 * into its sorted position with the specified
 * header text.
 */
int CShellBrowser::CheckGroup(std::wstring_view groupHeader, PFNLVGROUPCOMPARE groupComparison)
{
	auto itr = std::find_if(m_GroupList.begin(), m_GroupList.end(), [groupHeader] (const TypeGroup_t &group) {
		return group.header == groupHeader;
	});

	auto generateListViewHeader = [] (std::wstring_view header, int numItems) {
		return std::wstring(header) + L" (" + std::to_wstring(numItems) + L")";
	};

	if (itr != m_GroupList.end())
	{
		itr->nItems++;

		std::wstring listViewHeader = generateListViewHeader(groupHeader, itr->nItems);

		LVGROUP lvGroup;
		lvGroup.cbSize = sizeof(LVGROUP);
		lvGroup.mask = LVGF_HEADER;
		lvGroup.pszHeader = listViewHeader.data();
		ListView_SetGroupInfo(m_hListView, itr->iGroupId, &lvGroup);

		return itr->iGroupId;
	}

	int groupId = m_iGroupId++;

	TypeGroup_t typeGroup;
	typeGroup.header = groupHeader;
	typeGroup.iGroupId = groupId;
	typeGroup.nItems = 1;
	m_GroupList.push_back(typeGroup);

	std::wstring listViewHeader = generateListViewHeader(groupHeader, typeGroup.nItems);

	LVINSERTGROUPSORTED lvigs;
	lvigs.lvGroup.cbSize	= sizeof(LVGROUP);
	lvigs.lvGroup.mask		= LVGF_HEADER | LVGF_GROUPID | LVGF_STATE;
	lvigs.lvGroup.state		= LVGS_COLLAPSIBLE;
	lvigs.lvGroup.pszHeader	= listViewHeader.data();
	lvigs.lvGroup.iGroupId	= groupId;
	lvigs.lvGroup.stateMask	= 0;
	lvigs.pfnGroupCompare	= groupComparison;
	lvigs.pvData			= reinterpret_cast<void *>(this);
	ListView_InsertGroupSorted(m_hListView,&lvigs);

	return groupId;
}

/*
 * Determines the id of the group to which the specified
 * item belongs, based on the item's name.
 * Also returns the text header for the group when szGroupHeader
 * is non-NULL.
 */
/* TODO: These groups have changed as of Windows Visa.*/
std::wstring CShellBrowser::DetermineItemNameGroup(int iItemInternal) const
{
	/* Take the first character of the item's name,
	and use it to determine which group it belongs to. */
	TCHAR ch = m_itemInfoMap.at(iItemInternal).szDisplayName[0];

	if(iswalpha(ch))
	{
		return std::wstring(1, towupper(ch));
	}
	else
	{
		return L"Other";
	}
}

/*
 * Determines the id of the group to which the specified
 * item belongs, based on the item's size.
 * Also returns the text header for the group when szGroupHeader
 * is non-NULL.
 */
std::wstring CShellBrowser::DetermineItemSizeGroup(int iItemInternal) const
{
	TCHAR *SizeGroups[] = {_T("Folders"),_T("Tiny"),_T("Small"),_T("Medium"),_T("Large"),_T("Huge")};
	int SizeGroupLimits[] = {0,0,32 * KBYTE,100 * KBYTE,MBYTE,10 * MBYTE};
	int nGroups = 6;
	int iSize;
	int i;

	if((m_itemInfoMap.at(iItemInternal).wfd.dwFileAttributes & FILE_ATTRIBUTE_DIRECTORY)
	== FILE_ATTRIBUTE_DIRECTORY)
	{
		/* This item is a folder. */
		iSize = 0;
	}
	else
	{
		i = nGroups - 1;

		double FileSize = m_itemInfoMap.at(iItemInternal).wfd.nFileSizeLow +
		(m_itemInfoMap.at(iItemInternal).wfd.nFileSizeHigh * pow(2.0,32.0));

		/* Check which of the size groups this item belongs to. */
		while(FileSize < SizeGroupLimits[i]
		&& i > 0)
			i--;

		iSize = i;
	}

	return SizeGroups[iSize];
}

/*
 * Determines the id of the group to which the specified
 * drive/folder item belongs, based on the item's total size.
 * Also returns the text header for the group when szGroupHeader
 * is non-NULL.
 */
/* TODO: These groups have changed as of Windows Vista. */
std::wstring CShellBrowser::DetermineItemTotalSizeGroup(int iItemInternal) const
{
	IShellFolder *pShellFolder	= NULL;
	PCITEMID_CHILD pidlRelative	= NULL;
	TCHAR *SizeGroups[] = {_T("Unspecified"),_T("Small"),_T("Medium"),_T("Huge"),_T("Gigantic")};
	TCHAR szItem[MAX_PATH];
	STRRET str;
	ULARGE_INTEGER nTotalBytes;
	ULARGE_INTEGER nFreeBytes;
	BOOL bRoot;
	BOOL bRes = FALSE;
	ULARGE_INTEGER TotalSizeGroupLimits[6];
	int nGroups = 5;
	int iSize = 0;
	int i;

	TotalSizeGroupLimits[0].QuadPart	= 0;
	TotalSizeGroupLimits[1].QuadPart	= 0;
	TotalSizeGroupLimits[2].QuadPart	= GBYTE;
	TotalSizeGroupLimits[3].QuadPart	= 20 * TotalSizeGroupLimits[2].QuadPart;
	TotalSizeGroupLimits[4].QuadPart	= 100 * TotalSizeGroupLimits[2].QuadPart;

	SHBindToParent(m_itemInfoMap.at(iItemInternal).pidlComplete.get(), IID_PPV_ARGS(&pShellFolder), &pidlRelative);

	pShellFolder->GetDisplayNameOf(pidlRelative,SHGDN_FORPARSING,&str);
	StrRetToBuf(&str,pidlRelative,szItem,SIZEOF_ARRAY(szItem));

	bRoot = PathIsRoot(szItem);

	if(bRoot)
	{
		bRes = GetDiskFreeSpaceEx(szItem,NULL,&nTotalBytes,&nFreeBytes);

		pShellFolder->Release();

		i = nGroups - 1;

		while(nTotalBytes.QuadPart < TotalSizeGroupLimits[i].QuadPart && i > 0)
			i--;

		iSize = i;
	}

	if(!bRoot || !bRes)
	{
		iSize = 0;
	}

	return SizeGroups[iSize];
}

std::wstring CShellBrowser::DetermineItemTypeGroupVirtual(int iItemInternal) const
{
	SHFILEINFO shfi;
	std::list<TypeGroup_t>::iterator itr;

	SHGetFileInfo((LPTSTR)m_itemInfoMap.at(iItemInternal).pidlComplete.get(),
		0,&shfi,sizeof(shfi),SHGFI_PIDL|SHGFI_TYPENAME);

	return shfi.szTypeName;
}

std::wstring CShellBrowser::DetermineItemDateGroup(int iItemInternal,int iDateType) const
{
	using namespace boost::gregorian;
	using namespace boost::posix_time;

	SYSTEMTIME stFileTime;
	BOOL ret = FALSE;

	switch(iDateType)
	{
	case GROUP_BY_DATEMODIFIED:
		ret = FileTimeToLocalSystemTime(&m_itemInfoMap.at(iItemInternal).wfd.ftLastWriteTime, &stFileTime);
		break;

	case GROUP_BY_DATECREATED:
		ret = FileTimeToLocalSystemTime(&m_itemInfoMap.at(iItemInternal).wfd.ftCreationTime, &stFileTime);
		break;

	case GROUP_BY_DATEACCESSED:
		ret = FileTimeToLocalSystemTime(&m_itemInfoMap.at(iItemInternal).wfd.ftLastAccessTime, &stFileTime);
		break;

	default:
		throw std::runtime_error("Incorrect date type");
	}

	if (!ret)
	{
		return ResourceHelper::LoadString(m_hResourceModule, IDS_GROUPBY_UNSPECIFIED);
	}

	FILETIME localFileTime;
	ret = SystemTimeToFileTime(&stFileTime, &localFileTime);

	if (!ret)
	{
		return ResourceHelper::LoadString(m_hResourceModule, IDS_GROUPBY_UNSPECIFIED);
	}

	ptime filePosixTime = from_ftime<ptime>(localFileTime);
	date fileDate = filePosixTime.date();

	date today = day_clock::local_day();

	if (fileDate > today)
	{
		return ResourceHelper::LoadString(m_hResourceModule, IDS_GROUPBY_DATE_FUTURE);
	}

	if (fileDate == today)
	{
		return ResourceHelper::LoadString(m_hResourceModule, IDS_GROUPBY_DATE_TODAY);
	}

	date yesterday = today - days(1);

	if (fileDate == yesterday)
	{
		return ResourceHelper::LoadString(m_hResourceModule, IDS_GROUPBY_DATE_YESTERDAY);
	}

	// Note that this assumes that Sunday is the first day of the week.
	unsigned short currentWeekday = today.day_of_week().as_number();
	date startOfWeek = today - days(currentWeekday);

	if (fileDate >= startOfWeek)
	{
		return ResourceHelper::LoadString(m_hResourceModule, IDS_GROUPBY_DATE_THIS_WEEK);
	}

	date startOfLastWeek = startOfWeek - weeks(1);

	if (fileDate >= startOfLastWeek)
	{
		return ResourceHelper::LoadString(m_hResourceModule, IDS_GROUPBY_DATE_LAST_WEEK);
	}

	date startOfMonth = date(today.year(), today.month(), 1);

	if (fileDate >= startOfMonth)
	{
		return ResourceHelper::LoadString(m_hResourceModule, IDS_GROUPBY_DATE_THIS_MONTH);
	}

	date startOfLastMonth = startOfMonth - months(1);

	if (fileDate >= startOfLastMonth)
	{
		return ResourceHelper::LoadString(m_hResourceModule, IDS_GROUPBY_DATE_LAST_MONTH);
	}

	date startOfYear = date(today.year(), 1, 1);

	if (fileDate >= startOfYear)
	{
		return ResourceHelper::LoadString(m_hResourceModule, IDS_GROUPBY_DATE_THIS_YEAR);
	}

	date startOfLastYear = startOfYear - years(1);

	if (fileDate >= startOfLastYear)
	{
		return ResourceHelper::LoadString(m_hResourceModule, IDS_GROUPBY_DATE_LAST_YEAR);
	}

	return ResourceHelper::LoadString(m_hResourceModule, IDS_GROUPBY_DATE_LONG_AGO);
}

std::wstring CShellBrowser::DetermineItemSummaryGroup(const BasicItemInfo_t &itemInfo,
	const SHCOLUMNID *pscid, const GlobalFolderSettings &globalFolderSettings) const
{
	TCHAR szDetail[512];
	HRESULT hr = GetItemDetails(itemInfo, pscid, szDetail, SIZEOF_ARRAY(szDetail), globalFolderSettings);

	if(SUCCEEDED(hr) && lstrlen(szDetail) > 0)
	{
		return szDetail;
	}
	else
	{
		return L"Unspecified";
	}
}

/* TODO: Need to sort based on percentage free. */
std::wstring CShellBrowser::DetermineItemFreeSpaceGroup(int iItemInternal) const
{
	std::list<TypeGroup_t>::iterator itr;
	TCHAR szFreeSpace[MAX_PATH];
	IShellFolder *pShellFolder	= NULL;
	PCITEMID_CHILD pidlRelative	= NULL;
	STRRET str;
	TCHAR szItem[MAX_PATH];
	ULARGE_INTEGER nTotalBytes;
	ULARGE_INTEGER nFreeBytes;
	BOOL bRoot;
	BOOL bRes = FALSE;

	SHBindToParent(m_itemInfoMap.at(iItemInternal).pidlComplete.get(),
		IID_PPV_ARGS(&pShellFolder), &pidlRelative);

	pShellFolder->GetDisplayNameOf(pidlRelative,SHGDN_FORPARSING,&str);
	StrRetToBuf(&str,pidlRelative,szItem,SIZEOF_ARRAY(szItem));

	pShellFolder->Release();

	bRoot = PathIsRoot(szItem);

	if(bRoot)
	{
		bRes = GetDiskFreeSpaceEx(szItem,NULL,&nTotalBytes,&nFreeBytes);

		LARGE_INTEGER lDiv1;
		LARGE_INTEGER lDiv2;

		lDiv1.QuadPart = 100;
		lDiv2.QuadPart = 10;

		/* Divide by 10 to remove the one's digit, then multiply
		by 10 so that only the ten's digit rmains. */
		StringCchPrintf(szFreeSpace,SIZEOF_ARRAY(szFreeSpace),
			_T("%I64d%% free"),(((nFreeBytes.QuadPart * lDiv1.QuadPart) / nTotalBytes.QuadPart) / lDiv2.QuadPart) * lDiv2.QuadPart);
	}
	
	if(!bRoot || !bRes)
	{
		StringCchCopy(szFreeSpace,SIZEOF_ARRAY(szFreeSpace),_T("Unspecified"));
	}

	return szFreeSpace;
}

std::wstring CShellBrowser::DetermineItemAttributeGroup(int iItemInternal) const
{
	TCHAR FullFileName[MAX_PATH];
	std::list<TypeGroup_t>::iterator itr;
	TCHAR szAttributes[32];

	StringCchCopy(FullFileName,SIZEOF_ARRAY(FullFileName),m_CurDir);
	PathAppend(FullFileName,m_itemInfoMap.at(iItemInternal).wfd.cFileName);

	BuildFileAttributeString(FullFileName,szAttributes,
		SIZEOF_ARRAY(szAttributes));

	return szAttributes;
}

std::wstring CShellBrowser::DetermineItemOwnerGroup(int iItemInternal) const
{
	TCHAR FullFileName[MAX_PATH];
	std::list<TypeGroup_t>::iterator itr;
	TCHAR szOwner[512];

	StringCchCopy(FullFileName,SIZEOF_ARRAY(FullFileName),m_CurDir);
	PathAppend(FullFileName,m_itemInfoMap.at(iItemInternal).wfd.cFileName);

	BOOL ret = GetFileOwner(FullFileName,szOwner,SIZEOF_ARRAY(szOwner));

	if(!ret)
	{
		StringCchCopy(szOwner,SIZEOF_ARRAY(szOwner),EMPTY_STRING);
	}

	return szOwner;
}

std::wstring CShellBrowser::DetermineItemVersionGroup(int iItemInternal,TCHAR *szVersionType) const
{
	BOOL bGroupFound = FALSE;
	TCHAR FullFileName[MAX_PATH];
	std::list<TypeGroup_t>::iterator itr;
	TCHAR szVersion[512];
	BOOL bVersionInfoObtained;

	StringCchCopy(FullFileName,SIZEOF_ARRAY(FullFileName),m_CurDir);
	PathAppend(FullFileName,m_itemInfoMap.at(iItemInternal).wfd.cFileName);

	bVersionInfoObtained = GetVersionInfoString(FullFileName,
		szVersionType,szVersion,SIZEOF_ARRAY(szVersion));

	bGroupFound = FALSE;

	if(!bVersionInfoObtained)
		StringCchCopy(szVersion,SIZEOF_ARRAY(szVersion),_T("Unspecified"));

	return szVersion;
}

std::wstring CShellBrowser::DetermineItemCameraPropertyGroup(int iItemInternal,PROPID PropertyId) const
{
	TCHAR szFullFileName[MAX_PATH];
	std::list<TypeGroup_t>::iterator itr;
	TCHAR szProperty[512];
	BOOL bRes;

	StringCchCopy(szFullFileName,SIZEOF_ARRAY(szFullFileName),m_CurDir);
	PathAppend(szFullFileName,m_itemInfoMap.at(iItemInternal).wfd.cFileName);

	bRes = ReadImageProperty(szFullFileName,PropertyId,szProperty,
		SIZEOF_ARRAY(szProperty));

	if(!bRes)
		StringCchCopy(szProperty,SIZEOF_ARRAY(szProperty),_T("Other"));

	return szProperty;
}

std::wstring CShellBrowser::DetermineItemExtensionGroup(int iItemInternal) const
{
	if ((m_itemInfoMap.at(iItemInternal).wfd.dwFileAttributes & FILE_ATTRIBUTE_DIRECTORY) == FILE_ATTRIBUTE_DIRECTORY)
	{
		return ResourceHelper::LoadString(m_hResourceModule, IDS_GROUPBY_EXTENSION_FOLDER);
	}

	TCHAR FullFileName[MAX_PATH];
	StringCchCopy(FullFileName,SIZEOF_ARRAY(FullFileName),m_CurDir);
	PathAppend(FullFileName,m_itemInfoMap.at(iItemInternal).wfd.cFileName);

	TCHAR *pExt = PathFindExtension(FullFileName);

	if(*pExt == '\0')
	{
		return ResourceHelper::LoadString(m_hResourceModule, IDS_GROUPBY_EXTENSION_NONE);
	}
	else
	{
		return pExt;
	}
}

std::wstring CShellBrowser::DetermineItemFileSystemGroup(int iItemInternal) const
{
	IShellFolder *pShellFolder	= NULL;
	PCITEMID_CHILD pidlRelative	= NULL;
	TCHAR szFileSystemName[MAX_PATH];
	TCHAR szItem[MAX_PATH];
	STRRET str;
	BOOL bRoot;
	BOOL bRes;

	SHBindToParent(m_itemInfoMap.at(iItemInternal).pidlComplete.get(),
		IID_PPV_ARGS(&pShellFolder), &pidlRelative);

	pShellFolder->GetDisplayNameOf(pidlRelative,SHGDN_FORPARSING,&str);
	StrRetToBuf(&str,pidlRelative,szItem,SIZEOF_ARRAY(szItem));

	bRoot = PathIsRoot(szItem);

	if(bRoot)
	{
		bRes = GetVolumeInformation(szItem,NULL,0,NULL,NULL,NULL,szFileSystemName,
			SIZEOF_ARRAY(szFileSystemName));

		if(!bRes || *szFileSystemName == '\0')
		{
			LoadString(m_hResourceModule, IDS_GROUPBY_UNSPECIFIED, szFileSystemName, SIZEOF_ARRAY(szFileSystemName));
		}
	}
	else
	{
		LoadString(m_hResourceModule, IDS_GROUPBY_UNSPECIFIED, szFileSystemName, SIZEOF_ARRAY(szFileSystemName));
	}

	pShellFolder->Release();

	return szFileSystemName;
}

/* TODO: Fix. Need to check for each adapter. */
std::wstring CShellBrowser::DetermineItemNetworkStatus(int iItemInternal) const
{
	/* When this function is
	properly implemented, this
	can be removed. */
	UNREFERENCED_PARAMETER(iItemInternal);

	std::list<TypeGroup_t>::iterator itr;

	TCHAR szStatus[32] = EMPTY_STRING;
	IP_ADAPTER_ADDRESSES *pAdapterAddresses = NULL;
	UINT uStatusID = 0;
	ULONG ulOutBufLen = 0;

	GetAdaptersAddresses(AF_UNSPEC,0,NULL,NULL,&ulOutBufLen);

	pAdapterAddresses = (IP_ADAPTER_ADDRESSES *)malloc(ulOutBufLen);

	GetAdaptersAddresses(AF_UNSPEC,0,NULL,pAdapterAddresses,&ulOutBufLen);

	/* TODO: These strings need to be setup correctly. */
	/*switch(pAdapterAddresses->OperStatus)
	{
		case IfOperStatusUp:
			uStatusID = IDS_NETWORKADAPTER_CONNECTED;
			break;

		case IfOperStatusDown:
			uStatusID = IDS_NETWORKADAPTER_DISCONNECTED;
			break;

		case IfOperStatusTesting:
			uStatusID = IDS_NETWORKADAPTER_TESTING;
			break;

		case IfOperStatusUnknown:
			uStatusID = IDS_NETWORKADAPTER_UNKNOWN;
			break;

		case IfOperStatusDormant:
			uStatusID = IDS_NETWORKADAPTER_DORMANT;
			break;

		case IfOperStatusNotPresent:
			uStatusID = IDS_NETWORKADAPTER_NOTPRESENT;
			break;

		case IfOperStatusLowerLayerDown:
			uStatusID = IDS_NETWORKADAPTER_LOWLAYER;
			break;
	}*/

	LoadString(m_hResourceModule,uStatusID,
		szStatus,SIZEOF_ARRAY(szStatus));

	return szStatus;
}

void CShellBrowser::InsertItemIntoGroup(int iItem,int iGroupId)
{
	LVITEM Item;

	/* Move the item into the group. */
	Item.mask		= LVIF_GROUPID;
	Item.iItem		= iItem;
	Item.iSubItem	= 0;
	Item.iGroupId	= iGroupId;
	ListView_SetItem(m_hListView,&Item);
}

void CShellBrowser::MoveItemsIntoGroups(void)
{
	LVITEM Item;
	int nItems;
	int iGroupId;
	int i = 0;

	ListView_RemoveAllGroups(m_hListView);
	ListView_EnableGroupView(m_hListView,TRUE);

	nItems = ListView_GetItemCount(m_hListView);

	SendMessage(m_hListView,WM_SETREDRAW,(WPARAM)FALSE,(LPARAM)NULL);

	m_GroupList.clear();
	m_iGroupId = 0;

	for(i = 0;i < nItems ;i++)
	{
		Item.mask		= LVIF_PARAM;
		Item.iItem		= i;
		Item.iSubItem	= 0;
		ListView_GetItem(m_hListView,&Item);

		iGroupId = DetermineItemGroup((int)Item.lParam);

		InsertItemIntoGroup(i,iGroupId);
	}

	SendMessage(m_hListView,WM_SETREDRAW,(WPARAM)TRUE,(LPARAM)NULL);
}