
#include "main.h"

RakNetTime HOOK_RakNet_GetTime()
{
	static bool& initialized = *reinterpret_cast<bool*>(0x81A19C4);
	static timeval& initialTime = *reinterpret_cast<timeval*>(0x81A19BC);

	if (!initialized)
	{
		gettimeofday(&initialTime, NULL);
		initialized = true;
	}

	struct timeval tv;
	gettimeofday(&tv, NULL);
	return 1000 * (tv.tv_sec - initialTime.tv_sec) + (tv.tv_usec - initialTime.tv_usec) / 1000;
}

RakNetTimeNS HOOK_RakNet_GetTimeNS()
{
	static bool& initialized = *reinterpret_cast<bool*>(0x81A19C4);
	static timeval& initialTime = *reinterpret_cast<timeval*>(0x81A19BC);

	if (!initialized)
	{
		gettimeofday(&initialTime, NULL);
		initialized = true;
	}

	struct timeval tv;
	gettimeofday(&tv, NULL);
	return tv.tv_usec - initialTime.tv_usec + 1000000LL * (tv.tv_sec - initialTime.tv_sec);
}

bool Unlock(void* address, unsigned int len)
{
	size_t iPageSize = getpagesize();
	size_t iAddr = (reinterpret_cast<unsigned int>(address) / iPageSize) * iPageSize;
	return !mprotect(reinterpret_cast<void*>(iAddr), len, PROT_READ | PROT_WRITE | PROT_EXEC);
}

void InstallJump(void* addr, void* func)
{
	Unlock(addr, 5);
	*reinterpret_cast<uint8_t*>(addr) = 0xE9;
	*reinterpret_cast<uint32_t*>(addr + 1) = (reinterpret_cast<uint32_t>(func) - reinterpret_cast<uint32_t>(addr) - 5);
}

PLUGIN_EXPORT unsigned int PLUGIN_CALL Supports() 
{
	return SUPPORTS_VERSION;
}

PLUGIN_EXPORT bool PLUGIN_CALL Load(void **ppData) 
{
	InstallJump(0x805FBE0, HOOK_RakNet_GetTime);
	InstallJump(0x805FC60, HOOK_RakNet_GetTimeNS);
	return true;
}
