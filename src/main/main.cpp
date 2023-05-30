#include <std_include.hpp>

#include <utils/finally.hpp>
#include <utils/hook.hpp>
#include <utils/nt.hpp>
#include <utils/io.hpp>

namespace
{
}

int main()
{
	if (!utils::nt::is_wine())
	{
		printf("Application must be running within a Wine environment!\n");
		return 1;
	}

	printf("Hello World!\n");
	return 0;
}
