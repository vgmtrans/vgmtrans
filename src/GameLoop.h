/* WTLGame.h ******************************************************************
Author:		Paul Watt
Date:		4/22/2002
Purpose:	Contains any classes that will extend WTL in a manner that is 
			suitable for game development.
******************************************************************************/
#ifndef __WTLGAME_H
#define __WTLGAME_H

/* Include Files *************************************************************/
#include "AtlApp.h"


/* Class **********************************************************************
Author:		Paul Watt
Date:		4/22/2002
Purpose:	An interface used fro updating the game frame and indicating that
			the game is in pause status.
******************************************************************************/
class CGameHandler
{
public:
	virtual BOOL	IsPaused() = 0;
	virtual HRESULT OnUpdateFrame() = 0;
};


/* Class **********************************************************************
Author:		Paul Watt
Date:		4/22/2002
Purpose:	This class provides a replacement to the standard CMessageLoop
			class that oridinarily is used as the message pump in a WTL application.
			The CGameLoop changes the structure of the message pump to allow
			the game to update its current frame whenever the message queue 
			is empty.

			This class provides all of the mechanisms that are availble in
			the standard CMssageLoop.  Idle messages are only generated when the
			message queue is empty, rather than after every active message.
			PeekMessage is used in the loop rather than GetMessage because
			it is non-blocking when the message queue is empty.
******************************************************************************/
class CGameLoop : public CMessageLoop
{
public:
	CGameLoop ()
	{
		m_gameHandler = NULL;
	}
				//C: The class that should be used to update the game frame.
				//   There is only one frame.  If other game frames need to
				//   be updated, they should be chained through this update.
	CGameHandler* m_gameHandler;

	/* Public *********************************************************************
	Author:		Paul Watt
	Date:		4/22/2002
	Purpose:	Returns the current Game handler object.
	Parameters:	NONE
	Return:		-
	******************************************************************************/
	CGameHandler* GetGameHandler()
	{
		return m_gameHandler;
	}

	/* Public *********************************************************************
	Author:		Paul Watt
	Date:		4/22/2002
	Purpose:	Sets the current Game handler object.  The game handler is used
				to update the current game frame and managed the pause mode.
	Parameters:	pGameHandler[in]: A pointer to the object that will handle 
					game frame updates and indicate if the game is paused.
	Return:		-
	******************************************************************************/
	void SetGameHandler(CGameHandler* pGameHandler)
	{
		m_gameHandler = pGameHandler;
	}

	/* Public *********************************************************************
	Author:		Paul Watt
	Date:		4/22/2002
	Purpose:	The message processing loop.  This loop uses PeekMessage to grab
				messages because it does not block when the message queue is empty.
				It will then process idle messages when the queue becomes empty.
				Finally it will update the game frame while there are no messages
				in the queue.  The loop will block if the game is placed in pause
				mode.
	Parameters:	NONE	
	Return:		Returns the exit code of the loop.
	******************************************************************************/
	virtual int Run()
	{
		bool	isActive = true;

		while (TRUE)
		{

			if (PeekMessage( &m_msg, NULL, 0, 0, PM_REMOVE))
			{
				//C: Check for the WM_QUIT message to exit the loop.
				if (WM_QUIT == m_msg.message)
				{
					ATLTRACE2(atlTraceUI, 0, _T("CGameLoop::Run - exiting\n"));
					break;		// WM_QUIT, exit message loop
				}
				//C: Flag the loop as active only if an active message
				//   is processed.
				if (IsIdleMessage(&m_msg))
				{
					isActive = true;
				}
				//C: Attmpt to translate and dispatch the messages.
				if(!PreTranslateMessage(&m_msg))
				{
					::TranslateMessage(&m_msg);
					::DispatchMessage(&m_msg);
				}
			}
			else if (isActive)
			{
				//C: Perform idle message processing.
				OnIdle(0);
				//C: Flag the loop as inactive.  This will prevent other idle messages
				//   from being processed while the message queue is empty.
				isActive = false;
			}
			else if (m_gameHandler)
			{
				//C: Is the game paused.
				if (m_gameHandler->IsPaused())
				{
				//C: To keep the program from spinning needlessly, wait until the next
				//   message enters the queue before processing any more data.
					WaitMessage();
				}
				else
				{
				//C: All other activities are taken care of, update the current frame of the game.
					m_gameHandler->OnUpdateFrame();
				}
			}
		}
				//C: Returns the exit code for the loop.
		return (int)m_msg.wParam;
	}

};


#endif //__WTLGAME_H