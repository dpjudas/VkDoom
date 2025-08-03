
#include "hw_rectpacker.h"
#include "engineerrors.h"

// This is a dynamic atlas texture packer
// It is used by the level mesh to dynamically allocate and free room in the lightmap atlas textures
//
// See https://mozillagfx.wordpress.com/2021/02/04/improving-texture-atlas-allocation-in-webrender/
//
// This code doesn't use entirely the same algorithm, but it uses the same shelf terminology.
// (look at the pictures)
// 
// Plus it gives you a general idea about what dynamic atlas texture packing is about.

RectPacker::RectPacker(int width, int height, int padding) : PageWidth(width), PageHeight(height), Padding(padding)
{
}

void RectPacker::Clear()
{
	Pages.clear();
	ItemFreeList.resize(Items.size());
	size_t i = Items.size();
	for (auto& item : Items)
	{
		item->Shelf = nullptr;
		item->PrevItem = nullptr;
		item->NextItem = nullptr;
		item->IsAvailable = false;
		item->PrevAvailable = nullptr;
		item->NextAvailable = nullptr;
		ItemFreeList[--i] = item.get();
	}
}

RectPackerItem* RectPacker::Alloc(int width, int height)
{
	width += Padding * 2;
	height += Padding * 2;

	if (width < 0 || height < 0 || width > PageWidth || height >= PageHeight)
		I_FatalError("Tile too large for RectPacker!");

	// How much wasted space we want to allow per shelf
	int threshold = height * 2;

	// Search pages for room
	for (RectPackerPage& page : Pages)
	{
		// Look for space on an existing shelf
		for (auto& shelf : page.Shelves)
		{
			if (shelf->Height >= height && shelf->Height <= threshold)
			{
				// We found a shelf with an acceptable height

				for (RectPackerItem* item = shelf->AvailableList; item; item = item->NextAvailable)
				{
					if (width <= item->Width)
					{
						// We found a free slot with room!
						return AllocateRoom(item, width, height);
					}
				}
			}
		}

		// No shelf found. Do we have room for a new shelf on the page?
		int nextY = (page.Shelves.size() != 0) ? page.Shelves.back()->Y + page.Shelves.back()->Height : 0;
		int availableShelfSpace = PageHeight - nextY;
		if (height <= availableShelfSpace)
		{
			// We have room. Create a shelf and allocate room in it
			return CreateShelf(&page, nextY, width, height);
		}
	}

	// No space on any pages. Create a new one
	Pages.Push(RectPackerPage((int)Pages.size()));
	return CreateShelf(&Pages.back(), 0, width, height);
}

RectPackerItem* RectPacker::AllocateRoom(RectPackerItem* item, int width, int height)
{
	if (item->Width == width)
	{
		// Perfect fit. Just remove it from the available space list.
		RemoveFromAvailableList(item);
		item->Height = height;

		AddPadding(item);
		return item;
	}
	else
	{
		// We still have space left. Insert a new item.
		RectPackerItem* newitem = AllocItem(item->Shelf);
		AddToItemList(newitem, item);
		newitem->X = item->X;
		newitem->Y = item->Y;
		newitem->Width = width;
		newitem->Height = height;
		newitem->PageIndex = item->PageIndex;

		// Update the available space item to only contain what is left
		item->X += width;
		item->Width -= width;

		AddPadding(newitem);
		return newitem;
	}
}

RectPackerItem* RectPacker::CreateShelf(RectPackerPage* page, int y, int width, int height)
{
	// Create shelf object
	page->Shelves.Push(std::make_unique<RectPackerShelf>());
	RectPackerShelf* shelf = page->Shelves.back().get();
	shelf->Y = y;
	shelf->Height = height;

	// Fill it with empty space.
	RectPackerItem* item = AllocItem(shelf);
	AddToItemList(item);
	item->X = 0;
	item->Y = y;
	item->Width = PageWidth;
	item->Height = 0;
	item->PageIndex = page->PageIndex;
	AddToAvailableList(item);

	// Allocate room for our rect
	return AllocateRoom(item, width, height);
}

void RectPacker::AddPadding(RectPackerItem* item)
{
	item->X += Padding;
	item->Y += Padding;
	item->Width -= Padding * 2;
	item->Height -= Padding * 2;

	if (item->X < 0 || item->Y < 0 || item->Width <= 0 || item->Height <= 0 || item->X + item->Width > PageWidth || item->Y + item->Height > PageHeight || item->PageIndex >= (int)Pages.size())
		I_FatalError("RectPackerItem is out of bounds!");

#ifdef VALIDATE_RECTPACKER
	ValidateAllocation(item, false);
#endif
}

