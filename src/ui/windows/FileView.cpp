#include "stdafx.h"
#include "FileView.h"
#include "ItemTreeView.h"
#include "WinVGMRoot.h"
#include <zlib.h>

//CFileView fileView;

CFileView::CFileView()
: SelectedItem(NULL), beginOffset(0)//, colBufOffset(-1)
{
}

LRESULT CFileView::OnCreate(UINT uMsg, WPARAM wParam, LPARAM lParam, BOOL& bHandled)
{
	LRESULT nResult = DefWindowProc(uMsg, wParam, lParam);

	m_fontScreen.CreatePointFont (100, _T ("Courier New"));

	CDCHandle dc = GetDC();
	//CClientDC dc (m_hWnd);
    TEXTMETRIC tm;
	HFONT oldFont = dc.SelectFont(m_fontScreen);
    dc.GetTextMetrics (&tm);
    m_cyScreen = tm.tmHeight + tm.tmExternalLeading;
	m_cxScreen = tm.tmAveCharWidth;
    dc.SelectFont (oldFont);
	InitTextColors();

	ucDrawMode = DRAWMODE_OFFSETS | DRAWMODE_HEX | DRAWMODE_ASCII;

	//SCROLLINFO si;
    //si.cbSize = sizeof(si);
    //si.fMask = SIF_DISABLENOSCROLL;
    //SetScrollInfo( SB_HORZ, &si, TRUE);
    //SetScrollInfo( SB_VERT, &si, TRUE);
    //return 0; 

	bHandled = TRUE;
	return nResult;
}

void CFileView::InitTextColors(void)
{
	//SelectedItem = NULL;
	//ucDrawMode = ((CMainFrame*)AfxGetMainWnd())->GetHexDrawMode();
	TextClr[0] =  RGB(0, 0, 0);   //Black
	TextClr[1] =  RGB(255, 0, 0);		//Red
	TextClr[2] =  RGB(0x0, 0x5E, 0x0);	//Purple CC3299
	TextClr[3] =  RGB(0, 0, 255);	//Blue
	//			RGB(0, 255, 0),		//Green
	//			RGB(0, 0, 255) };		//Blue
	BGTextClr[0] = RGB(255, 255, 255);  //White
	BGTextClr[1] = RGB(255, 0, 0);		//Red
	BGTextClr[2] = RGB(0, 255, 0);		//Green
	BGTextClr[3] = RGB(0, 128, 255);		//Blue
	BGTextClr[4] = RGB(255, 255, 0);	//Yellow
	BGTextClr[5] = RGB(255, 0, 255);	//Magenta
	BGTextClr[6] = RGB(0, 255, 255);	//Cyan
	BGTextClr[7] = RGB(128, 0, 0);	//Dark Red
	BGTextClr[8] = RGB(0, 0, 0);	//Black
	BGTextClr[9] = RGB(0, 64, 255);  //Dark Blue
	BGTextClr[10] = RGB(0x70, 0x93, 0xDB);		//Steel
	BGTextClr[11] = RGB(255, 192, 64); //Cheddar
	BGTextClr[12] = RGB(255, 64, 128); //Pink
	BGTextClr[13] = RGB(0, 128, 255);  //Light blue
	BGTextClr[14] = RGB(0, 224, 128);  //Aqua
	BGTextClr[15] = RGB(255, 128, 128); //Peach  (not Flesh... need to be PC)
	BGTextClr[16] = RGB(0xD8, 0xD8, 0xBF); //Wheat
	//				  RGB(255, 0, 0);		//Bright Red
}

LRESULT CFileView::OnDestroy(UINT /*uMsg*/, WPARAM /*wParam*/, LPARAM /*lParam*/, BOOL& bHandled)
{
	col.clear();

	bHandled = FALSE;
	return 0;
}

LRESULT CFileView::OnSetFocus(UINT /*uMsg*/, WPARAM /*wParam*/, LPARAM /*lParam*/, BOOL& bHandled)
{
	bHandled = FALSE;
	return 0;
}

