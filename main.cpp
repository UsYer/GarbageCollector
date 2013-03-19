//
//  main.cpp
//  GarbageCollector
//
//  Created by Samuel Williams on 2/09/11.
//  Copyright 2011 Orion Transfer Ltd. All rights reserved.
//

#include <iostream>
#include <map>
#include <cstddef>
#include <cstdlib>
#include <cstring>

/// A very basic stack-only garbage collector.
/// This was to enjoy writing some code to see how garbage collection works.
/// Everything else is just an implementation detail or optimisation =p
class GarbageCollector {
	protected:
		struct Allocation {
			std::size_t size;
			unsigned short mark;
		};

		typedef std::map<void *, Allocation> PointerMap;

		PointerMap m_allocations;

	public:
		void * allocate(std::size_t size);
		void collect();
    private:

};

void* GarbageCollector::allocate(std::size_t size) {
	void * pointer = malloc(size);

	if (pointer) {
		std::cout << "Allocating " << size << " bytes of memory at " << pointer << std::endl;
		Allocation allocation = {size, 0};

		PointerMap::value_type pair(pointer, allocation);
		m_allocations.insert(pair);
	}

	return pointer;
}

// This will only collect pointers _directly_ on the stack.
void GarbageCollector::collect() {
	// We expect for this to work, the GarbageCollector must be allocated at the top of the stack.
	void * top = this;

	// Then we grab the address of the stack at the current point.
	void ** current = (void **)&top;
	std::cout << "Collecting from " << current << "(" << reinterpret_cast<int>(current) << ") to " << top << "(" << reinterpret_cast<int>(top) << ")" << std::endl;
    std::cout << "diff: " << (reinterpret_cast<int>(top) - reinterpret_cast<int>(current))/sizeof(void*) << std::endl;
	// Clear all the marks.
	for (auto& root : m_allocations) {
		root.second.mark = 0;
	}
//	 for (PointerMap::iterator root = m_allocations.begin(); root != m_allocations.end(); root++) {
//        root->second.mark = 0;
//    }
    unsigned i = 0;
    unsigned survived = 0;
	// We scan the stack and mark all pointers we find.
	std::cout << "[i]\tstack pos.\tpoints to" << std::endl;
	while (current < top) {
		void * pointer = *current;
		std::cout << "[" << i << "]:\t" << current << "\t" << pointer << std::endl;//" dec: " << reinterpret_cast<int>(pointer) << std::endl;
		PointerMap::iterator allocation = m_allocations.find(pointer);

		if (allocation != m_allocations.end()) {
			std::cerr << "Found allocation " << pointer << " at " << current << std::endl;
			allocation->second.mark += 1;
			survived++;
		}

		// Move to next pointer
		current++;
		// move the pointer just one byte forward, so we scan the entire stack via a window
//		unsigned char ** current_byte = reinterpret_cast<unsigned char **>(current);
//		current_byte++;
//		current = reinterpret_cast<void **>(current_byte);
		i++;
	}

	// Scan through all roots again and free any items that were not marked.
//	for (PointerMap::iterator root = m_allocations.begin(); root != m_allocations.end(); root++) {
//		if (root->second.mark == 0) {
//			std::cerr << "Releasing " << root->second.size << " bytes at " << root->first << std::endl;
//
//			free(root->first);
//
//			m_allocations.erase(root);
//		}
//	}
    unsigned freed = 0;
    for (const auto& root : m_allocations) {
		if (root.second.mark == 0) {
			std::cerr << "Releasing " << root.second.size << " bytes at " << root.first << std::endl;

			free(root.first);

			m_allocations.erase(root.first);
			++freed;
		}
	}
	std::cout << "freed: " << freed << " survived: " << survived << std::endl;
}

const char* helloWorld (GarbageCollector & gc)
{
	char * buffer = (char *)gc.allocate(256);
	std::cout << "Buffer pointer at " << &buffer << std::endl;

	strcpy(buffer, "Hello, World!");

	return buffer;
}

int main (int argc, const char * argv[])
{
	GarbageCollector gc;

    std::cout << "How many allocations? ";
    unsigned allocs = 0;
    std::cin >> allocs;
    const char* buf = nullptr;
	std::size_t test = 0;
	for (std::size_t i = 0; i < allocs; i++)
    {
		buf = helloWorld(gc);
        if( i == 0)
            test = reinterpret_cast<std::size_t>(buf);
    }
	std::cout << &buf << " " << test << std::endl;
    std::system("pause");
	gc.collect();
    std::system("pause");
    return 0;
}
