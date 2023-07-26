#include <std_include.hpp>

#include <utils/hook.hpp>
#include <utils/nt.hpp>
#include <utils/io.hpp>

#include <Windows.h>
#include <cstdio>

#include <fstream>
#include <regex>

#include "elf.h"

Elf64_Phdr* elf_get_phdr(void* base, int type)
{
	int i;
	Elf64_Ehdr* ehdr;
	Elf64_Phdr* phdr;

	// sanity check on base and type
	if (base == NULL || type == PT_NULL) return NULL;

	// ensure this some semblance of ELF header
	if (*(uint32_t*)base != 0x464c457fUL) return NULL;

	// ok get offset to the program headers
	ehdr = (Elf64_Ehdr*)base;
	phdr = (Elf64_Phdr*)((uint8_t*)base + ehdr->e_phoff);

	// search through list to find requested type
	for (i = 0; i < ehdr->e_phnum; i++)
	{
		// if found
		if ((int)phdr[i].p_type == type)
		{
			// return pointer to it
			return &phdr[i];
		}
	}
	// return NULL if not found
	return NULL;
}

uint64_t elf_get_delta(void* base)
{
	Elf64_Phdr* phdr;
	uint64_t low{};

	// get pointer to PT_LOAD header
	// first should be executable
	phdr = elf_get_phdr(base, PT_LOAD);

	if (phdr != NULL)
	{
		low = phdr->p_vaddr;
	}
	return (uint64_t)base - low;
}

// return pointer to first dynamic type found
Elf64_Dyn* elf_get_dyn(void* base, int tag)
{
	Elf64_Phdr* dynamic;
	Elf64_Dyn* entry;

	// 1. obtain pointer to DYNAMIC program header
	dynamic = elf_get_phdr(base, PT_DYNAMIC);

	if (dynamic != NULL)
	{
		entry = (Elf64_Dyn*)(dynamic->p_vaddr + elf_get_delta(base));
		// 2. obtain pointer to type
		while (entry->d_tag != DT_NULL)
		{
			if (entry->d_tag == tag)
			{
				return entry;
			}
			entry++;
		}
	}
	return NULL;
}

uint32_t elf_hash(const uint8_t* name)
{
	uint32_t h = 0, g;

	while (*name)
	{
		h = (h << 4) + *name++;
		g = h & 0xf0000000;
		if (g)
			h ^= g >> 24;
		h &= ~g;
	}
	return h;
}

void* elf_lookup(
	const char* name,
	uint32_t* hashtab,
	Elf64_Sym* sym,
	const char* str)
{
	uint32_t idx;
	uint32_t nbuckets = hashtab[0];
	uint32_t* buckets = &hashtab[2];
	uint32_t* chains = &buckets[nbuckets];

	for (idx = buckets[elf_hash((const uint8_t*)name) % nbuckets];
	     idx != 0;
	     idx = chains[idx])
	{
		// does string match for this index?
		if (!strcmp(name, sym[idx].st_name + str))
			// return address of function
			return (void*)sym[idx].st_value;
	}
	return NULL;
}

#define ELFCLASS_BITS 64

uint32_t gnu_hash(const uint8_t* name)
{
	uint32_t h = 5381;

	for (; *name; name++)
	{
		h = (h << 5) + h + *name;
	}
	return h;
}

struct gnu_hash_table
{
	uint32_t nbuckets;
	uint32_t symoffset;
	uint32_t bloom_size;
	uint32_t bloom_shift;
	uint64_t bloom[1];
	uint32_t buckets[1];
	uint32_t chain[1];
};

void* gnu_lookup(
	const char* name, /* symbol to look up */
	const void* hash_tbl, /* hash table */
	const Elf64_Sym* symtab, /* symbol table */
	const char* strtab /* string table */
)
{
	struct gnu_hash_table* hashtab = (struct gnu_hash_table*)hash_tbl;
	const uint32_t namehash = gnu_hash((const uint8_t*)name);

	const uint32_t nbuckets = hashtab->nbuckets;
	const uint32_t symoffset = hashtab->symoffset;
	const uint32_t bloom_size = hashtab->bloom_size;
	const uint32_t bloom_shift = hashtab->bloom_shift;

	const uint64_t* bloom = (const uint64_t*)&hashtab->bloom;
	const uint32_t* buckets = (const uint32_t*)&bloom[bloom_size];
	const uint32_t* chain = &buckets[nbuckets];

	uint64_t word = bloom[(namehash / ELFCLASS_BITS) % bloom_size];
	uint64_t mask = 0
		| (uint64_t)1 << (namehash % ELFCLASS_BITS)
		| (uint64_t)1 << ((namehash >> bloom_shift) % ELFCLASS_BITS);

	if ((word & mask) != mask)
	{
		return NULL;
	}

	uint32_t symix = buckets[namehash % nbuckets];
	if (symix < symoffset)
	{
		return NULL;
	}

	/* Loop through the chain. */
	for (;;)
	{
		const char* symname = strtab + symtab[symix].st_name;
		const uint32_t hash = chain[symix - symoffset];
		if ((namehash | 1) == (hash | 1) && strcmp(name, symname) == 0)
		{
			return (void*)symtab[symix].st_value;
		}
		if (hash & 1) break;
		symix++;
	}
	return 0;
}

