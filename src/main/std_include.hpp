#pragma once

#pragma warning(push)
#pragma warning(disable: 4100)
#pragma warning(disable: 4127)
#pragma warning(disable: 4244)
#pragma warning(disable: 4458)
#pragma warning(disable: 4702)
#pragma warning(disable: 4996)
#pragma warning(disable: 5054)
#pragma warning(disable: 5056)
#pragma warning(disable: 6011)
#pragma warning(disable: 6297)
#pragma warning(disable: 6385)
#pragma warning(disable: 6386)
#pragma warning(disable: 6387)
#pragma warning(disable: 26110)
#pragma warning(disable: 26451)
#pragma warning(disable: 26444)
#pragma warning(disable: 26451)
#pragma warning(disable: 26489)
#pragma warning(disable: 26495)
#pragma warning(disable: 26498)
#pragma warning(disable: 26812)
#pragma warning(disable: 28020)

#define WIN32_LEAN_AND_MEAN

#include <Windows.h>

// min and max is required by gdi, therefore NOMINMAX won't work
#ifdef max
#undef max
#endif

#ifdef min
#undef min
#endif

#include <cassert>
#include <cstring>
#include <cctype>

#include <atomic>
#include <chrono>
#include <filesystem>
#include <functional>
#include <map>
#include <mutex>
#include <optional>
#include <thread>
#include <vector>

#pragma warning(pop)
#pragma warning(disable: 4100)

#pragma comment(lib, "ntdll.lib")

using namespace std::literals;
