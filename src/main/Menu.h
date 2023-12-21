/**
 * VGMTrans (c) - 2002-2021
 * Licensed under the zlib license
 * See the included LICENSE for more information
 */

#pragma once

#include "common.h"
#include "VGMItem.h"

#ifdef VGMTRANS_LEGACY

#define BEGIN_MENU_SUB(origclass, parentclass)                                            \
    template <class T> class origclassMenu;                                                \
    protected:                                                                            \
        static origclassMenu<origclass> menu;                                            \
    public:                                                                                \
        virtual std::vector<const wchar_t*>* GetMenuItemNames()                            \
        {                                                                                \
            return menu.GetMenuItemNames();                                                \
        }                                                                                \
        virtual bool CallMenuItem(VGMItem* item, int menuItemNum)                        \
        {                                                                                \
            return menu.CallMenuItem(item, menuItemNum);                                \
        }                                                                                \
        template <class T> class origclassMenu : public parentclass::origclassMenu<T>    \
    {                                                                                    \
    public:                                                                                \
        origclassMenu()                                                                    \
        {


#define BEGIN_MENU(origclass)                                                            \
    template <class T> class origclassMenu;                                                \
    protected:                                                                            \
        static origclassMenu<origclass> menu;                                            \
    public:                                                                                \
        virtual std::vector<const wchar_t*>* GetMenuItemNames()                            \
        {                                                                                \
            return menu.GetMenuItemNames();                                                \
        }                                                                                \
        virtual bool CallMenuItem(VGMItem* item, int menuItemNum)                        \
        {                                                                                \
            return menu.CallMenuItem(item, menuItemNum);                                \
        }                                                                                \
    template <class T> class origclassMenu : public Menu<T>                                \
    {                                                                                    \
    public:                                                                                \
        origclassMenu()                                                                    \
        {

#define MENU_ITEM(origclass, func, menutext)    Menu<T>::AddMenuItem(&origclass::func, menutext);
#define END_MENU()    } };

#define DECLARE_MENU(origclass)   origclass::origclassMenu<origclass> origclass::menu;

#else
    #define BEGIN_MENU_SUB(STUB, STUB2)
    #define BEGIN_MENU(STUB)
    #define MENU_ITEM(STUB, STUB2, STUB3)
    #define END_MENU()
    #define DECLARE_MENU(STUB)
#endif

template<class T>
class Menu {
 public:
  Menu() { }
  virtual ~Menu() { }

  void AddMenuItem(bool (T::*funcPtr)(void), const char* name, uint8_t flag = 0) {
    funcs.push_back(funcPtr);
    names.push_back(name);
  }

  bool CallMenuItem(VGMItem *item, int menuItemNum) {
    return (((T *) item)->*funcs[menuItemNum])();
  }

  std::vector<const char* > *GetMenuItemNames(void) {
    return &names;
  }

 protected:
  std::vector<const char* > names;
  std::vector<bool (T::*)(void)> funcs;
};