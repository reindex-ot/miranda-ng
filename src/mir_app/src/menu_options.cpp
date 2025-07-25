/*

Miranda NG: the free IM client for Microsoft* Windows*

Copyright (C) 2012-25 Miranda NG team (https://miranda-ng.org),
Copyright (c) 2000-12 Miranda IM project,
all portions of this codebase are copyrighted to the people
listed in contributors.txt.

This program is free software; you can redistribute it and/or
modify it under the terms of the GNU General Public License
as published by the Free Software Foundation; either version 2
of the License, or (at your option) any later version.

This program is distributed in the hope that it will be useful,
but WITHOUT ANY WARRANTY; without even the implied warranty of
MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
GNU General Public License for more details.

You should have received a copy of the GNU General Public License
along with this program; if not, write to the Free Software
Foundation, Inc., 59 Temple Place - Suite 330, Boston, MA  02111-1307, USA.
*/

#include "stdafx.h"
#include "genmenu.h"
#include "plugins.h"

#define STR_SEPARATOR L"-----------------------------------"

extern int DefaultImageListColorDepth;
void RebuildProtoMenus();

MIR_APP_DLL(void) Menu_SetVisible(TMO_IntMenuItem *pimi, bool bVisible)
{
	if ((pimi = MO_GetIntMenuItem(pimi)) == nullptr)
		return;

	char menuItemName[256];
	auto szModule(pimi->parent->getModule());
	bin2hex(&pimi->mi.uid, sizeof(pimi->mi.uid), menuItemName);

	ptrW wszValue(db_get_wsa(0, szModule, menuItemName, L"1;;;"));
	wszValue[0] = bVisible ? '1' : '0';
	db_set_ws(0, szModule, menuItemName, wszValue);

	Menu_ShowItem(pimi, bVisible);
}

/////////////////////////////////////////////////////////////////////////////////////////

struct MenuItemOptData : public MZeroedObject
{
	~MenuItemOptData() {}

	int    pos;

	ptrW   name;
	ptrW   defname;
	
	bool   bShow;
	int    id;

	TMO_IntMenuItem *pimi;
};

static int SortMenuItems(const MenuItemOptData *p1, const MenuItemOptData *p2)
{
	if (p1->pos < p2->pos) return -1;
	if (p1->pos > p2->pos) return 1;
	return 0;
}

/////////////////////////////////////////////////////////////////////////////////////////

class CGenMenuOptionsPage : public CDlgBase
{
	bool m_bInitMenuValue;
	LIST<TMO_IntMenuItem> m_arDeleted;

	wchar_t idstr[100];

	void SaveTreeInternal(MenuItemOptData *pParent, HTREEITEM hRootItem, const char *szModule)
	{
		TVITEMEX tvi;
		tvi.hItem = hRootItem;
		tvi.cchTextMax = _countof(idstr);
		tvi.mask = TVIF_TEXT | TVIF_PARAM | TVIF_HANDLE | TVIF_STATE;
		tvi.pszText = idstr;

		int count = 0;
		int runtimepos = 100;

		char pszParent[33];
		if (pParent == nullptr)
			pszParent[0] = 0;
		else
			bin2hex(&pParent->pimi->mi.uid, sizeof(MUUID), pszParent);

		while (tvi.hItem != nullptr) {
			m_menuItems.GetItem(&tvi);

			bool bChecked = (tvi.state >> 12) == 2;
			auto *iod = (MenuItemOptData*)tvi.lParam;
			if (TMO_IntMenuItem *pimi = iod->pimi) {
				if (pimi->mi.uid != miid_last) {
					char menuItemName[256];
					bin2hex(&pimi->mi.uid, sizeof(pimi->mi.uid), menuItemName);

					wchar_t *ptszCustomName;
					if (iod->name != nullptr && iod->defname != nullptr && mir_wstrcmp(iod->name, iod->defname) != 0)
						ptszCustomName = iod->name;
					else
						ptszCustomName = L"";

					CMStringW tszValue(FORMAT, L"%d;%d;%S;%s", bChecked, runtimepos, pszParent, ptszCustomName);
					db_set_ws(0, szModule, menuItemName, tszValue);
				}

				HTREEITEM hChild = m_menuItems.GetChild(tvi.hItem);
				if (hChild != nullptr)
					SaveTreeInternal(iod, hChild, szModule);

				runtimepos += 100;
			}

			if (iod->name && !mir_wstrcmp(iod->name, STR_SEPARATOR) && bChecked)
				runtimepos += SEPARATORPOSITIONINTERVAL;

			tvi.hItem = m_menuItems.GetNextSibling(tvi.hItem);
			count++;
		}
	}

