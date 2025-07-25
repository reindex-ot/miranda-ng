/*

Miranda NG: the free IM client for Microsoft* Windows*

Copyright (C) 2012-25 Miranda NG team,
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

/////////////////////////////////////////////////////////////////////////////////////////
// SRMM log container

#include "stdafx.h"
#include "chat.h"
#include "file.h"

#define EVENTTYPE_STATUSCHANGE 25368
#define EVENTTYPE_ERRMSG 25366

static int OnRedrawLog(void *pObj, WPARAM hContact, LPARAM hDbEvent)
{
	DB::EventInfo dbei(hDbEvent);
	if (dbei && dbei.eventType == EVENTTYPE_FILE) {
		DB::FILE_BLOB blob(dbei);
		if (!blob.isCompleted())
			return 0;
	}

	auto *pLog = (CRtfLogWindow *)pObj;
	auto &pDlg = pLog->GetDialog();

	if (pDlg.m_hContact == hContact)
		pDlg.ScheduleRedrawLog();
	else if (auto hParent = db_mc_getMeta(hContact))
		if (pDlg.m_hContact == hParent)
			pDlg.ScheduleRedrawLog();

	return 0;
}

CRtfLogWindow::CRtfLogWindow(CMsgDialog &pDlg) :
	CSrmmLogWindow(pDlg),
	m_rtf(*(CCtrlRichEdit*)pDlg.FindControl(IDC_SRMM_LOG))
{
	hevEdited = HookEventObj(ME_DB_EVENT_EDITED, OnRedrawLog, this);
	hevDelete = HookEventObj(ME_DB_EVENT_DELETED, OnRedrawLog, this);
}

CRtfLogWindow::~CRtfLogWindow()
{
	UnhookEvent(hevEdited);
	UnhookEvent(hevDelete);
}

/////////////////////////////////////////////////////////////////////////////////////////

EXTERN_C MIR_APP_DLL(LRESULT) CALLBACK stubLogProc(HWND hwnd, UINT msg, WPARAM wParam, LPARAM lParam)
{
	CRtfLogWindow *pLog = (CRtfLogWindow *)GetWindowLongPtr(hwnd, GWLP_USERDATA);
	if (pLog != nullptr)
		return pLog->WndProc(msg, wParam, lParam);

	return mir_callNextSubclass(hwnd, stubLogProc, msg, wParam, lParam);
}

void CRtfLogWindow::Attach()
{
	SetWindowLongPtr(m_rtf.GetHwnd(), GWLP_USERDATA, LPARAM(this));
	m_rtf.SetReadOnly(true);

	mir_subclassWindow(m_rtf.GetHwnd(), stubLogProc);
}

/////////////////////////////////////////////////////////////////////////////////////////

void CRtfLogWindow::CreateRtfTail(RtfLogStreamData *dat)
{
	dat->buf = "}";
}

/////////////////////////////////////////////////////////////////////////////////////////

void CRtfLogWindow::Detach()
{
	mir_unsubclassWindow(m_rtf.GetHwnd(), stubLogProc);
}

/////////////////////////////////////////////////////////////////////////////////////////

bool CRtfLogWindow::AtBottom()
{
	if (!(GetWindowLongPtr(m_rtf.GetHwnd(), GWL_STYLE) & WS_VSCROLL))
		return true;

	SCROLLINFO si = {};
	si.cbSize = sizeof(si);
	si.fMask = SIF_PAGE | SIF_RANGE | SIF_POS;
	GetScrollInfo(m_rtf.GetHwnd(), SB_VERT, &si);
	return (si.nPos + (int)si.nPage + 5) >= si.nMax;
}

void CRtfLogWindow::Clear()
{
	m_rtf.SetText(L"");
}

HWND CRtfLogWindow::GetHwnd()
{
	return m_rtf.GetHwnd();
}

int CRtfLogWindow::GetType()
{
	return 0;
}

/////////////////////////////////////////////////////////////////////////////////////////

static DWORD CALLBACK StreamOutCallback(DWORD_PTR dwCookie, LPBYTE pbBuff, LONG cb, LONG *pcb)
{
	CMStringW *str = (CMStringW *)dwCookie;
	str->Append((wchar_t*)pbBuff, cb / 2);
	*pcb = cb;
	return 0;
}

wchar_t* CRtfLogWindow::GetSelectedText()
{
	CHARRANGE sel;
	SendMessage(m_rtf.GetHwnd(), EM_EXGETSEL, 0, (LPARAM)&sel);
	if (sel.cpMin == sel.cpMax)
		return nullptr;

	CMStringW result;

	EDITSTREAM stream;
	memset(&stream, 0, sizeof(stream));
	stream.pfnCallback = StreamOutCallback;
	stream.dwCookie = (DWORD_PTR)&result;
	SendMessage(m_rtf.GetHwnd(), EM_STREAMOUT, SF_TEXT | SF_UNICODE | SFF_SELECTION, (LPARAM)&stream);
	return result.Detach();
}

/////////////////////////////////////////////////////////////////////////////////////////

void CRtfLogWindow::InsertFileLink(CMStringA &buf, MEVENT hEvent, const DB::FILE_BLOB &blob)
{
	buf.Append("{\\field{\\*\\fldinst HYPERLINK \"");
	buf.AppendFormat("ofile:%ul", hEvent);
	buf.Append("\"}{\\fldrslt{\\ul ");
	AppendUnicodeString(buf, blob.getName() ? blob.getName() : TranslateT("Unnamed"));
	buf.Append("}}}");

	if (auto *pwszDescr = blob.getDescr()) {
		buf.Append(" - ");
		AppendUnicodeString(buf, pwszDescr);
	}

	if (blob.getSize() > 0 && blob.getSize() == blob.getTransferred())
		buf.Append(" \\u10004? ");

	int64_t size = blob.getSize();
	if (size > 0)
		buf.AppendFormat(" %uKB", size < 1024 ? 1 : unsigned(blob.getSize() / 1024));

	CMStringA szHost;
	if (const char *b = strstr(blob.getUrl(), "://"))
		for (b = b + 3; *b != 0 && *b != '/' && *b != ':'; b++)
			szHost.AppendChar(*b);

	if (!szHost.IsEmpty()) {
		buf.AppendChar(' ');
		AppendUnicodeString(buf, TranslateT("at"));
		buf.AppendFormat(" %s", szHost.c_str());
	}
}

/////////////////////////////////////////////////////////////////////////////////////////

INT_PTR CRtfLogWindow::Notify(WPARAM, LPARAM lParam)
{
	LPNMHDR hdr = (LPNMHDR)lParam;
	if (hdr->code != EN_LINK)
		return FALSE;

	ENLINK *pLink = (ENLINK *)lParam;
	switch (pLink->msg) {
	case WM_SETCURSOR:
		SetCursor(g_hCurHyperlinkHand);
		SetWindowLongPtr(m_pDlg.m_hwnd, DWLP_MSGRESULT, TRUE);
		return TRUE;

	case WM_RBUTTONDOWN:
	case WM_LBUTTONUP:
	case WM_LBUTTONDBLCLK:
		CHARRANGE sel;
		m_rtf.SendMsg(EM_EXGETSEL, 0, (LPARAM)&sel);
		if (sel.cpMin != sel.cpMax)
			break;

		CMStringW wszText(' ', pLink->chrg.cpMax - pLink->chrg.cpMin + 1);

		TEXTRANGE tr;
		tr.chrg = pLink->chrg;
		tr.lpstrText = wszText.GetBuffer();
		m_rtf.SendMsg(EM_GETTEXTRANGE, 0, (LPARAM)&tr);
		if (wcschr(tr.lpstrText, '@') != nullptr && wcschr(tr.lpstrText, ':') == nullptr && wcschr(tr.lpstrText, '/') == nullptr)
			wszText.Insert(0, L"mailto:");

		MEVENT hDbEvent;
		if (swscanf(tr.lpstrText, L"ofile:%u", &hDbEvent) == 1) {
			DB::EventInfo dbei(hDbEvent);
			if (!dbei)
				return FALSE;
			
			DB::FILE_BLOB blob(dbei);
			int nCmd = 2;
			if (pLink->msg == WM_RBUTTONDOWN) {
				HMENU hMenu = CreatePopupMenu();
				// AppendMenu(hMenu, MF_STRING | MF_GRAYED, 1, TranslateT("Get size"));
				AppendMenu(hMenu, MF_STRING, 3, TranslateT("Download"));
				if (blob.getUrl() != nullptr)
					AppendMenu(hMenu, MF_STRING, 4, TranslateT("Copy URL"));
				AppendMenu(hMenu, MF_STRING, 5, TranslateT("Save as"));

				POINT pt = { GET_X_LPARAM(pLink->lParam), GET_Y_LPARAM(pLink->lParam) };
				ClientToScreen(((NMHDR *)lParam)->hwndFrom, &pt);
				nCmd = TrackPopupMenu(hMenu, TPM_RETURNCMD, pt.x, pt.y, 0, m_pDlg.m_hwnd, nullptr);
				DestroyMenu(hMenu);
			}

			switch (nCmd) {
			case 2:
			case 3:
				DownloadOfflineFile(m_pDlg.m_hContact, hDbEvent, dbei, (nCmd == 2) ? OFD_RUN : OFD_DOWNLOAD, new OFD_Download());
				break;

			case 4:
				{
					OFDTHREAD *ofd = new OFDTHREAD(m_pDlg.m_hContact, hDbEvent, L"", OFD_COPYURL);
					ofd->pCallback = new OFD_CopyUrl(blob.getUrl());
					CallProtoService(dbei.szModule, PS_OFFLINEFILE, (WPARAM)ofd);
				}
				break;

			case 5:
				Srmm_DownloadOfflineFile(m_pDlg.m_hContact, hDbEvent, OFD_RUN | OFD_SAVEAS);
				break;
			}

			return TRUE;
		}

		if (pLink->msg == WM_RBUTTONDOWN) {
			HMENU hMenu = LoadMenu(g_plugin.getInst(), MAKEINTRESOURCE(IDR_CONTEXT));
			HMENU hSubMenu = GetSubMenu(hMenu, 1);
			TranslateMenu(hSubMenu);

			POINT pt = { GET_X_LPARAM(pLink->lParam), GET_Y_LPARAM(pLink->lParam) };
			ClientToScreen(((NMHDR *)lParam)->hwndFrom, &pt);

			switch (TrackPopupMenu(hSubMenu, TPM_RETURNCMD, pt.x, pt.y, 0, m_pDlg.m_hwnd, nullptr)) {
			case IDM_OPENLINK:
				Utils_OpenUrlW(wszText);
				break;

			case IDM_COPYLINK:
				Utils_ClipboardCopy(MClipUnicode(wszText));
				break;
			}

			DestroyMenu(hMenu);
			SetWindowLongPtr(m_pDlg.m_hwnd, DWLP_MSGRESULT, TRUE);
			return TRUE;
		}

		Utils_OpenUrlW(wszText);
		SetFocus(m_pDlg.m_message.GetHwnd());
	}

	return FALSE;
}

void CRtfLogWindow::Resize()
{
	bool bottomScroll = m_pDlg.isChat() ? AtBottom() : true;

	// ::MoveWindow(m_rtf.GetHwnd(), x, y, cx, cy, true);

	if (bottomScroll)
		ScrollToBottom();
}

void CRtfLogWindow::ScrollToBottom()
{
	if (!(GetWindowLongPtr(m_rtf.GetHwnd(), GWL_STYLE) & WS_VSCROLL))
		return;

	SCROLLINFO si = {};
	si.cbSize = sizeof(si);
	si.fMask = SIF_PAGE | SIF_RANGE;
	GetScrollInfo(m_rtf.GetHwnd(), SB_VERT, &si);

	si.fMask = SIF_POS;
	si.nPos = si.nMax - si.nPage;
	SetScrollInfo(m_rtf.GetHwnd(), SB_VERT, &si, TRUE);
	m_rtf.SendMsg(WM_VSCROLL, MAKEWPARAM(SB_BOTTOM, 0), 0);
}

/////////////////////////////////////////////////////////////////////////////////////////

#define EVENTTYPE_JABBER_CHATSTATES     2000
#define EVENTTYPE_JABBER_PRESENCE       2001

static bool CreateRtfFromDbEvent(RtfLogStreamData *dat)
{
	DB::EventInfo dbei(dat->hDbEvent);
	if (!dbei)
		return false;

	if (!dat->pLog->CreateRtfEvent(dat, dbei))
		return false;

	if (!dbei.bSent) {
		if (dbei.isSrmm())
			dat->pLog->GetDialog().MarkEventRead(dbei);

		else if (dbei.eventType == EVENTTYPE_FILE) {
			DB::FILE_BLOB blob(dbei);
			if (blob.isOffline())
				dat->pLog->GetDialog().MarkEventRead(dbei);
		}
	}
	else if (dbei.eventType == EVENTTYPE_JABBER_CHATSTATES || dbei.eventType == EVENTTYPE_JABBER_PRESENCE) {
		db_event_markRead(dat->hContact, dat->hDbEvent);
	}

	return true;
}

static DWORD CALLBACK LogStreamInEvents(DWORD_PTR dwCookie, LPBYTE pbBuff, LONG cb, LONG *pcb)
{
	auto *dat = (RtfLogStreamData *)dwCookie;

	if (dat->buf.IsEmpty()) {
		switch (dat->iStage) {
		case STREAMSTAGE_HEADER:
			dat->pLog->CreateRtfHeader(dat);
			dat->iStage = STREAMSTAGE_EVENTS;
			break;

		case STREAMSTAGE_EVENTS:
			if (dat->dbei) {
				if (dat->pLog->CreateRtfEvent(dat, *dat->dbei)) {
					dat->eventsToInsert++;
					break;
				}
			}
			else if (dat->eventsToInsert) {
				bool bOk;
				do {
					bOk = CreateRtfFromDbEvent(dat);
					if (bOk)
						dat->hDbEventLast = dat->hDbEvent;

					dat->hDbEvent = db_event_next(dat->hContact, dat->hDbEvent);
					if (--dat->eventsToInsert == 0)
						break;
				} while (!bOk && dat->hDbEvent);

				if (bOk) {
					dat->isEmpty = false;
					break;
				}
			}
			dat->iStage = STREAMSTAGE_TAIL;
			__fallthrough;

		case STREAMSTAGE_TAIL:
			dat->pLog->CreateRtfTail(dat);
			dat->iStage = STREAMSTAGE_STOP;
			break;

		case STREAMSTAGE_STOP:
			*pcb = 0;
			return 0;
		}
	}

	*pcb = min(cb, dat->buf.GetLength());
	memcpy(pbBuff, dat->buf.GetBuffer(), *pcb);
	if (dat->buf.GetLength() == *pcb)
		dat->buf.Empty();
	else
		dat->buf.Delete(0, *pcb);

	return 0;
}

void CRtfLogWindow::StreamRtfEvents(RtfLogStreamData *dat, bool bAppend)
{
	if (Contact::IsGroupChat(dat->hContact)) {
		if (auto *si = SM_FindSessionByContact(dat->hContact)) {
			bool bDone = false;

			while (dat->hDbEvent && dat->eventsToInsert) {
				Chat_EventToGC(si, dat->hDbEvent);
				dat->hDbEvent = db_event_next(dat->hContact, dat->hDbEvent);
				dat->eventsToInsert--;
				bDone = true;
			}

			if (bDone && si->pDlg)
				si->pDlg->RedrawLog();
		}
	}
	else {
		EDITSTREAM stream = {};
		stream.pfnCallback = LogStreamInEvents;
		stream.dwCookie = (DWORD_PTR)dat;
		m_rtf.SendMsg(EM_STREAMIN, bAppend ? SFF_SELECTION | SF_RTF : SF_RTF, (LPARAM)&stream);
	}
}

/////////////////////////////////////////////////////////////////////////////////////////

static wchar_t szTrimString[] = L":;,.!?\'\"><()[]- \r\n";

INT_PTR CRtfLogWindow::WndProc(UINT msg, WPARAM wParam, LPARAM lParam)
{
	CHARRANGE sel;

	switch (msg) {
	case WM_ACTIVATE:
		if (LOWORD(wParam) == WA_INACTIVE) {
			m_rtf.SendMsg(EM_EXGETSEL, 0, (LPARAM)&sel);
			if (sel.cpMin != sel.cpMax) {
				sel.cpMin = sel.cpMax;
				m_rtf.SendMsg(EM_EXSETSEL, 0, (LPARAM)&sel);
			}
		}
		break;

	case WM_SETCURSOR:
		if (m_pDlg.m_bInMenu) {
			SetCursor(LoadCursor(nullptr, IDC_ARROW));
			return TRUE;
		}
		break;

	case WM_KEYDOWN:
	case WM_SYSKEYDOWN:
		if (!(GetKeyState(VK_RMENU) & 0x8000)) {
			MSG message = { m_pDlg.m_hwnd, msg, wParam, lParam };
			LRESULT iButtonFrom = Hotkey_Check(&message, BB_HK_SECTION);
			if (iButtonFrom) {
				m_pDlg.ProcessToolbarHotkey(iButtonFrom);
				return TRUE;
			}
		}
		break;

	case WM_CHAR:
		if (wParam >= ' ') {
			SetFocus(m_pDlg.m_message.GetHwnd());
			m_pDlg.m_message.SendMsg(WM_CHAR, wParam, lParam);
		}
		else if (wParam == '\t')
			SetFocus(m_pDlg.m_message.GetHwnd());
		break;

	case WM_CONTEXTMENU:
		POINT pt, ptl;
		m_rtf.SendMsg(EM_EXGETSEL, 0, (LPARAM)&sel);
		if (lParam == 0xFFFFFFFF) {
			m_rtf.SendMsg(EM_POSFROMCHAR, (WPARAM)&pt, (LPARAM)sel.cpMax);
			ClientToScreen(m_rtf.GetHwnd(), &pt);
		}
		else {
			pt.x = GET_X_LPARAM(lParam);
			pt.y = GET_Y_LPARAM(lParam);
		}
		ptl = pt;
		ScreenToClient(m_rtf.GetHwnd(), &ptl);
		{
			ptrW pszWord;

			HMENU hMenu = LoadMenu(g_plugin.getInst(), MAKEINTRESOURCE(IDR_LOGMENU));
			HMENU hSubMenu = GetSubMenu(hMenu, 0);
			TranslateMenu(hSubMenu);

			// get a word under cursor
			if (sel.cpMin == sel.cpMax) {
				ModifyMenuW(hSubMenu, IDM_COPY, MF_BYCOMMAND | MF_STRING, IDM_COPYALL, TranslateT("Copy all"));

				int iCharIndex = m_rtf.SendMsg(EM_CHARFROMPOS, 0, (LPARAM)&ptl);
				if (iCharIndex < 0)
					break;

				sel.cpMin = m_rtf.SendMsg(EM_FINDWORDBREAK, WB_LEFT, iCharIndex);
				sel.cpMax = m_rtf.SendMsg(EM_FINDWORDBREAK, WB_RIGHT, iCharIndex);
			}

			if (sel.cpMax > sel.cpMin) {
				pszWord = (wchar_t*)mir_alloc(sizeof(wchar_t) * (sel.cpMax - sel.cpMin + 1));

				TEXTRANGE tr = {};
				tr.chrg = sel;
				tr.lpstrText = pszWord;
				int iRes = m_rtf.SendMsg(EM_GETTEXTRANGE, 0, (LPARAM)&tr);
				if (iRes > 0) {
					if (wchar_t *p = wcschr(pszWord, '\r'))
						*p = 0;

					int iLen = (int)mir_wstrlen(pszWord) - 1;
					while (iLen >= 0) {
						if (!wcschr(szTrimString, pszWord[iLen]))
							break;
						pszWord[iLen--] = '\0';
					}

					if (iLen > 0) {
						CMStringW wszText(FORMAT, TranslateT("Look up '%s':"), pszWord);
						if (wszText.GetLength() > 30) {
							wszText.Truncate(30);
							wszText.Append(L"\u2026'");
						}
						ModifyMenu(hSubMenu, 4, MF_STRING | MF_BYPOSITION, 4, wszText);
					}
				}
			}

			CHARRANGE all = { 0, -1 };
			m_pDlg.m_bInMenu = true;

			int flags = MF_BYPOSITION | (GetRichTextLength(m_rtf.GetHwnd()) == 0 ? MF_GRAYED : MF_ENABLED);
			EnableMenuItem(hSubMenu, 0, flags);
			EnableMenuItem(hSubMenu, 2, flags);

			Chat_CreateMenu(hSubMenu, m_pDlg.m_si, nullptr);
			UINT uID = TrackPopupMenu(hSubMenu, TPM_RETURNCMD | TPM_RIGHTBUTTON, pt.x, pt.y, 0, m_rtf.GetHwnd(), nullptr);
			m_pDlg.m_bInMenu = false;
			DestroyMenu(hMenu);

			switch (uID) {
			case 0:
				PostMessage(m_pDlg.m_hwnd, WM_MOUSEACTIVATE, 0, 0);
				break;

			case IDM_COPY:
				m_rtf.SendMsg(WM_COPY, 0, 0);
				PostMessage(m_pDlg.m_hwnd, WM_MOUSEACTIVATE, 0, 0);
				break;

			case IDM_COPYALL:
				m_rtf.SendMsg(EM_EXGETSEL, 0, (LPARAM)&sel);
				m_rtf.SendMsg(EM_EXSETSEL, 0, (LPARAM)&all);
				m_rtf.SendMsg(WM_COPY, 0, 0);
				m_rtf.SendMsg(EM_EXSETSEL, 0, (LPARAM)&sel);
				PostMessage(m_pDlg.m_hwnd, WM_MOUSEACTIVATE, 0, 0);
				break;

			case IDM_CLEAR:
				m_rtf.SetText(L"");
				if (auto *si = m_pDlg.m_si) {
					si->arEvents.destroy();
					si->LastTime = 0;
				}
				PostMessage(m_pDlg.m_hwnd, WM_MOUSEACTIVATE, 0, 0);
				break;

			case IDM_SEARCH_GOOGLE:
			case IDM_SEARCH_BING:
			case IDM_SEARCH_YANDEX:
			case IDM_SEARCH_YAHOO:
			case IDM_SEARCH_WIKIPEDIA:
			case IDM_SEARCH_FOODNETWORK:
			case IDM_SEARCH_GOOGLE_MAPS:
			case IDM_SEARCH_GOOGLE_TRANSLATE:
				{
					CMStringW szURL;
					switch (uID) {
					case IDM_SEARCH_WIKIPEDIA:
						szURL.Format(L"https://en.wikipedia.org/wiki/%s", pszWord.get());
						break;
					case IDM_SEARCH_YAHOO:
						szURL.Format(L"https://search.yahoo.com/search?p=%s&ei=UTF-8", pszWord.get());
						break;
					case IDM_SEARCH_FOODNETWORK:
						szURL.Format(L"https://www.foodnetwork.com/search/%s-", pszWord.get());
						break;
					case IDM_SEARCH_BING:
						szURL.Format(L"https://www.bing.com/search?q=%s&form=OSDSRC", pszWord.get());
						break;
					case IDM_SEARCH_GOOGLE_MAPS:
						szURL.Format(L"https://maps.google.com/maps?q=%s&ie=utf-8&oe=utf-8", pszWord.get());
						break;
					case IDM_SEARCH_GOOGLE_TRANSLATE:
						szURL.Format(L"https://translate.google.com/?q=%s&ie=utf-8&oe=utf-8", pszWord.get());
						break;
					case IDM_SEARCH_YANDEX:
						szURL.Format(L"https://yandex.ru/yandsearch?text=%s", pszWord.get());
						break;
					case IDM_SEARCH_GOOGLE:
						szURL.Format(L"https://www.google.com/search?q=%s&ie=utf-8&oe=utf-8", pszWord.get());
						break;
					}
					Utils_OpenUrlW(szURL);
				}
				PostMessage(m_pDlg.m_hwnd, WM_MOUSEACTIVATE, 0, 0);
				break;

			default:
				PostMessage(m_pDlg.m_hwnd, WM_MOUSEACTIVATE, 0, 0);
				Chat_DoEventHook(m_pDlg.m_si, GC_USER_LOGMENU, nullptr, nullptr, uID);
				break;
			}
		}
		return 0;
	}

  	LRESULT res = mir_callNextSubclass(m_rtf.GetHwnd(), stubLogProc, msg, wParam, lParam);
	if (msg == WM_GETDLGCODE)
		return res & ~DLGC_HASSETSEL;
	return res;
}