void RectPacker::RemovePadding(RectPackerItem* item)
{
#ifdef VALIDATE_RECTPACKER
	ValidateAllocation(item, true);
#endif

	item->X -= Padding;
	item->Y -= Padding;
	item->Width += Padding * 2;
	item->Height += Padding * 2;
}

void RectPacker::Free(RectPackerItem* item)
{
	// Already freed or no item?
	if (!item || item->IsAvailable)
		return;

	RemovePadding(item);
	AddToAvailableList(item);

	// If next item is available space we can merge them
	if (item->NextItem && item->NextItem->IsAvailable)
	{
		item->Width += item->NextItem->Width;
		FreeItem(item->NextItem);
	}

	// If previous item is available space we can merge them
	if (item->PrevItem && item->PrevItem->IsAvailable)
	{
		item->PrevItem->Width += item->Width;
		FreeItem(item);
	}
}

RectPackerItem* RectPacker::AllocItem(RectPackerShelf* shelf)
{
	if (ItemFreeList.size() != 0)
	{
		RectPackerItem* item = ItemFreeList.back();
		ItemFreeList.Pop();
		item->Shelf = shelf;
		return item;
	}

	Items.Push(std::make_unique<RectPackerItem>());
	Items.back()->Shelf = shelf;
	return Items.back().get();
}

void RectPacker::FreeItem(RectPackerItem* item)
{
	RemoveFromItemList(item);
	RemoveFromAvailableList(item);
	item->Shelf = nullptr;
	ItemFreeList.Push(item);
}

void RectPacker::AddToItemList(RectPackerItem* item, RectPackerItem* insertAt)
{
	item->PrevItem = insertAt ? insertAt->PrevItem : nullptr;
	item->NextItem = insertAt;
	if (insertAt && insertAt->PrevItem)
		insertAt->PrevItem->NextItem = item;
	if (insertAt)
		insertAt->PrevItem = item;
	if (item->Shelf->ItemList == insertAt)
		item->Shelf->ItemList = item;
}

void RectPacker::RemoveFromItemList(RectPackerItem* item)
{
	if (!item->Shelf)
		return;

	if (item->Shelf->ItemList == item)
		item->Shelf->ItemList = item->NextItem;

	if (item->PrevItem)
		item->PrevItem->NextItem = item->NextItem;

	if (item->NextItem)
		item->NextItem->PrevItem = item->PrevItem;

	item->PrevItem = nullptr;
	item->NextItem = nullptr;
}

void RectPacker::AddToAvailableList(RectPackerItem* item)
{
	item->IsAvailable = true;

	item->NextAvailable = item->Shelf->AvailableList;
	if (item->NextAvailable)
		item->NextAvailable->PrevAvailable = item;
	item->Shelf->AvailableList = item;
}

void RectPacker::RemoveFromAvailableList(RectPackerItem* item)
{
	if (!item->Shelf || !item->IsAvailable)
		return;

	if (item->Shelf->AvailableList == item)
		item->Shelf->AvailableList = item->NextAvailable;

	if (item->PrevAvailable)
		item->PrevAvailable->NextAvailable = item->NextAvailable;

	if (item->NextAvailable)
		item->NextAvailable->PrevAvailable = item->PrevAvailable;

	item->IsAvailable = false;
	item->PrevAvailable = nullptr;
	item->NextAvailable = nullptr;
}

#ifdef VALIDATE_RECTPACKER
void RectPacker::ValidateAllocation(RectPackerItem* item, bool dealloc)
{
	// This crudely checks if an allocation is already in use by an existing allocation

	size_t bufsize = (item->PageIndex + 1) * PageWidth * PageHeight;
	while (ValidationBuffer.size() < bufsize)
		ValidationBuffer.push_back(0);

	uint8_t oldvalue = dealloc ? 1 : 0;
	uint8_t newvalue = dealloc ? 0 : 1;

	uint8_t* page = ValidationBuffer.Data() + item->PageIndex * PageWidth * PageHeight;
	int pitch = PageWidth;
	int x = item->X;
	int y = item->Y;
	int w = item->Width;
	int h = item->Height;
	for (int yy = y; yy < y + h; yy++)
	{
		for (int xx = x; xx < x + w; xx++)
		{
			if (page[xx + yy * pitch] != oldvalue)
				I_FatalError("RectPacker items are corrupted");
			page[xx + yy * pitch] = newvalue;
		}
	}
}
#endif