void CFileView::OnLButtonDown(UINT nFlags, CPoint point)
{
	// TODO: Add your message handler code here and/or call default
	SCROLLINFO info;
	//GetScrollInfo(SB_VERT, &info);
	//int scrollPos = info.nTrackPos;
	CPoint scrollPos;
	if (curFile != NULL)
	{
		GetScrollOffset(scrollPos);
		if ((point.x > GetLeftMostOffset()) && (point.x < GetRightMostOffset()))			//if we are clicking within the hexadecimal area of the the HexView
		{
			ULONG offset = (scrollPos.y + point.y)/m_cyScreen * 16 + beginOffset;
			offset += (point.x - GetLeftMostOffset())/(CHAR_WIDTH*3);		//CHAR_WIDTH*2 because there are 2 characters, and 1 space per byte
			VGMItem* selectedItem = curFile->GetItemFromOffset(offset);
			if (selectedItem)
			{
				winroot.SelectItem(selectedItem);
				//itemTreeView.SelectItem(selectedItem);
				//SelectItem(selectedItem);
				//UpdateStatusBarWithItem(selectedItem);
				curOffset = offset;
			}
		}
	}
	SetFocus();
}

LRESULT CFileView::OnRButtonUp(UINT uMsg, WPARAM wParam, LPARAM lParam, BOOL& bHandled)
{
	SendMessage(this->m_hWnd, WM_LBUTTONDOWN, wParam, lParam);
	bHandled = false;
	return 0;
}



LRESULT CFileView::OnContextMenu(HWND hwndCtrl, CPoint ptClick )
{
	if (!SelectedItem)
		return 0;
	if (ptClick.x == -1 && ptClick.y == -1)
		ptClick = (CPoint) GetMessagePos();

	ScreenToClient(&ptClick);

	//CTreeItem treeitem = HitTest( ptClick, &uFlags );
	
	//if( treeitem == NULL )
	//	return 0;

	//VGMFile* pvgmfile = (VGMFile*)treeitem.GetData();
	ClientToScreen(&ptClick);
	ItemContextMenu(hwndCtrl, ptClick, SelectedItem);

	return 0;
}

	

void CFileView::OnKeyDown(UINT nChar, UINT nRepCnt, UINT nFlags)
{
	// TODO: Add your message handler code here and/or call default
	if (SelectedItem)
	{
		switch (nChar)
		{
		case VK_LEFT:
			curOffset = SelectedItem->dwOffset-1;
			break;
		case VK_RIGHT:
			curOffset = SelectedItem->dwOffset+SelectedItem->unLength;
			break;
		case VK_UP:
			curOffset -= 16;
			break;
		case VK_DOWN:
			curOffset += 16;
			break;
		}
		if (curOffset < 0)
			curOffset = 0;
		else if (curOffset >= endOffset)
			curOffset = endOffset-1;

		VGMItem* temp = curFile->GetItemFromOffset(curOffset);
		if (temp)
		{
			winroot.SelectItem(temp);
			//itemTreeView.SelectItem(temp);
			//SelectItem(temp);
			//UpdateStatusBarWithItem(temp);
		}
	}

//	CScrollView::OnKeyDown(nChar, nRepCnt, nFlags);
}