void* get_proc_address(void* module, const char* name)
{
	Elf64_Dyn *symtab, *strtab, *hash;
	Elf64_Sym* syms;
	char* strs;
	void* addr = NULL;

	// 1. obtain pointers to string and symbol tables
	strtab = elf_get_dyn(module, DT_STRTAB);
	symtab = elf_get_dyn(module, DT_SYMTAB);

	if (strtab == NULL || symtab == NULL) return NULL;

	// 2. load virtual address of string and symbol tables
	strs = (char*)strtab->d_un.d_ptr;
	syms = (Elf64_Sym*)symtab->d_un.d_ptr;

	// 3. try obtain the ELF hash table
	hash = elf_get_dyn(module, DT_HASH);

	// 4. if we have it, lookup symbol by ELF hash
	if (hash != NULL)
	{
		addr = elf_lookup(name, (uint32_t*)hash->d_un.d_ptr, syms, strs);
	}
	else
	{
		// if we don't, try obtain the GNU hash table
		hash = elf_get_dyn(module, DT_GNU_HASH);
		if (hash != NULL)
		{
			addr = gnu_lookup(name, (void*)hash->d_un.d_ptr, syms, strs);
		}
	}
	// 5. did we find symbol? add base address and return
	if (addr != NULL)
	{
		addr = (void*)((uint64_t)module + (uint64_t)addr);
	}
	return addr;
}

void* wrap(void* func)
{
	return utils::hook::assemble([func](utils::hook::assembler& a)
	{
		a.push(rax);
		a.pushad64();

		a.mov(rdi, rcx); // 1
		a.mov(rsi, rdx); // 2
		a.mov(rdx, r8); // 3
		a.mov(rcx, r9); // 4
		a.mov(r8, qword_ptr(rsp, 0x90)); // 5
		a.mov(r9, qword_ptr(rsp, 0x98)); // 6

		// 7
		a.mov(rax, qword_ptr(rsp, 0xA0));
		a.push(rax);

		// 8
		a.mov(rax, qword_ptr(rsp, 0xB0));
		a.push(rax);

		a.call_aligned(func);

		a.add(rsp, 0x10);

		a.mov(qword_ptr(rsp, 0x80), rax);
		a.popad64();
		a.pop(rax);

		a.ret();
	});
}

int main()
{
	if (!utils::nt::is_wine())
	{
		printf("Application must be running within a Wine environment!\n");
		return 1;
	}

	printf("[*] We are running in wine :)\n");
	printf("[*] Wine pid: %d\n", GetCurrentProcessId());

	const auto linux_getpid = static_cast<int(*)()>(utils::hook::assemble([](utils::hook::assembler& a)
	{
		a.push(rax);
		a.pushad64();

		a.mov(rax, 39);
		a.syscall();

		a.mov(qword_ptr(rsp, 0x80), rax);
		a.popad64();
		a.pop(rax);

		a.ret();
	}));

	const auto lin_pid = linux_getpid();
	printf("[*] Real pid: %d\n", lin_pid);

	// /proc/self/maps seems incomplete :(
	std::ifstream t("Z:\\proc\\" + std::to_string(lin_pid) + "\\maps");
	std::string str((std::istreambuf_iterator<char>(t)),
	                std::istreambuf_iterator<char>());

	static const std::regex reg{R"(([0-9a-fA-F]+)-[0-9a-fA-F]+ [^ ]+ \d+ \d\d:\d\d \d+\s+([^ ]+\/libdl-[^ ]+\.so))"};
	static const std::regex reg2{R"(([0-9a-fA-F]+)-[0-9a-fA-F]+ [^ ]+ \d+ \d\d:\d\d \d+\s+([^ ]+\/libc.so.*))"};

	std::smatch match{};
	if (!std::regex_search(str, match, reg))
	{
		if (!std::regex_search(str, match, reg2))
		{
			printf("Bad search :(\n");
			return 1;
		}
	}

	printf("[*] Resolving libc: %s - %s\n", match[1].str().data(), match[2].str().data());

	auto* data = reinterpret_cast<uint8_t*>(strtoull(match[1].str().data(), nullptr, 16));
	const auto& elf_hdr = *reinterpret_cast<Elf64_Ehdr*>(data);
	if (memcmp(&elf_hdr, "\x7F" "ELF", 4) != 0)
	{
		printf("Bad elf :(\n");
		return 1;
	}
	
	auto* dlsym = get_proc_address(data, "dlsym");
	printf("[*] Resolving: dlsym: % p\n", dlsym);

	printf("[*] Creating calling convention wrapper...\n");
	auto* dlsym_func = (((void* (*)(void*, const char*))wrap(dlsym)));

	auto* dlopen = dlsym_func(nullptr, "dlopen");
	printf("[*] Resolving dlopen: % p\n", dlopen);

	auto* memcopy = dlsym_func(nullptr, "memcpy");
	printf("[*] Resolving libc memcpy: % p\n", memcopy);

	char buffer[0x100]{0};
	std::string _str = "Hello World!";

	((void(*)(void*, const void*, size_t))wrap(memcopy))(buffer, _str.data(), _str.size() + 1);

	printf("[*] Performing linux memcpy: %s\n", buffer);

	return 0;
}
