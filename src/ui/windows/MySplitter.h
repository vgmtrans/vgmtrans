// MySplitterWindowT.h: interface for the CMySplitterWindowT class.
//
//////////////////////////////////////////////////////////////////////

#if !defined(AFX_MYSPLITTER_H__B5EE008A_5CD0_4673_8275_E17CF3BCB190__INCLUDED_)
#define AFX_MYSPLITTER_H__B5EE008A_5CD0_4673_8275_E17CF3BCB190__INCLUDED_

#if _MSC_VER > 1000
#pragma once
#endif // _MSC_VER > 1000

class CMySplitter : public CSplitterWindowImpl<CMySplitter, true>
{
public:
    DECLARE_WND_CLASS_EX(_T("MySplitter"), CS_DBLCLKS, COLOR_WINDOW)

    CMySplitter() : m_bPatternBar(false)
    { 
	}
    
    // Operations
    void EnablePatternBar ( bool bEnable = true )
    {
        if ( bEnable != m_bPatternBar )
            {
            m_bPatternBar = bEnable;
            UpdateSplitterLayout();
            }
    }

    // Overrideables
    void DrawSplitterBar(CDCHandle dc)
    {
    RECT rect;

        // If we're not using a colored bar, let the base class do the
        // default drawing.
        if ( !m_bPatternBar )
            {
            CSplitterWindowImpl<CMySplitter, true>::DrawSplitterBar(dc);
            return;
            }

        // Create a brush to paint with, if we haven't already done so.
        if ( m_br.IsNull() )
            m_br.CreateHatchBrush ( HS_DIAGCROSS, RGB(255,0,0) );

        if ( GetSplitterBarRect ( &rect ) )
        {
            dc.FillRect ( &rect, m_br );

            // draw 3D edge if needed
            if ( (GetExStyle() & WS_EX_CLIENTEDGE) != 0)
                dc.DrawEdge(&rect, EDGE_RAISED, (BF_LEFT | BF_RIGHT));
        }
    }

protected:
    CBrush m_br;
    bool   m_bPatternBar;
};

#endif // !defined(AFX_SONGSPLITTER_H__B5EE008A_5CD0_4673_8275_E17CF3BCB190__INCLUDED_)
