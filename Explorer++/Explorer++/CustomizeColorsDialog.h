// Copyright (C) Explorer++ Project
// SPDX-License-Identifier: GPL-3.0-only
// See LICENSE in the top level directory

#pragma once

#include "ColorRuleHelper.h"
#include "CoreInterface.h"
#include "Explorer++_internal.h"
#include "../Helper/BaseDialog.h"
#include "../Helper/DialogSettings.h"
#include "../Helper/ResizableDialog.h"
#include <vector>

class CustomizeColorsDialog;

class CustomizeColorsDialogPersistentSettings : public DialogSettings
{
public:

	static CustomizeColorsDialogPersistentSettings &GetInstance();

private:

	friend CustomizeColorsDialog;

	static const TCHAR SETTINGS_KEY[];

	CustomizeColorsDialogPersistentSettings();

	CustomizeColorsDialogPersistentSettings(const CustomizeColorsDialogPersistentSettings &);
	CustomizeColorsDialogPersistentSettings & operator=(const CustomizeColorsDialogPersistentSettings &);
};

class CustomizeColorsDialog : public BaseDialog
{
public:

	CustomizeColorsDialog(HINSTANCE hInstance, HWND hParent, IExplorerplusplus *expp,
		std::vector<NColorRuleHelper::ColorRule_t> *pColorRuleList);

protected:

	INT_PTR	OnInitDialog();
	INT_PTR	OnCommand(WPARAM wParam,LPARAM lParam);
	INT_PTR	OnNotify(NMHDR *pnmhdr);
	INT_PTR	OnClose();

	virtual wil::unique_hicon GetDialogIcon(int iconWidth, int iconHeight) const override;

private:

	void	GetResizableControlInformation(BaseDialog::DialogSizeConstraint &dsc, std::list<ResizableDialog::Control_t> &ControlList);
	void	SaveState();

	void	OnNew();
	void	OnEdit();
	void	InsertColorRuleIntoListView(HWND hListView,const NColorRuleHelper::ColorRule_t &ColorRule,int iIndex);
	void	EditColorRule(int iSelected);
	void	OnMove(BOOL bUp);
	void	OnDelete();

	void	OnOk();
	void	OnCancel();

	IExplorerplusplus *m_expp;

	std::vector<NColorRuleHelper::ColorRule_t> *m_pColorRuleList;

	CustomizeColorsDialogPersistentSettings *m_pccdps;
};