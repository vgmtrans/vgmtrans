#pragma once

template <class T> class ToggleView
{
public:
	ToggleView(void);
public:
	~ToggleView(void);

	void Toggle(void)
	{
		CFileFrame* pChild = new CFileFrame;
		pChild->CreateEx(m_hWndClient);

		pChild->SetCommandBarCtrlForContextMenu(&m_CmdBar);
	}
};