//void CFileView::OnPaint(HDC hdc)
//LRESULT CFileView::OnPaint(UINT /*uMsg*/, WPARAM /*wParam*/, LPARAM /*lParam*/, BOOL& bHandled)
void CFileView::DoPaint(CDCHandle dc)
{
	//CDCHandle  dc(hdc);
	//CDCHandle dc((HDC)wParam);
	//CPaintDC   dc(m_hWnd);
	
	//CRect rc;
	//GetClientRect ( rc );
	//dc.SaveDC();

    //dc.SetBkColor ( RGB(255,153,0) );
    //dc.SetTextColor ( RGB(0,0,0) );
    //dc.ExtTextOut ( 0, 0, ETO_OPAQUE, rc, _T("BLAH"), 
    //                4, NULL );

    // Restore the DC.
    //dc.RestoreDC(-1);
	if (!curFile)
		return;

	if (m_nLinesTotal != 0) {
		CRect rect;
		dc.GetClipBox (&rect);

		UINT nStart = rect.top / m_cyScreen;
		UINT nEnd = min (m_nLinesTotal - 1,
			(rect.bottom + m_cyScreen - 1) / m_cyScreen);

		HFONT oldFont = dc.SelectFont (m_fontScreen);
		for (UINT i=nStart; i<=nEnd; i++) {
			//CString string;
			//FormatLine (pDoc, i, string);
			WriteLine(i, dc);
			//dc.TextOut (2, (i * m_cyScreen) + 2, string);
		}
		dc.SelectFont (oldFont);
		if (SelectedItem != NULL && (ucDrawMode & DRAWMODE_HEX)>0)
		{
			//if (SelectedItem->dwOffset/16 >= nStart && SelectedItem->dwOffset/16 <= nEnd)
			//{
				int leftMostOffset = GetLeftMostOffset();
				CPen pen;
				pen.CreatePen(PS_SOLID, 4, RGB (255, 0, 0));
				HPEN oldPen = dc.SelectPen (pen);
				BOOL bPoint1Edge;
				BOOL bPoint2Edge = FALSE;
				ULONG selectedItemOffset = SelectedItem->dwOffset - beginOffset;
				ULONG selectedItemLength = SelectedItem->unLength;
				int yOffset1 = ((selectedItemOffset / 16))*m_cyScreen;
				int xOffset1 = (selectedItemOffset % 16)*3*8+leftMostOffset;
				bPoint1Edge = ((selectedItemOffset % 16) == 0);
				int yOffset2 = ((selectedItemOffset+selectedItemLength) / 16)*m_cyScreen;
				int xOffset2 = ((selectedItemOffset+selectedItemLength) % 16)*3*8+leftMostOffset;
				if (((selectedItemOffset+selectedItemLength) % 16) == 0)
				{
					bPoint2Edge = TRUE;
					yOffset2 -= m_cyScreen;
					xOffset2 += 16*3*8;
				}

				if (selectedItemLength <= 16)		//if the length does not exceed 16 bytes, we draw 1 box if it encompasses only 1 row, or 2 separate for 2 rows
				{
					if (yOffset1 == yOffset2)
					{
						dc.MoveTo(xOffset1, yOffset1);
						dc.LineTo(xOffset1, yOffset1+m_cyScreen);
						dc.LineTo(xOffset2, yOffset1+m_cyScreen);
						dc.LineTo(xOffset2, yOffset1);
						dc.LineTo(xOffset1, yOffset1);
					}
					else
					{
						dc.MoveTo(xOffset1, yOffset1);
						dc.LineTo(xOffset1, yOffset1+m_cyScreen);
						dc.LineTo(leftMostOffset+16*3*8, yOffset1+m_cyScreen);
						dc.LineTo(leftMostOffset+16*3*8, yOffset1);
						dc.LineTo(xOffset1, yOffset1);

						dc.MoveTo(leftMostOffset, yOffset2);				//left edge
						dc.LineTo(leftMostOffset, yOffset2+m_cyScreen);
						dc.LineTo(xOffset2, yOffset2+m_cyScreen);
						dc.LineTo(xOffset2, yOffset2);
						dc.LineTo(leftMostOffset, yOffset2);
					}
				}
				else
				{
					//int leftYOffset = yOffset1;
					if (!bPoint1Edge)  //start in upper right corner
					{
	//					leftYOffset += m_cyScreen;		//increase the line on the far left by 1 character
						dc.MoveTo(leftMostOffset+16*3*8, yOffset1);
						dc.LineTo(xOffset1, yOffset1);
						dc.LineTo(xOffset1, yOffset1+m_cyScreen);
						dc.LineTo(leftMostOffset, yOffset1+m_cyScreen);
					}
					else
					{
						dc.MoveTo(leftMostOffset+16*3*8, yOffset1);
						dc.LineTo(leftMostOffset, yOffset1);
					}
					dc.LineTo(leftMostOffset, yOffset2+m_cyScreen);
					//now we are at the lower left corner
					if (!bPoint2Edge)
					{
						dc.LineTo(xOffset2, yOffset2+m_cyScreen);
						dc.LineTo(xOffset2, yOffset2);
						dc.LineTo(leftMostOffset+16*3*8, yOffset2);
					}
					else
						dc.LineTo(leftMostOffset+16*3*8, yOffset2+m_cyScreen);
					dc.LineTo(leftMostOffset+16*3*8, yOffset1);
				}
				dc.SelectPen(oldPen);
			//}
		}
		CRect clientRect;
		GetClientRect(&clientRect);
		CPen pen;
		pen.CreatePen(PS_SOLID, 4, RGB (255, 255, 255));
		HPEN oldPen = dc.SelectPen (pen);
		dc.Rectangle(8*(12+48+4+16)+4, 0, clientRect.right, rect.bottom);
		//dc.SelectPen (oldPen);
	}
	
//	bHandled = false;
//	return 0;
}