	void SaveTree()
	{
		int MenuObjectId;
		if (!GetCurrentMenuObjectID(MenuObjectId))
			return;

		TIntMenuObject *pmo = GetMenuObjbyId(MenuObjectId);
		if (pmo == nullptr)
			return;

		auto szModule(pmo->getModule());
		db_delete_module(0, szModule);
		SaveTreeInternal(nullptr, m_menuItems.GetRoot(), szModule);
		db_set_b(0, szModule, "MenuFormat", 1);
	}

	void FreeTreeData()
	{
		HTREEITEM hItem = m_menuItems.GetRoot();
		while (hItem != nullptr) {
			TVITEMEX tvi;
			tvi.mask = TVIF_HANDLE | TVIF_PARAM;
			tvi.hItem = hItem;
			m_menuItems.GetItem(&tvi);
			delete (MenuItemOptData *)tvi.lParam;

			tvi.lParam = 0;
			m_menuItems.SetItem(&tvi);

			hItem = m_menuItems.GetNextSibling(hItem);
		}
	}

	void RebuildCurrent()
	{
		int MenuObjectID;
		if (GetCurrentMenuObjectID(MenuObjectID))
			BuildTree(MenuObjectID, true);
	}

	void BuildTreeInternal(const char *pszModule, bool bReread, const TMO_LinkedList &pList, HTREEITEM hRoot)
	{
		LIST<MenuItemOptData> arItems(10, SortMenuItems);

		for (auto &it : pList) {
			// filter out items whose presence & position might not be changed
			if (it->mi.flags & CMIF_SYSTEM)
				continue;

			MenuItemOptData *PD = new MenuItemOptData();
			PD->pimi = it;
			PD->defname = mir_wstrdup(GetMenuItemText(it));
			PD->name = mir_wstrdup((bReread && it->ptszCustomName != nullptr) ? it->ptszCustomName : PD->defname);
			PD->bShow = (it->mi.flags & CMIF_HIDDEN) == 0;
			PD->pos = (bReread) ? it->mi.position : it->originalPosition;
			PD->id = it->iCommand;
			arItems.insert(PD);
		}

		int lastpos = 0;
		bool bIsFirst = TRUE;

		TVINSERTSTRUCT tvis;
		tvis.hParent = hRoot;
		tvis.hInsertAfter = TVI_LAST;
		tvis.item.mask = TVIF_PARAM | TVIF_CHILDREN | TVIF_TEXT | TVIF_STATE;

		for (auto &it : arItems) {
			if (it != arItems[0] && it->pos - lastpos >= SEPARATORPOSITIONINTERVAL) {
				MenuItemOptData *sep = new MenuItemOptData();
				sep->id = -1;
				sep->name = mir_wstrdup(STR_SEPARATOR);
				sep->pos = it->pos - 1;

				tvis.item.lParam = (LPARAM)sep;
				tvis.item.pszText = sep->name;
				tvis.item.cChildren = 0;
				tvis.item.state = INDEXTOSTATEIMAGEMASK(2);
				tvis.item.stateMask = TVIS_STATEIMAGEMASK;
				m_menuItems.InsertItem(&tvis);
			}

			tvis.item.lParam = (LPARAM)it;
			tvis.item.pszText = it->name;
			tvis.item.state = INDEXTOSTATEIMAGEMASK(it->bShow ? 2 : 1);
			tvis.item.stateMask = TVIS_STATEIMAGEMASK;
			tvis.item.cChildren = it->pimi->submenu.getCount() != 0;

			HTREEITEM hti = m_menuItems.InsertItem(&tvis);
			if (bIsFirst) {
				if (hRoot == nullptr)
					m_menuItems.SelectItem(hti);
				bIsFirst = false;
			}

			if (it->pimi->submenu.getCount()) {
				BuildTreeInternal(pszModule, bReread, it->pimi->submenu, hti);
				m_menuItems.Expand(hti, TVE_EXPAND);
			}

			lastpos = it->pos;
		}
	}

