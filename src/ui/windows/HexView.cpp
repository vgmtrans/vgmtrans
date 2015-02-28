#include "pch.h"
#include "HexView.h"
#include "ItemTreeView.h"
#include "HexViewFrame.h"
#include "WinVGMRoot.h"
#include "SeqEvent.h"
//#include "VGMSeq.h"
#include <zlib.h>

using namespace std;

//CHexView fileView;

CHexView::CHexView(CHexViewFrame* parentFrame)
: SelectedItem(NULL), beginOffset(0), parFrame(parentFrame)//, colBufOffset(-1)
{
}

LRESULT CHexView::OnCreate(UINT uMsg, WPARAM wParam, LPARAM lParam, BOOL& bHandled)
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

	ucDrawMode = DRAWMODE_OFFSETS | DRAWMODE_HEX | DRAWMODE_ASCII;

	if (curFile != NULL)
	{
		SetupScrollInfo();
		SetupColor();
	}

	//SCROLLINFO si;
    //si.cbSize = sizeof(si);
    //si.fMask = SIF_DISABLENOSCROLL;
    //SetScrollInfo( SB_HORZ, &si, TRUE);
    //SetScrollInfo( SB_VERT, &si, TRUE);
    //return 0; 

	bHandled = TRUE;
	return nResult;
}

LRESULT CHexView::OnDestroy(UINT /*uMsg*/, WPARAM /*wParam*/, LPARAM /*lParam*/, BOOL& bHandled)
{
	//col.clear();
	delete[] col;

	bHandled = FALSE;
	return 0;
}

LRESULT CHexView::OnSetFocus(UINT /*uMsg*/, WPARAM /*wParam*/, LPARAM /*lParam*/, BOOL& bHandled)
{
	bHandled = FALSE;
	return 0;
}

void CHexView::OnLButtonDown(UINT nFlags, CPoint point)
{
	CPoint scrollPos;
	if (curFile != NULL)
	{
		bool bClickedAnItem = false;
		ULONG offset = 0;
		GetScrollOffset(scrollPos);
		
		if ((point.x > GetHexLeftEdge()) && (point.x < GetHexRightEdge()))	//if we are clicking within the hexadecimal area of the the HexView
		{
			bClickedAnItem = true;
			offset = (scrollPos.y + point.y)/m_cyScreen * 16 + beginOffset;
			offset += (point.x - GetHexLeftEdge()) / (CHAR_WIDTH*3);	//CHAR_WIDTH*3 because there are 2 characters, and 1 space per byte
		}
		else if ((point.x > GetAsciiLeftEdge()) && (point.x < GetAsciiRightEdge()))
		{
			bClickedAnItem = true;
			offset = (scrollPos.y + point.y)/m_cyScreen * 16 + beginOffset;
			offset += (point.x - GetAsciiLeftEdge()) / CHAR_WIDTH;
		}
		VGMItem* selectedItem = NULL;
		if (bClickedAnItem) {
			selectedItem = curFile->GetItemFromOffset(offset, false);
			if (selectedItem == NULL) {
				selectedItem = curFile->GetItemFromOffset(offset, true);
			}
		}
		parFrame->SelectItem(selectedItem);
		curOffset = offset;
	}
	SetFocus();
}

LRESULT CHexView::OnRButtonUp(UINT uMsg, WPARAM wParam, LPARAM lParam, BOOL& bHandled)
{
	SendMessage(this->m_hWnd, WM_LBUTTONDOWN, wParam, lParam);
	bHandled = false;
	return 0;
}



LRESULT CHexView::OnContextMenu(HWND hwndCtrl, CPoint ptClick )
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

	

void CHexView::OnKeyDown(UINT nChar, UINT nRepCnt, UINT nFlags)
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

		VGMItem* temp = curFile->GetItemFromOffset(curOffset, false);
		if (temp)
		{
			//winroot.SelectItem(temp);
			parFrame->SelectItem(temp);
			//itemTreeView.SelectItem(temp);
			//SelectItem(temp);
			//UpdateStatusBarWithItem(temp);
		}
	}

//	CScrollView::OnKeyDown(nChar, nRepCnt, nFlags);
}

