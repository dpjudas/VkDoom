#pragma once

#include "templates.h"
#include <memory>

// Enable this if you want to test if atlas building is broken internally in the RectPacker
// #define VALIDATE_RECTPACKER

class RectPacker;
class RectPackerPage;
class RectPackerShelf;
class RectPackerItem;

class RectPackerItem
{
public:
	int X = 0;
	int Y = 0;
	int Width = 0;
	int Height = 0;
	int PageIndex = 0;

private:
	RectPackerShelf* Shelf = nullptr;

	RectPackerItem* PrevItem = nullptr;
	RectPackerItem* NextItem = nullptr;

	bool IsAvailable = false;
	RectPackerItem* PrevAvailable = nullptr;
	RectPackerItem* NextAvailable = nullptr;

	friend class RectPacker;
};

class RectPackerShelf
{
public:
	int X = 0;
	int Width = 0;
	RectPackerItem* ItemList = nullptr;
	RectPackerItem* AvailableList = nullptr;
};

class RectPackerPage
{
public:
	RectPackerPage(int pageIndex) : PageIndex(pageIndex) { }

	int PageIndex = 0;
	TArray<std::unique_ptr<RectPackerShelf>> Shelves;
};

class RectPacker
{
public:
	RectPacker(int width, int height, int padding);

	void Clear();

	RectPackerItem* Alloc(int width, int height);
	void Free(RectPackerItem* item);

	int GetNumPages() { return (int)Pages.size(); }

private:
	void AddPadding(RectPackerItem* item);
	void RemovePadding(RectPackerItem* item);

	RectPackerItem* AllocateRoom(RectPackerItem* item, int width, int height);
	RectPackerItem* CreateShelf(RectPackerPage* page, int x, int width, int height);

	RectPackerItem* AllocItem(RectPackerShelf* shelf);
	void FreeItem(RectPackerItem* item);

	void AddToItemList(RectPackerItem* item, RectPackerItem* insertAt = nullptr);
	void RemoveFromItemList(RectPackerItem* item);

	void AddToAvailableList(RectPackerItem* item);
	void RemoveFromAvailableList(RectPackerItem* item);

	int PageWidth = 0;
	int PageHeight = 0;
	int Padding = 0;
	TArray<RectPackerPage> Pages;
	TArray<std::unique_ptr<RectPackerItem>> Items;
	TArray<RectPackerItem*> ItemFreeList;

#ifdef VALIDATE_RECTPACKER
	void ValidateAllocation(RectPackerItem* item, bool dealloc);
	TArray<uint8_t> ValidationBuffer;
#endif

	friend class RectPackerPage;
};
