// Copyright (c) 2000
// Sergey Klimov (kidd@ukr.net)

#ifndef __WTL_DW__DDTRACKER_H__
#define __WTL_DW__DDTRACKER_H__

#pragma once

#include<cassert>

class IDDTracker
{
public:
    virtual void BeginDrag(){}
    virtual void EndDrag(bool /*bCanceled*/){}
    virtual void OnMove(long /*x*/, long /*y*/){}

    virtual void OnCancelDrag(long /*x*/, long /*y*/){}
    virtual void OnDrop(long /*x*/, long /*y*/){}
    virtual void OnDropRightButton(long x, long y)
    {
       OnDrop(x,y);
    }
    virtual void OnDropLeftButton(long x, long y)
    {
       OnDrop(x,y);
    }
    virtual bool ProcessWindowMessage(MSG* /*pMsg*/)
    {
       return false;
    }


};
template<class T>
class CDDTrackerBaseT
{
public:
    void BeginDrag(){}
    void EndDrag(bool /*bCanceled*/){}
    void OnMove(long /*x*/, long /*y*/){}

    void OnCancelDrag(long /*x*/, long /*y*/){}
    void OnDrop(long /*x*/, long /*y*/){}
    void OnDropRightButton(long x, long y)
    {
       ((T*)this)->OnDrop(x,y);
    }
    void OnDropLeftButton(long x, long y)
    {
       ((T*)this)->OnDrop(x,y);
    }
    bool ProcessWindowMessage(MSG* /*pMsg*/)
    {
       return false;
    }
};

template<class T>
bool TrackDragAndDrop(T& tracker,HWND hWnd)
{
    bool bResult=true;
    tracker.BeginDrag();
    ::SetCapture(hWnd);
    MSG msg;
    while((::GetCapture()==hWnd)&&
            (GetMessage(&msg, NULL, 0, 0)))
    {
		if(!tracker.ProcessWindowMessage(&msg))
		{
		  switch(msg.message)
		  {
			   case WM_MOUSEMOVE:
					tracker.OnMove(GET_X_LPARAM( msg.lParam), GET_Y_LPARAM(msg.lParam));
					break;
			   case WM_RBUTTONUP:
					::ReleaseCapture();
					tracker.OnDropRightButton(GET_X_LPARAM( msg.lParam), GET_Y_LPARAM(msg.lParam));
					break;
			   case WM_LBUTTONUP:
					::ReleaseCapture();
					tracker.OnDropLeftButton(GET_X_LPARAM( msg.lParam), GET_Y_LPARAM(msg.lParam));
					break;
			   case WM_KEYDOWN:
					if(msg.wParam!=VK_ESCAPE)
											break;
			   case WM_RBUTTONDOWN:
			   case WM_LBUTTONDOWN:
					::ReleaseCapture();
					tracker.OnCancelDrag(GET_X_LPARAM( msg.lParam), GET_Y_LPARAM(msg.lParam));
					bResult=false;
					break;
			   case WM_SYSKEYDOWN:
					::ReleaseCapture();
					tracker.OnCancelDrag(GET_X_LPARAM( msg.lParam), GET_Y_LPARAM(msg.lParam));
					bResult=false;
			   default:
					DispatchMessage(&msg);
		  }
		}
    }
    tracker.EndDrag(!bResult);
    assert(::GetCapture()!=hWnd);
    return bResult;
}
#endif // __WTL_DW__DDTRACKER_H__