void CFileView::WriteLine(UINT nLine, CDCHandle& dc)
{
	CString string;
	COLORREF oldBGColor;
	COLORREF oldTextColor;
	BYTE b[17];
	BYTE c[17];
	int prevcolor = -1;

	POINT beginPos;
	dc.GetCurrentPosition(&beginPos);

	UINT nCount = curFile->GetBytes (nLine * 16 + beginOffset, 16, b);
	//curFile->GetColors(nLine * 16, 16, c);

	//UpdateColorBuffer(nLine * 0x10);
	//memcpy(c, colBuf+(nLine*0x10)-(colBufOffset*COL_BLOCK_SIZE), 0x10);
	col.GetBytes((nLine*0x10)+beginOffset, 0x10, c);
	
	//FormatLine(pDoc, nLine, string);
	dc.SetTextAlign(TA_UPDATECP);
	dc.MoveTo(2, (nLine * m_cyScreen));

	if ((ucDrawMode & DRAWMODE_OFFSETS) > 0)
	{
		string.Format(_T ("%0.8X    "), beginOffset+nLine * 16);
		dc.TextOut(0, 0, string);
	}

	oldBGColor = dc.GetBkColor();
	oldTextColor = dc.GetTextColor();

/*	dc.SetBkMode(TRANSPARENT);

	CPen pen;
	pen.CreatePen(PS_NULL, 1, RGB(255,0,0));
	HPEN pOldPen = dc.SelectPen(pen);
	CBrush brush;
	brush.CreateSolidBrush(RGB(255,255,255));
	HBRUSH pOldBrush = dc.SelectBrush(brush);
	dc.Rectangle(0, (nLine * m_cyScreen), m_cxScreen*(8+4), (nLine * m_cyScreen)+m_cyScreen+1);
	
	UpdateColorBuffer(nLine * 0x10);

	for (int i=0; i<16; i++)
		dc.Rectangle(m_cxScreen*(8+4+i), (nLine * m_cyScreen),
		             m_cxScreen*(8+4+(i*3)+3), ((nLine+1) * m_cyScreen)+1);

	//dc.SelectPen(pOldPen);
	//dc.SelectBrush(pOldBrush);

	string.Format (_T ("%0.8X    %0.2X %0.2X %0.2X %0.2X %0.2X %0.2X %0.2X %0.2X %0.2X %0.2X %0.2X %0.2X %0.2X %0.2X %0.2X %0.2X"),
		nLine * 16, b[0], b[1],  b[2],  b[3],  b[4],  b[5],  b[6],  b[7],
        b[8], b[9], b[10], b[11], b[12], b[13], b[14], b[15]);
	dc.TextOut (0, (nLine * m_cyScreen), string);
*/
	if ((ucDrawMode & DRAWMODE_HEX) > 0)
	{
		for (UINT i=0; i<16; i++)
		{
			if (i >= nCount)
			{
				dc.SetBkColor(oldBGColor);
				dc.TextOut(0, 0, _T("   "));
			}
			else
			{
				string.Format(_T ("%0.2X "), b[i]);
				if (prevcolor != c[i])
				{
					dc.SetBkColor(BGTextClr[c[i] & 0x1F]);			//bits 00011111
					dc.SetTextColor(TextClr[(c[i] & 0x60) >> 5]);	//bits 01100000
				}
				dc.TextOut(0, 0, string);
				prevcolor = c[i];
			}
		}
		dc.SetBkColor(oldBGColor);
		dc.SetTextColor(oldTextColor);
	}

	if ((ucDrawMode & DRAWMODE_ASCII) > 0)
	{
		for (UINT i=0; i<nCount; i++) 
		{
			if (!::IsCharAlphaNumeric (b[i]))
				b[i] = 0x2E;
		}
		dc.TextOut(0,0, _T("    "));	//print white space
		prevcolor = -1;
		for (UINT i=0; i<16; i++)
		{
			if (i >= nCount)
			{
				dc.SetBkColor(oldBGColor);
				dc.TextOut(0, 0, _T(" "));
			}
			else
			{
				string.Format(_T ("%c"), b[i]);
				if (prevcolor != c[i])
				{
					dc.SetBkColor(BGTextClr[c[i] & 0x1F]);		//bits 00011111
					dc.SetTextColor(TextClr[(c[i] & 0x60) >> 5]);	//bits 01100000
				}
				dc.TextOut(0, 0, string);
				prevcolor = c[i];
			}
		}
		dc.SetBkColor(oldBGColor);
		dc.SetTextColor(oldTextColor);
	}
	//
	// If less than 16 bytes were retrieved, erase to the end of the line.
	//
	/*if (nCount < 16) {
        UINT pos1 = 59;
        UINT pos2 = 60;
        UINT j = 16 - nCount;

        for (i=0; i<j; i++) {
            string.SetAt (pos1, _T (' '));
            string.SetAt (pos2, _T (' '));
            pos1 -= 3;
            pos2 -= 3;
            if (pos1 == 35) {
                string.SetAt (35, _T (' '));
                string.SetAt (36, _T (' '));
                pos1 = 33;
                pos2 = 34;
            }
        }
    }*/
}

