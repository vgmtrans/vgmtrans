/**
 * VGMTrans (c) - 2002-2021
 * Licensed under the zlib license
 * See the included LICENSE for more information
 */

#pragma once

class CMySplitter : public CSplitterWindowImpl<CMySplitter> {
public:
  DECLARE_WND_CLASS_EX(_T("MySplitter"), CS_DBLCLKS, COLOR_WINDOW)

  // Operations
  void EnablePatternBar(bool bEnable = true) {
    if (bEnable != m_bPatternBar) {
      m_bPatternBar = bEnable;
      UpdateSplitterLayout();
    }
  }

  // Overrideables
  void DrawSplitterBar(CDCHandle dc) {
    RECT rect;

    // If we're not using a colored bar, let the base class do the
    // default drawing.
    if (!m_bPatternBar) {
      CSplitterWindowImpl<CMySplitter>::DrawSplitterBar(dc);
      return;
    }

    // Create a brush to paint with, if we haven't already done so.
    if (m_br.IsNull())
      m_br.CreateHatchBrush(HS_DIAGCROSS, RGB(255, 0, 0));

    if (GetSplitterBarRect(&rect)) {
      dc.FillRect(&rect, m_br);

      // draw 3D edge if needed
      if ((GetExStyle() & WS_EX_CLIENTEDGE) != 0)
        dc.DrawEdge(&rect, EDGE_RAISED, (BF_LEFT | BF_RIGHT));
    }
  }

protected:
  CBrush m_br{};
  bool m_bPatternBar{false};
};