	bool BuildTree(int MenuObjectId, bool bReread)
	{
		FreeTreeData();

		TIntMenuObject *pmo = GetMenuObjbyId(MenuObjectId);
		if (!pmo || !pmo->m_items.getCount())
			return false;

		auto szModule(pmo->getModule());

		if (bReread) // no need to reread database on reset
			Menu_LoadAllFromDatabase(pmo->m_items, szModule.c_str());

		m_menuItems.SetDraw(false);
		m_menuItems.DeleteAllItems();
		BuildTreeInternal(szModule, bReread, pmo->m_items, nullptr);
		m_menuItems.SetDraw(true);

		m_menuItems.Enable(pmo->m_bUseUserDefinedItems);
		m_btnInsSeparator.Enable(pmo->m_bUseUserDefinedItems);
		return 1;
	}

	bool GetCurrentMenuObjectID(int &result)
	{
		int iItem = m_menuObjects.GetCurSel();
		if (iItem == -1)
			return false;

		result = (int)m_menuObjects.GetItemData(iItem);
		return true;
	}

	CCtrlListBox m_menuObjects;
	CCtrlTreeView m_menuItems;
	CCtrlCheck m_radio1, m_radio2, m_enableIcons;
	CCtrlEdit m_customName, m_service, m_module, m_id;
	CCtrlButton m_btnInsSeparator, m_btnReset, m_btnSet, m_btnDefault;

public:
	CGenMenuOptionsPage() :
		CDlgBase(g_plugin, IDD_OPT_GENMENU),
		m_arDeleted(1),
		m_menuItems(this, IDC_MENUITEMS),
		m_menuObjects(this, IDC_MENUOBJECTS),
		m_radio1(this, IDC_RADIO1),
		m_radio2(this, IDC_RADIO2),
		m_enableIcons(this, IDC_DISABLEMENUICONS),
		m_btnInsSeparator(this, IDC_INSERTSEPARATOR),
		m_btnReset(this, IDC_RESETMENU),
		m_btnSet(this, IDC_GENMENU_SET),
		m_btnDefault(this, IDC_GENMENU_DEFAULT),
		m_id(this, IDC_GENMENU_ID),
		m_customName(this, IDC_GENMENU_CUSTOMNAME),
		m_service(this, IDC_GENMENU_SERVICE),
		m_module(this, IDC_GENMENU_MODULE)
	{
		m_btnSet.OnClick = Callback(this, &CGenMenuOptionsPage::btnSet_Clicked);
		m_btnReset.OnClick = Callback(this, &CGenMenuOptionsPage::btnReset_Clicked);
		m_btnInsSeparator.OnClick = Callback(this, &CGenMenuOptionsPage::btnInsSep_Clicked);
		m_btnDefault.OnClick = Callback(this, &CGenMenuOptionsPage::btnDefault_Clicked);

		m_menuObjects.OnSelChange = Callback(this, &CGenMenuOptionsPage::onMenuObjectChanged);

		m_menuItems.SetFlags(MTREE_CHECKBOX | MTREE_DND);
		m_menuItems.OnSelChanged = Callback(this, &CGenMenuOptionsPage::onMenuItemChanged);
		m_menuItems.OnBeginDrag = BCallback(this, &CGenMenuOptionsPage::onMenuItemBeginDrag);

		m_customName.SetSilent();
		m_service.SetSilent();
		m_module.SetSilent();
	}