//void CHexView::OnPaint(HDC hdc)
//LRESULT CHexView::OnPaint(UINT /*uMsg*/, WPARAM /*wParam*/, LPARAM /*lParam*/, BOOL& bHandled)
void CHexView::DoPaint(CDCHandle dc)
{
	//CDCHandle  dc(hdc);
	//CDCHandle dc((HDC)wParam);
	//CPaintDC   dc(m_hWnd);
	
	//Crect rc;
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

	if (m_nLinesTotal == 0)
		return;
	CRect rect;
	dc.GetClipBox (&rect);

	UINT nStart = rect.top / m_cyScreen;
	UINT nEnd = min (m_nLinesTotal - 1,
		(rect.bottom + m_cyScreen - 1) / m_cyScreen);

	HFONT oldFont = dc.SelectFont (m_fontScreen);
	for (UINT i=nStart; i<=nEnd; i++)
		WriteLine(i, dc);
	dc.SelectFont (oldFont);
	if (SelectedItem != NULL && (ucDrawMode & DRAWMODE_HEX))
	{
		CPen pen;
		pen.CreatePen(PS_SOLID, 4, RGB (255, 0, 0));
		HPEN oldPen = dc.SelectPen (pen);
		ULONG selectedItemOffset = SelectedItem->dwOffset - beginOffset;
		ULONG selectedItemLength = SelectedItem->unLength;
		const int hexLeftEdge = GetHexLeftEdge();
		const int hexRightEdge = GetHexRightEdge();
		const int asciiLeftEdge = GetAsciiLeftEdge();
		const int asciiRightEdge = GetAsciiRightEdge();
		const int nBeginByteNum = selectedItemOffset % 16;
		const int nEndByteNum = (selectedItemOffset+selectedItemLength) % 16;
		const int yOffset1 = ((selectedItemOffset / 16))*m_cyScreen;
		const int xOffset1 = hexLeftEdge + nBeginByteNum*3*CHAR_WIDTH;
		int yOffset2 = ((selectedItemOffset+selectedItemLength) / 16)*m_cyScreen;
		int xOffset2 = hexLeftEdge + nEndByteNum*3*CHAR_WIDTH;
		
		int asciiXOffset1 = asciiLeftEdge + (nBeginByteNum*CHAR_WIDTH);
		int asciiXOffset2 = asciiLeftEdge + (nEndByteNum*CHAR_WIDTH);

		if (nEndByteNum == 0)
		{
			yOffset2 -= m_cyScreen;
			xOffset2 += 16*3*CHAR_WIDTH;
			asciiXOffset2 += 16*CHAR_WIDTH;
		}

		if (selectedItemLength <= 16)		//if the length does not exceed 16 bytes, we draw 1 box if it encompasses only 1 row, or 2 separate for 2 rows
		{
			if (yOffset1 == yOffset2)
			{
				// hex
				DrawBox(dc, xOffset1, yOffset1, xOffset2, yOffset1+m_cyScreen);
				// ascii
				DrawBox(dc, asciiXOffset1, yOffset1, asciiXOffset2, yOffset1+m_cyScreen);
			}
			else
			{
				// hex
				DrawBox(dc, xOffset1, yOffset1, hexRightEdge, yOffset1+m_cyScreen);
				DrawBox(dc, hexLeftEdge, yOffset2, xOffset2, yOffset2+m_cyScreen);
				// ascii
				DrawBox(dc, asciiXOffset1, yOffset1, asciiRightEdge, yOffset1+m_cyScreen);
				DrawBox(dc, asciiLeftEdge, yOffset2, asciiXOffset2, yOffset2+m_cyScreen);
			}
		}
		else
		{
			// hex
			DrawComplexOutline(dc, hexLeftEdge, hexRightEdge, xOffset1, yOffset1, xOffset2, yOffset2);
			// ascii
			DrawComplexOutline(dc, asciiLeftEdge, asciiRightEdge, asciiXOffset1, yOffset1, asciiXOffset2, yOffset2);
		}
		dc.SelectPen(oldPen);
	}
	CRect clientRect;
	GetClientRect(&clientRect);
	CPen pen;
	pen.CreatePen(PS_SOLID, 4, RGB (255, 255, 255));
	HPEN oldPen = dc.SelectPen (pen);
	dc.Rectangle(8*(12+48+4+16)+4, 0, clientRect.right, rect.bottom);
	if (rect.bottom/m_cyScreen >= m_nLinesTotal)
		dc.Rectangle(rect.left, m_nLinesTotal*m_cyScreen, rect.right, rect.bottom);
	//dc.SelectPen (oldPen);
}