void CFileView::OnSize(UINT nType, CSize size) {
    if(nType != SIZE_MINIMIZED) 
	{
	}
	return;
}

void CFileView::OnSizing(UINT nSide, LPRECT lpRect)
{
	return;
}

void CFileView::SelectItem(VGMItem* newItem)
{
	if (curOffset == 0)
		curOffset = newItem->dwOffset;
	if (!ScrollToByte(newItem->dwOffset))	//if a scroll position change is NOT necessary
	{
		//first check to see if the old selection encompasses the new one, or vice versa
		if (SelectedItem == NULL)						//if an old selection does not exist, just invalidate the new selection
			InvalidateFromOffsets(newItem->dwOffset, newItem->dwOffset+newItem->unLength);
		else if (SelectedItem->dwOffset < newItem->dwOffset &&					//if the old item encompasses all of the new one
			(SelectedItem->dwOffset+SelectedItem->unLength) > (newItem->dwOffset+newItem->unLength))
			InvalidateFromOffsets(SelectedItem->dwOffset, SelectedItem->dwOffset+SelectedItem->unLength);
		else if (newItem->dwOffset < SelectedItem->dwOffset &&					//if the new item encompasses all of the old one
			(newItem->dwOffset+newItem->unLength) > (SelectedItem->dwOffset+SelectedItem->unLength))
			InvalidateFromOffsets(newItem->dwOffset, newItem->dwOffset+newItem->unLength);
		else																	//otherwise, invalidate both regions
		{
			InvalidateFromOffsets(SelectedItem->dwOffset, SelectedItem->dwOffset+SelectedItem->unLength);
			InvalidateFromOffsets(newItem->dwOffset, newItem->dwOffset+newItem->unLength);
		}
	}
	else
		Invalidate();
	SelectedItem = newItem;

	/*		if (!ScrollToByte(SelectedItem->dwOffset))	//if a scroll position change is NOT necessary
	{
		//first check to see if the old selection encompasses the new one, or vice versa
		if (pNMTreeView->itemOld.hItem == NULL)						//if an old selection does not exist, just invalidate the new selection
			tempHexView->InvalidateFromOffsets(newVGMItem->dwOffset, newVGMItem->dwOffset+newVGMItem->unLength);
		else if (oldVGMItem->dwOffset < newVGMItem->dwOffset &&					//if the old item encompasses all of the new one
			(oldVGMItem->dwOffset+oldVGMItem->unLength) > (newVGMItem->dwOffset+newVGMItem->unLength))
			tempHexView->InvalidateFromOffsets(oldVGMItem->dwOffset, oldVGMItem->dwOffset+oldVGMItem->unLength);
		else if (newVGMItem->dwOffset < oldVGMItem->dwOffset &&					//if the new item encompasses all of the old one
			(newVGMItem->dwOffset+newVGMItem->unLength) > (oldVGMItem->dwOffset+oldVGMItem->unLength))
			tempHexView->InvalidateFromOffsets(newVGMItem->dwOffset, newVGMItem->dwOffset+newVGMItem->unLength);
		else																	//otherwise, invalidate both regions
		{
			tempHexView->InvalidateFromOffsets(oldVGMItem->dwOffset, oldVGMItem->dwOffset+oldVGMItem->unLength);
			tempHexView->InvalidateFromOffsets(newVGMItem->dwOffset, newVGMItem->dwOffset+newVGMItem->unLength);
		}
	}
	else
		tempHexView->Invalidate();*/
}