	//---- init dialog -------------------------------------------
	bool OnInitDialog() override
	{
		m_bInitMenuValue = db_get_b(0, "CList", "MoveProtoMenus", true) != 0;
		if (m_bInitMenuValue)
			m_radio2.SetState(true);
		else
			m_radio1.SetState(true);

		m_enableIcons.SetState(g_bMenuIconsEnabled);

		//---- init menu object list --------------------------------------
		for (auto &p : g_menus)
			if (p->id != (int)hStatusMenuObject && p->m_bUseUserDefinedItems)
				m_menuObjects.AddString(TranslateW(p->ptszDisplayName), p->id);
		
		m_menuObjects.SetCurSel(0);
		RebuildCurrent();
		return true;
	}

	bool OnApply() override
	{
		if (g_bMenuIconsEnabled != m_enableIcons.IsChecked()) {
			g_bMenuIconsEnabled = m_enableIcons.IsChecked();
			db_set_b(0, "CList", "DisableMenuIcons", !g_bMenuIconsEnabled);

			Menu_GetMainMenu();
			Menu_GetStatusMenu();
			Menu_ReloadProtoMenus();
		}

		if (m_customName.IsChanged())
			btnSet_Clicked(0);

		SaveTree();

		for (auto &pimi : m_arDeleted)
			Menu_RemoveItem(pimi);

		bool iNewMenuValue = !m_radio1.IsChecked();
		if (iNewMenuValue != m_bInitMenuValue) {
			db_set_b(0, "CList", "MoveProtoMenus", iNewMenuValue);

			RebuildProtoMenus();
			m_bInitMenuValue = iNewMenuValue;
		}
		RebuildCurrent();
		return true;
	}

	void OnDestroy() override
	{
		FreeTreeData();
	}

	void btnInsSep_Clicked(CCtrlButton*)
	{
		HTREEITEM hti = m_menuItems.GetSelection();
		if (hti == nullptr)
			return;

		TVITEMEX tvi = { 0 };
		tvi.mask = TVIF_HANDLE | TVIF_PARAM | TVIF_TEXT | TVIF_STATE;
		tvi.hItem = hti;
		if (!m_menuItems.GetItem(&tvi))
			return;

		MenuItemOptData *PD = new MenuItemOptData();
		PD->id = -1;
		PD->name = mir_wstrdup(STR_SEPARATOR);
		PD->pos = ((MenuItemOptData *)tvi.lParam)->pos - 1;

		TVINSERTSTRUCT tvis = {};
		tvis.hParent = m_menuItems.GetParent(hti);
		tvis.hInsertAfter = hti;
		tvis.item.mask = TVIF_PARAM | TVIF_TEXT | TVIF_STATE;
		tvis.item.lParam = (LPARAM)PD;
		tvis.item.pszText = PD->name;
		tvis.item.state = INDEXTOSTATEIMAGEMASK(2);
		tvis.item.stateMask = TVIS_STATEIMAGEMASK;
		m_menuItems.InsertItem(&tvis);

		NotifyChange();
	}

	void btnReset_Clicked(CCtrlButton*)
	{
		int MenuObjectID;
		if (GetCurrentMenuObjectID(MenuObjectID)) {
			BuildTree(MenuObjectID, false);
			NotifyChange();
		}
	}

