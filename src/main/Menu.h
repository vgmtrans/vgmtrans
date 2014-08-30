#pragma once

#include "common.h"
#include "VGMItem.h"

//typedef bool (*funcPtr)(void);
//typedef bool (VGMItem::*funcPtr)(void); 

/*#define DECLARE_MENU_CALLS(menuclass, origclass)											\
	protected:																		\
		static menuclass<origclass> menu;											\
	public:																			\
	virtual vector<const char*>* GetMenuItemNames() {return menu.GetMenuItemNames();}	\
	virtual bool CallMenuItem(VGMItem* item, int menuItemNum){ return menu.CallMenuItem(item, menuItemNum); }
*/

#define BEGIN_MENU_SUB(origclass, parentclass)											\
	template <class T> class origclassMenu;												\
	protected:																			\
		static origclassMenu<origclass> menu;											\
	public:																				\
		virtual std::vector<const wchar_t*>* GetMenuItemNames()							\
		{																				\
			return menu.GetMenuItemNames();												\
		}																				\
		virtual bool CallMenuItem(VGMItem* item, int menuItemNum)						\
		{																				\
			return menu.CallMenuItem(item, menuItemNum);								\
		}																				\
		template <class T> class origclassMenu : public parentclass::origclassMenu<T>	\
	{																					\
	public:																				\
		origclassMenu()																	\
		{



#define BEGIN_MENU(origclass)															\
	template <class T> class origclassMenu;												\
	protected:																			\
		static origclassMenu<origclass> menu;											\
	public:																				\
		virtual std::vector<const wchar_t*>* GetMenuItemNames()							\
		{																				\
			return menu.GetMenuItemNames();												\
		}																				\
		virtual bool CallMenuItem(VGMItem* item, int menuItemNum)						\
		{																				\
			return menu.CallMenuItem(item, menuItemNum);								\
		}																				\
	template <class T> class origclassMenu : public Menu<T>								\
	{																					\
	public:																				\
		origclassMenu()																	\
		{

#define MENU_ITEM(origclass, func, menutext)	Menu<T>::AddMenuItem(&origclass::func, menutext);
#define END_MENU()	} };

#define DECLARE_MENU(origclass)   origclass::origclassMenu<origclass> origclass::menu;

//  ********
//  MenuItem
//  ********


/*class MenuItem
{
public:
	MenuItem( bool (VGMItem::*functionPtr)(void), const char* theName, uint8_t theFlag = 0)
		: func(functionPtr), name(theName), flag(theFlag) {}
	~MenuItem() {}

public:
	const char* name;
	bool (*func)(void);
	uint8_t flag;
};*/

template <class T>
class Menu
{
public:
	Menu() {}
	virtual ~Menu() {}

	void AddMenuItem(bool (T::*funcPtr)(void), const wchar_t* name, uint8_t flag = 0)
	{
		funcs.push_back(funcPtr);
		names.push_back(name);
		//menuItems.push_back(MenuItem(funcPtr, name, flag));
	}

	bool CallMenuItem(VGMItem* item, int menuItemNum)
	{
		return (((T*)item)->*funcs[menuItemNum])();
	}

	std::vector<const wchar_t*>* GetMenuItemNames(void)
	{
		return &names;
	}
	//vector<MenuItem>* GetMenuItems(void)
	//{
	//	return &menuItems;
	//}

protected:
	std::vector<const wchar_t*> names;
	std::vector<bool (T::*)(void)> funcs;
	//vector<MenuItem> menuItems;
};