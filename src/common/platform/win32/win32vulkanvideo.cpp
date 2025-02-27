
#include <assert.h>
#include <algorithm>
#include "i_mainwindow.h"

bool I_GetVulkanPlatformExtensions(unsigned int *count, const char **names)
{
	static std::vector<std::string> extensions;

	if (mainwindow)
		extensions = mainwindow->GetVulkanInstanceExtensions();

	unsigned int extensionCount = extensions.size();
	bool result = *count >= extensionCount;

	*count = std::min<unsigned int>(*count, extensionCount);
	for (unsigned int i = 0; i < extensionCount; i++)
		names[i] = extensions[i].c_str();

	return result;
}

bool I_CreateVulkanSurface(VkInstance instance, VkSurfaceKHR *surface)
{
	try
	{
		*surface = mainwindow->CreateVulkanSurface(instance);
		return true;
	}
	catch (...)
	{
		return false;
	}
}