void CHexView::DrawBox(CDCHandle& dc, int x1, int y1, int x2, int y2)
{
	//counter-clockwise from top left.  why not?
	dc.MoveTo(x1, y1);
	dc.LineTo(x1, y2);
	dc.LineTo(x2, y2);
	dc.LineTo(x2, y1);
	dc.LineTo(x1, y1);
}

void CHexView::DrawComplexOutline(CDCHandle& dc, int leftEdge, int rightEdge, 
								  int x1, int y1, int x2, int y2)
{
	bool bPoint1Edge = (x1 == leftEdge);
	bool bPoint2Edge = (x2 == rightEdge);

	if (!bPoint1Edge)  //start in upper right corner
	{
		dc.MoveTo(rightEdge, y1);
		dc.LineTo(x1, y1);
		dc.LineTo(x1, y1+m_cyScreen);
		dc.LineTo(leftEdge, y1+m_cyScreen);
	}
	else
	{
		dc.MoveTo(rightEdge, y1);
		dc.LineTo(leftEdge, y1);
	}
	dc.LineTo(leftEdge, y2+m_cyScreen);
	//now we are at the lower left corner
	if (!bPoint2Edge)
	{
		dc.LineTo(x2, y2+m_cyScreen);
		dc.LineTo(x2, y2);
		dc.LineTo(rightEdge, y2);
	}
	else
		dc.LineTo(rightEdge, y2+m_cyScreen);
	dc.LineTo(rightEdge, y1);
}

void CHexView::WriteLine(UINT nLine, CDCHandle& dc)
{
	CString string;
	COLORREF oldBGColor;
	COLORREF oldTextColor;
	BYTE b[17];
	BYTE c[17];
	int prevcolor = -1;

	POINT beginPos;
	dc.GetCurrentPosition(&beginPos);

	//UINT nCount = 16;
	//if (nLine*16 + beginOffset + nCount > curFile->dwOffset + curFile->unLength)
	//	nCount = (nLine*16 + beginOffset + nCount) - (curFile->dwOffset + curFile->unLength);
	
	BYTE nCount = curFile->GetBytes (nLine * 16 + beginOffset, 16, b);
	//curFile->GetColors(nLine * 16, 16, c);

	//UpdateColorBuffer(nLine * 0x10);
	//memcpy(c, colBuf+(nLine*0x10)-(colBufOffset*COL_BLOCK_SIZE), 0x10);
	
	//col.GetBytes((nLine*0x10)+beginOffset, 0x10, c);
	memcpy(c, col+(nLine*0x10), 0x10);

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
	if (ucDrawMode & DRAWMODE_HEX)
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
					dc.SetTextColor(TextClr[(c[i] & 0xE0) >> 5]);	//bits 11100000
				}
				dc.TextOut(0, 0, string);
				prevcolor = c[i];
			}
		}
		dc.SetBkColor(oldBGColor);
		dc.SetTextColor(oldTextColor);
	}

	if (ucDrawMode & DRAWMODE_ASCII)
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
	//if (nCount < 16) {
 //       UINT pos1 = 59;
 //       UINT pos2 = 60;
 //       UINT j = 16 - nCount;

 //       for (int i=0; i<j; i++) {
 //           string.SetAt (pos1, _T (' '));
 //           string.SetAt (pos2, _T (' '));
 //           pos1 -= 3;
 //           pos2 -= 3;
 //           if (pos1 == 35) {
 //               string.SetAt (35, _T (' '));
 //               string.SetAt (36, _T (' '));
 //               pos1 = 33;
 //               pos2 = 34;
 //           }
 //       }
 //   }
}