	void btnDefault_Clicked(CCtrlButton*)
	{
		HTREEITEM hti = m_menuItems.GetSelection();
		if (hti == nullptr)
			return;

		TVITEMEX tvi;
		tvi.mask = TVIF_HANDLE | TVIF_PARAM;
		tvi.hItem = hti;
		m_menuItems.GetItem(&tvi);

		MenuItemOptData *iod = (MenuItemOptData *)tvi.lParam;
		if (iod->name && wcsstr(iod->name, STR_SEPARATOR))
			return;

		iod->name = mir_wstrdup(iod->defname);
		m_customName.SetText(iod->defname);

		tvi.mask = TVIF_TEXT;
		tvi.pszText = iod->name;
		m_menuItems.SetItem(&tvi);
		NotifyChange();
	}

	void btnSet_Clicked(CCtrlButton*)
	{
		HTREEITEM hti = m_menuItems.GetSelection();
		if (hti == nullptr)
			return;

		TVITEMEX tvi;
		tvi.mask = TVIF_HANDLE | TVIF_PARAM;
		tvi.hItem = hti;
		m_menuItems.GetItem(&tvi);

		MenuItemOptData *iod = (MenuItemOptData *)tvi.lParam;
		if (iod->name && wcsstr(iod->name, STR_SEPARATOR))
			return;

		iod->name = m_customName.GetText();

		tvi.mask = TVIF_TEXT;
		tvi.pszText = iod->name;
		m_menuItems.SetItem(&tvi);
		NotifyChange();
	}

	void onMenuObjectChanged(void*)
	{
		m_bInitialized = false;
		RebuildCurrent();
		m_bInitialized = true;
	}

	void onMenuItemChanged(void*)
	{
		m_customName.SetTextA("");
		m_service.SetTextA("");
		m_module.SetTextA("");
		m_id.SetTextA("");

		m_btnDefault.Disable();
		m_btnSet.Disable();
		m_customName.Disable();

		HTREEITEM hti = m_menuItems.GetSelection();
		if (hti == nullptr)
			return;

		TVITEMEX tvi;
		tvi.mask = TVIF_HANDLE | TVIF_PARAM;
		tvi.hItem = hti;
		m_menuItems.GetItem(&tvi);
		if (tvi.lParam == 0)
			return;

		MenuItemOptData *iod = (MenuItemOptData *)tvi.lParam;
		if (iod->name && wcsstr(iod->name, STR_SEPARATOR))
			return;

		m_customName.SetText(iod->name);

		if (iod->pimi->mi.uid != miid_last) {
			auto &id = iod->pimi->mi.uid;
			char szText[100];
			mir_snprintf(szText, "%08x-%04x-%04x-", id.a, id.b, id.c);
			bin2hex(&iod->pimi->mi.uid.d, sizeof(iod->pimi->mi.uid.d), szText + strlen(szText));
			m_id.SetTextA(szText);
		}

		if (iod->pimi->mi.pszService)
			m_service.SetTextA(iod->pimi->mi.pszService);

		auto *pPlugin = iod->pimi->mi.pPlugin;
		m_module.SetTextA(pPlugin ? pPlugin->getInfo().shortName : "");

		m_btnDefault.Enable(mir_wstrcmp(iod->name, iod->defname) != 0);
		m_btnSet.Enable(true);
		m_customName.Enable(true);
	}

	bool onMenuItemBeginDrag(CCtrlTreeView::TEventInfo *evt)
	{
		MenuItemOptData *p = (MenuItemOptData*)evt->nmtv->itemNew.lParam;
		if (p->pimi != nullptr)
			if (p->pimi->mi.flags & CMIF_UNMOVABLE)
				return false; // reject an attempt to change item's position

		return true;
	}
};

int GenMenuOptInit(WPARAM wParam, LPARAM)
{
	OPTIONSDIALOGPAGE odp = {};
	odp.position = -1000000000;
	odp.szTitle.a = LPGEN("Menus");
	odp.szGroup.a = LPGEN("Customize");
	odp.flags = ODPF_BOLDGROUPS;
	odp.pDialog = new CGenMenuOptionsPage();
	g_plugin.addOptions(wParam, &odp);
	
	return ProtocolOrderOptInit(wParam, 0);
}