//InvalidateFromOffsets will invalidate sections of the hexview window based on the offset range its fed
void CFileView::InvalidateFromOffsets(UINT first, UINT last)
{
	CRect rect;
	first -= beginOffset;
	last -= beginOffset;
	BOOL bFirstEdge;
	BOOL bLastEdge = FALSE;
	CPoint scrollPos;
	GetScrollOffset(scrollPos);
	int leftMostOffset = GetLeftMostOffset();
	int yFirst = ((first / 16))*m_cyScreen-2 - scrollPos.y;
	int xFirst = (first % 16)*3*8 + leftMostOffset - 2;
	bFirstEdge = ((first % 16) == 0);
	int yLast = (last / 16)*m_cyScreen+m_cyScreen+2 - scrollPos.y;
	int xLast = (last % 16)*3*8+ leftMostOffset + 2;
	if ((last % 16) == 0)
	{
		bLastEdge = TRUE;
		yLast -= m_cyScreen;
		xLast += 16*3*8+2;
	}


	if (yFirst == (yLast-m_cyScreen-4) || bFirstEdge && bLastEdge)	//if everything is contained on a single line or if everything is conveniently contained in one box
	{
		SetRect(&rect, xFirst, yFirst, xLast, yLast);
		InvalidateRect(&rect);
	}
	else if (last - first <= 16)		//otherwise, if it's less than 16 bytes, we have 2 unattached boxes
	{
		SetRect(&rect, xFirst, yFirst, leftMostOffset+16*3*8+2, yLast-m_cyScreen);
		InvalidateRect(&rect);
		SetRect(&rect, leftMostOffset-2, yLast-m_cyScreen-4, xLast, yLast);
		InvalidateRect(&rect);
	}
	else if (bFirstEdge)
	{
		SetRect(&rect, xFirst, yFirst, leftMostOffset+16*3*8+2, yLast-m_cyScreen);	//invalidate square from beginning not including last line
		InvalidateRect(&rect);
		SetRect(&rect, xFirst, yLast-m_cyScreen, xLast, yLast);
		InvalidateRect(&rect);
	}
	else if (bLastEdge)
	{
		SetRect(&rect, xFirst, yFirst, xLast, yFirst+m_cyScreen);
		InvalidateRect(&rect);
		SetRect(&rect, leftMostOffset-2, yFirst+m_cyScreen, xLast, yLast);
		InvalidateRect(&rect);
	}
	else if (xFirst == xLast)		//if the selection is dividable into 2 boxes still
	{
		SetRect(&rect, xFirst, yFirst, leftMostOffset+16*3*8+2, yLast-m_cyScreen);
		InvalidateRect(&rect);
		SetRect(&rect, leftMostOffset-2, yFirst+m_cyScreen, xLast, yLast);
		InvalidateRect(&rect);
	}
	else							//oh fine, force me to make 3 rects, asshole
	{
		SetRect(&rect, xFirst, yFirst, leftMostOffset+16*3*8+2, yLast-m_cyScreen);
		InvalidateRect(&rect);
		SetRect(&rect, leftMostOffset-2, yFirst+m_cyScreen, xFirst, yLast-m_cyScreen);
		InvalidateRect(&rect);
		SetRect(&rect, leftMostOffset-2, yLast-m_cyScreen, xLast, yLast);
		InvalidateRect(&rect);
	}
}

UINT CFileView::GetLeftMostOffset()
{
	return ((ucDrawMode & DRAWMODE_OFFSETS) > 0)*12*8;		//8 is the character width, 12 is the number of characters for offset (including 4 spaces)
}

UINT CFileView::GetRightMostOffset()
{
	return GetLeftMostOffset() + 16*CHAR_WIDTH*3;
}

void CFileView::SetOffsetRange(ULONG newBeginOffset, ULONG newEndOffset)
{
	beginOffset = newBeginOffset;
	endOffset = newEndOffset;
}

int CFileView::ScrollToByte(DWORD byte)		//change the scroll position to the new selected item offset, if it's out of range
{
	CRect rect;
	CPoint scrollPos;
	int newPos = ((byte-beginOffset)/16)*m_cyScreen;
	GetScrollOffset(scrollPos);
	GetClientRect(&rect);
	if (scrollPos.y > newPos || scrollPos.y+rect.bottom < newPos)
	{
		SetScrollOffset(CPoint(0, newPos));
		return 1;	//return true, indicating that we did change scroll position
	}
	return 0;  //return false cause we didn't need to scroll
}



LRESULT CFileView::OnEraseBkgnd ( HDC hdc )
{
	if (!curFile)
	{
	CDCHandle  dc(hdc);
	CRect      rc;
	//SYSTEMTIME st;
	//CString    sTime;

		GetClientRect( &rc );
        dc.FillSolidRect( &rc, RGB(255,255,255)); 
		// Get our window's client area.
		//GetClientRect ( rc );

		// Build the string to show in the window.
		//GetLocalTime ( &st );
		//sTime.Format ( _T("The time is %d:%02d:%02d"), 
		//			   st.wHour, st.wMinute, st.wSecond );
		
		// Set up the DC and draw the text.
		//dc.SaveDC();

		//dc.SetBkColor ( RGB(255,153,0) );
		//dc.SetTextColor ( RGB(0,0,0) );
		//dc.ExtTextOut ( 0, 0, ETO_OPAQUE, rc, sTime, 
		//				sTime.GetLength(), NULL );

		// Restore the DC.
		//dc.RestoreDC(-1);
		return 1;    // We erased the background (ExtTextOut did it)*/
	}
	return 0;
}