/*void CHexView::OnSize(UINT nType, CSize size) {
    if(nType != SIZE_MINIMIZED) 
	{
		
	}
	return;
}

void CHexView::OnSizing(UINT nSide, LPRECT lpRect)
{
	return;
}*/

void CHexView::SelectItem(VGMItem* newItem)
{
	if (!newItem)
	{
		Invalidate();
		SelectedItem = newItem;
		return;
	}
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
void CHexView::InvalidateFromOffsets(UINT first, UINT last)
{
	CRect rect;
	first -= beginOffset;
	last -= beginOffset;
	BOOL bFirstEdge;
	BOOL bLastEdge = FALSE;
	CPoint scrollPos;
	GetScrollOffset(scrollPos);
	const int hexLeftEdge = GetHexLeftEdge();
	const int hexRightEdge = GetHexRightEdge();
	const int asciiLeftEdge = GetAsciiLeftEdge();
	const int asciiRightEdge = GetAsciiRightEdge();
	const int nBeginByteNum = first % 16;
	const int nEndByteNum = last % 16;
	const int yFirst = ((first / 16))*m_cyScreen-2 - scrollPos.y;
	const int xFirst = nBeginByteNum*3*CHAR_WIDTH + hexLeftEdge - 2;
	bFirstEdge = ((first % 16) == 0);
	int yLast = (last / 16)*m_cyScreen+m_cyScreen+2 - scrollPos.y;
	int xLast = nEndByteNum*3*CHAR_WIDTH+ hexLeftEdge + 2;

	const int asciiXFirst = asciiLeftEdge + nBeginByteNum*CHAR_WIDTH - 2;
	int asciiXLast = asciiLeftEdge + nEndByteNum*CHAR_WIDTH + 2;

	if ((last % 16) == 0)
	{
		bLastEdge = TRUE;
		yLast -= m_cyScreen;
		xLast += 16*3*CHAR_WIDTH+2;
		asciiXLast += 16*CHAR_WIDTH+2;
	}

	if ((yFirst == (yLast-m_cyScreen-4)) || (bFirstEdge && bLastEdge))	//if everything is contained on a single line or if everything is conveniently contained in one box
	{
		// hex
		SetRect(&rect, xFirst, yFirst, xLast, yLast);
		InvalidateRect(&rect);
		// ascii
		SetRect(&rect, asciiXFirst, yFirst, asciiXLast, yLast);
		InvalidateRect(&rect);
	}
	else if (last - first <= 16)		//otherwise, if it's less than 16 bytes, we have 2 unattached boxes
	{
		// hex
		SetRect(&rect, xFirst, yFirst, hexRightEdge+2, yLast-m_cyScreen);
		InvalidateRect(&rect);
		SetRect(&rect, hexLeftEdge-2, yLast-m_cyScreen-4, xLast, yLast);
		InvalidateRect(&rect);
		// ascii
		SetRect(&rect, asciiXFirst, yFirst, asciiRightEdge+2, yLast-m_cyScreen);
		InvalidateRect(&rect);
		SetRect(&rect, asciiLeftEdge-2, yLast-m_cyScreen-4, asciiXLast, yLast);
		InvalidateRect(&rect);
	}
	else if (bFirstEdge)
	{
		// hex
		SetRect(&rect, xFirst, yFirst, hexRightEdge+2, yLast-m_cyScreen);	//invalidate square from beginning not including last line
		InvalidateRect(&rect);
		SetRect(&rect, xFirst, yLast-m_cyScreen, xLast, yLast);
		InvalidateRect(&rect);
		// ascii
		SetRect(&rect, asciiXFirst, yFirst, asciiRightEdge+2, yLast-m_cyScreen);	//invalidate square from beginning not including last line
		InvalidateRect(&rect);
		SetRect(&rect, asciiXFirst, yLast-m_cyScreen, asciiXLast, yLast);
		InvalidateRect(&rect);
	}
	else if (bLastEdge)
	{
		// hex
		SetRect(&rect, xFirst, yFirst, xLast, yFirst+m_cyScreen);
		InvalidateRect(&rect);
		SetRect(&rect, hexLeftEdge-2, yFirst+m_cyScreen, xLast, yLast);
		InvalidateRect(&rect);
		// ascii
		SetRect(&rect, asciiXFirst, yFirst, asciiXLast, yFirst+m_cyScreen);
		InvalidateRect(&rect);
		SetRect(&rect, asciiLeftEdge-2, yFirst+m_cyScreen, asciiXLast, yLast);
		InvalidateRect(&rect);
	}
	else if (xFirst == xLast)		//if the selection is dividable into 2 boxes still
	{
		// hex
		SetRect(&rect, xFirst, yFirst, hexRightEdge+2, yLast-m_cyScreen);
		InvalidateRect(&rect);
		SetRect(&rect, hexLeftEdge-2, yFirst+m_cyScreen, xLast, yLast);
		InvalidateRect(&rect);
		// ascii
		SetRect(&rect, asciiXFirst, yFirst, asciiRightEdge+2, yLast-m_cyScreen);
		InvalidateRect(&rect);
		SetRect(&rect, asciiLeftEdge-2, yFirst+m_cyScreen, asciiXLast, yLast);
		InvalidateRect(&rect);
	}
	else							//oh fine, force me to make 3 rects, asshole
	{
		// hex
		SetRect(&rect, xFirst, yFirst, hexRightEdge+2, yLast-m_cyScreen);
		InvalidateRect(&rect);
		SetRect(&rect, hexLeftEdge-2, yFirst+m_cyScreen, xFirst, yLast-m_cyScreen);
		InvalidateRect(&rect);
		SetRect(&rect, hexLeftEdge-2, yLast-m_cyScreen, xLast, yLast);
		InvalidateRect(&rect);
		// ascii
		SetRect(&rect, asciiXFirst, yFirst, asciiRightEdge+2, yLast-m_cyScreen);
		InvalidateRect(&rect);
		SetRect(&rect, asciiLeftEdge-2, yFirst+m_cyScreen, asciiXFirst, yLast-m_cyScreen);
		InvalidateRect(&rect);
		SetRect(&rect, asciiLeftEdge-2, yLast-m_cyScreen, asciiXLast, yLast);
		InvalidateRect(&rect);
	}
}

int CHexView::GetHexLeftEdge()
{
	//12 is the number of characters for offset (8 digits plus 4 spaces)
	return ((ucDrawMode & DRAWMODE_OFFSETS) > 0)*12*CHAR_WIDTH;
}

int CHexView::GetHexRightEdge()
{
	return GetHexLeftEdge() + 16*CHAR_WIDTH*3;
}

int CHexView::GetAsciiLeftEdge()
{
	//12 is the number of characters for offset (8 digits plus 4 spaces)
	//16 bytes per line, 3 chars per byte (digit, digit, space), 4 spaces after hex
	return (((ucDrawMode & DRAWMODE_OFFSETS) ? 12 : 0) + (16*3)+4) *CHAR_WIDTH;
}

int CHexView::GetAsciiRightEdge()
{
	return GetAsciiLeftEdge() + 16*CHAR_WIDTH;
}

void CHexView::SetOffsetRange(ULONG newBeginOffset, ULONG newEndOffset)
{
	beginOffset = newBeginOffset;
	endOffset = newEndOffset;
}

int CHexView::ScrollToByte(DWORD byte)		//change the scroll position to the new selected item offset, if it's out of range
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



LRESULT CHexView::OnEraseBkgnd ( HDC hdc )
{
	if (!curFile)
	{
		CDCHandle  dc(hdc);
		CRect      rc;

		GetClientRect( &rc );
        dc.FillSolidRect( &rc, RGB(255,255,255)); 

		return 1;    // We erased the background (ExtTextOut did it)*/
	}
	return 0;
}

VGMFile* CHexView::GetCurFile()
{
	return curFile;
}

void CHexView::SetCurFile(VGMFile* file)
{
	if (curFile != file)
		curFile = file;
}

void CHexView::SetupScrollInfo()
{
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
		m_nLinesTotal = ((endOffset-beginOffset) + 15) / 16;
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
}


void CHexView::SetupColor()
{
	ULONG offset = curFile->dwOffset;

	/*BYTE* block*/ col = new BYTE[curFile->unLength];
	memset(col, 0, curFile->unLength);

	//BYTE* compBlock = (BYTE*)malloc(COL_BLOCKDEST_SIZE);
	unsigned int j = 0;
	while (j < curFile->unLength)
	{
		//if (offset+j > curFile->dwOffset+curFile->unLength)
		//	break;
		VGMItem* item = curFile->GetItemFromOffset(offset+j, false);
		if (item)
		{
			ULONG itemSize = item->unLength;
			if (itemSize > curFile->unLength)
			{
				wostringstream	str;
				str << L"Error.  VGMItem: \"" << item->name << "\" is greater than length of containing VGMFile.";
				//Alert(str.str().c_str());
				itemSize = curFile->unLength - item->dwOffset;
			}
			if (j+itemSize > curFile->unLength)
			{
				//wostringstream	str;
				//str << L"Error.  VGMItem: \"" << item->name << "\" exceeds past end of containing VGMFile.";
				//Alert(str.str().c_str());
				itemSize = curFile->unLength - item->dwOffset;
			}
			//if (j + itemSize > COL_BLOCK_SIZE)
			//	itemSize = COL_BLOCK_SIZE-j;
			if (item->dwOffset - offset == j)
			{
				memset(col+j, GetItemColor(item), itemSize);
				j += itemSize;
			}
			else
				j++; //j = item->dwOffset - offset + item->unLength;//itemSize; //- (offset+j - item->dwOffset);
		}
		else
			j++;
	}
	//col.load(block, curFile->dwOffset, curFile->unLength/*, COL_BLOCK_SIZE*/);
	//delete[] block;
}

/*void CHexView::UpdateColorBuffer(ULONG offset)
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

BYTE CHexView::GetItemColor(VGMItem* item)
{
	return ColorTranslation[item->color];

	//if (item->GetType() == ITEMTYPE_SEQEVENT)
	//{
		//return EventColorTranslation[(SeqEvent*)item)->GetEventType()];
		//switch (((SeqEvent*)item)->GetEventType())
		//{
		//case EVENTTYPE_NOTEON :				return CLR_NOTEON;
		//case EVENTTYPE_NOTEOFF :			return CLR_NOTEOFF;
		//case EVENTTYPE_DURNOTE :			return CLR_NOTEON;
		//case EVENTTYPE_REST :				return CLR_REST;
		//case EVENTTYPE_EXPRESSION :			return CLR_EXPRESSION;
		//case EVENTTYPE_EXPRESSIONSLIDE :	return CLR_EXPRESSION;
		//case EVENTTYPE_VOLUME :				return CLR_VOLUME;
		//case EVENTTYPE_VOLUMESLIDE :		return CLR_VOLUME;
		//case EVENTTYPE_PROGCHANGE :			return CLR_PROGCHANGE;
		//case EVENTTYPE_PAN :				return CLR_PAN;
		//case EVENTTYPE_PITCHBEND :			return CLR_PITCHBEND;
		//case EVENTTYPE_PITCHBENDRANGE :		return CLR_PITCHBENDRANGE;
		//case EVENTTYPE_TRANSPOSE :			return CLR_TRANSPOSE;
		//case EVENTTYPE_TEMPO :				return CLR_TEMPO;
		//case EVENTTYPE_TIMESIG :			return CLR_TIMESIG;
		//case EVENTTYPE_MODULATION :			return CLR_MODULATION;
		//case EVENTTYPE_SUSTAIN :			return CLR_SUSTAIN;
		//case EVENTTYPE_PORTAMENTO :			return CLR_PORTAMENTO;
		//case EVENTTYPE_PORTAMENTOTIME :		return CLR_PORTAMENTOTIME;
		//case EVENTTYPE_TRACKEND :			return CLR_TRACKEND;
		//case EVENTTYPE_LOOPFOREVER :		return CLR_LOOPFOREVER;
		//case EVENTTYPE_UNDEFINED :			return item->color;
		//default :							return CLR_UNKNOWN;
		//}
	//}

	//return BG_CLR_BLACK | TXT_CLR_WHITE;
}