VGMFile* CFileView::GetCurFile()
{
	return curFile;
}

void CFileView::SetCurFile(VGMFile* file)
{
	if (curFile != file)
	{
		curFile = file;
		SetOffsetRange(curFile->dwOffset, curFile->dwOffset+curFile->unLength);
		//if (!curFile->bUsingRawFile)
		//	SetOffsetRange(curFile->data.startOff, curFile->data.endOff);
		//else
		//	SetOffsetRange(0, file->rawfile->size());

		if (curFile == NULL)
		{
			SelectedItem = NULL;
			m_nLinesTotal = 1;
		}
		else
			m_nLinesTotal = (endOffset-beginOffset + 15) / 16;
		//SetScrollInfo(
		//SetScrollSizes (MM_TEXT, CSize (0, m_nLinesTotal * m_cyScreen),
		//	CSize (0, m_cyScreen * 10), CSize (0, m_cyScreen));
		//ScrollToPosition (CPoint (0, 0));
		//CRect rect;
		//this->GetClientRect(rect);
		//CRect rect;
		//this->Get(&rect);

		SetScrollOffset(0, 0, FALSE);
		SetScrollSize(1, m_nLinesTotal * m_cyScreen);
		SetScrollLine(0, m_cyScreen);
		//SetScrollPage(0, rc.Height()+2/*(rc.Height()/m_cyScreen)*m_cyScreen*/);
		SetScrollPage(0, m_cyScreen * 16);
		//this->SetScrollRange(SB_VERT, 0, m_nLinesTotal * m_cyScreen - 500);

		SetupColor();
	}
}

/*void CFileView::SetupColor()
{
	ULONG offset = curFile->dwOffset;
	int nSegments = curFile->unLength / COL_BLOCK_SIZE;
	if (curFile->unLength % COL_BLOCK_SIZE)
		nSegments++;

	//clear out the color buffer, if it was previously used
	for (UINT i=0; i<col.size(); i++)
		free(col[i].first);
	col.clear();

	col.reserve(nSegments);
	for (int i = 0; i < nSegments; i++)
	{
		BYTE block[COL_BLOCK_SIZE] = { 0 };
		BYTE* compBlock = (BYTE*)malloc(COL_BLOCKDEST_SIZE);
		int j = 0;
		while (j < COL_BLOCK_SIZE)
		{
			if (offset+j > curFile->dwOffset+curFile->unLength)
				break;
			VGMItem* item = curFile->GetItemFromOffset(offset+j);
			if (item)
			{
				ULONG itemSize = item->unLength;
				if (j + itemSize > COL_BLOCK_SIZE)
					itemSize = COL_BLOCK_SIZE-j;
				memset(block+j, GetItemColor(item), itemSize);
				j += itemSize - (offset+j - item->dwOffset);
			}
			else
				j++;
		}
		ULONG size = COL_BLOCKDEST_SIZE;
		compress(compBlock, &size, block, COL_BLOCK_SIZE);
		compBlock = (BYTE*)realloc(compBlock, size);
		col.push_back(pair<BYTE*, long>(compBlock, size));
		offset += COL_BLOCK_SIZE;
	}
}*/

void CFileView::SetupColor()
{
	ULONG offset = curFile->dwOffset;

	BYTE* block = new BYTE[curFile->unLength];
	memset(block, 0, curFile->unLength);

	//BYTE* compBlock = (BYTE*)malloc(COL_BLOCKDEST_SIZE);
	int j = 0;
	while (j < curFile->unLength)
	{
		//if (offset+j > curFile->dwOffset+curFile->unLength)
		//	break;
		VGMItem* item = curFile->GetItemFromOffset(offset+j);
		if (item)
		{
			ULONG itemSize = item->unLength;
			if (itemSize > curFile->unLength)
			{
				wostringstream	str;
				str << L"Error.  VGMItem: \"" << item->name << "\" is greater than length of containing VGMFile.";
				Alert(str.str().c_str());
				itemSize = curFile->unLength - item->dwOffset;
			}
			//if (j + itemSize > COL_BLOCK_SIZE)
			//	itemSize = COL_BLOCK_SIZE-j;
			if (item->dwOffset - offset == j)
			{
				memset(block+j, GetItemColor(item), itemSize);
				j += itemSize;
			}
			else
				j++; //j = item->dwOffset - offset + item->unLength;//itemSize; //- (offset+j - item->dwOffset);
		}
		else
			j++;
	}
	col.load(block, curFile->dwOffset, curFile->unLength, COL_BLOCK_SIZE);
	delete[] block;
	//ULONG size = COL_BLOCKDEST_SIZE;
	//compress(compBlock, &size, block, COL_BLOCK_SIZE);
	//compBlock = (BYTE*)realloc(compBlock, size);
	//col.push_back(pair<BYTE*, long>(compBlock, size));
	//offset += COL_BLOCK_SIZE;
}

/*void CFileView::UpdateColorBuffer(ULONG offset)
{
	int newBufOffset = offset/COL_BLOCK_SIZE;
	if (newBufOffset == colBufOffset)
		return;

	colBufOffset = newBufOffset;
	assert(colBufOffset < col.size());
	ULONG unused;
	ULONG size = sizeof(col[colBufOffset]);
	uncompress(colBuf, &unused, col[colBufOffset].first, col[colBufOffset].second);
}*/

/*
#define TEXT_CLR_BLACK 0
#define TEXT_CLR_RED 0x20
#define TEXT_CLR_PURPLE 0x40
#define TEXT_CLR_BLUE 0x60

#define BG_CLR_WHITE 0
#define BG_CLR_RED 1
#define BG_CLR_GREEN 2
#define BG_CLR_BLUE 3
#define BG_CLR_YELLOW 4
#define BG_CLR_MAGENTA 5
#define BG_CLR_CYAN 6
#define BG_CLR_DARKRED 7
#define BG_CLR_BLACK 8
#define BG_CLR_DARKBLUE 9
#define BG_CLR_STEEL 10
#define BG_CLR_CHEDDAR 11
#define BG_CLR_PINK 12
#define BG_CLR_LIGHTBLUE 13
#define BG_CLR_AQUA 14
#define BG_CLR_PEACH 15
#define BG_CLR_WHEAT 16

#define CLR_UNKNOWN (BG_CLR_BLACK|TEXT_CLR_RED)
#define CLR_NOTEON BG_CLR_BLUE
#define CLR_NOTEOFF BG_CLR_DARKBLUE
#define CLR_PROGRAMCHANGE BG_CLR_MAGENTA
#define CLR_SETVOLUME BG_CLR_PEACH
#define CLR_SETEXPRESSION BG_CLR_PINK
#define CLR_TEMPO BG_CLR_AQUA
*/

BYTE CFileView::GetItemColor(VGMItem* item)
{
	if (item->GetType() == ITEMTYPE_SEQEVENT)
	{
		switch (((SeqEvent*)item)->GetEventType())
		{
		case SeqEvent::EVENTTYPE_NOTEON :
			return CLR_NOTEON;
		case SeqEvent::EVENTTYPE_NOTEOFF :
			return CLR_NOTEOFF;
		case SeqEvent::EVENTTYPE_DURNOTE :
			return CLR_NOTEON;
		case SeqEvent::EVENTTYPE_REST :
			return CLR_REST;
		case SeqEvent::EVENTTYPE_EXPRESSION :
			return CLR_EXPRESSION;
		case SeqEvent::EVENTTYPE_VOLUME :
			return CLR_VOLUME;
		case SeqEvent::EVENTTYPE_VOLUMESLIDE :
			return CLR_VOLUME;
		case SeqEvent::EVENTTYPE_PROGCHANGE :
			return CLR_PROGCHANGE;
		case SeqEvent::EVENTTYPE_PAN :
			return CLR_PAN;
		case SeqEvent::EVENTTYPE_PITCHBEND :
			return CLR_PITCHBEND;
		case SeqEvent::EVENTTYPE_TEMPO :
			return CLR_TEMPO;
		case SeqEvent::EVENTTYPE_TIMESIG :
			return CLR_TIMESIG;
		case SeqEvent::EVENTTYPE_MODULATION :
			return CLR_MODULATION;
		case SeqEvent::EVENTTYPE_TRACKEND :
			return CLR_TRACKEND;
		case SeqEvent::EVENTTYPE_UNDEFINED :
			return CLR_UNKNOWN;
		default :
			return CLR_UNKNOWN;
		}
	}

	return BG_CLR_STEEL | TEXT_CLR_BLACK;
}