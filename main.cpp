//
//  main.cpp
//  GarbageCollector
//
//  Created by Samuel Williams on 2/09/11.
//  Copyright 2011 Orion Transfer Ltd. All rights reserved.
//

#include <iostream>
#include <sstream>
#include <map>
#include <vector>
#include <algorithm>
#include <cstddef>
#include <cstdlib>
#include <cstring>
#include <cassert>

//TODO: Merging of adjacent unused blocks.
//TODO: Fix collection of teststruct in realease mode.


/** Creates a new pointer that points to the location specified by pointer plus offset in bytes.

@param ptr: Base pointer from which the new pointer shall be created.
@param bytes: offset from the address in ptr.
*/
inline void* create_pointer_by_offset(void* ptr, std::size_t bytes){
	return static_cast<void*>(static_cast<char*>(ptr) + bytes);
}

template<typename T>
T* create_pointer_by_offset(T* ptr, std::size_t bytes){
	return reinterpret_cast<T*>(reinterpret_cast<char*>(ptr)+bytes);
}

class AllocationException : public std::runtime_error{
public:
	AllocationException(const char* msg):
		std::runtime_error(msg)
	{}
};

enum BlockState : short{
	Used = 0,
	Unused = -1,
	Freed = -2
};

/// A very basic stack-only garbage collector.
/// This was to enjoy writing some code to see how garbage collection works.
/// Everything else is just an implementation detail or optimisation =p
class GarbageCollector final{
	protected:

		struct Chunk{
			Chunk(void * ptr_, std::size_t used_size_, std::size_t max_size_) :
				ptr(ptr_),
				used_size(used_size_),
				max_size(max_size_)
			{}
			void * ptr = nullptr;
			std::size_t used_size = 0;
			std::size_t max_size = 0;
		};

		struct AllocatedBlock{
			AllocatedBlock(void * ptr_, std::size_t size_, unsigned short mark_, std::vector<Chunk>::size_type chunk_idx_) :
				ptr(ptr_),
				size(size_),
				mark(mark_),
				chunk_idx(chunk_idx_)
			{}
			void * ptr = nullptr;
			std::size_t size = 0;
			short mark = 0;
			std::vector<Chunk>::size_type chunk_idx = 0;
		};

		using AllocationVector = std::vector < AllocatedBlock >;

		static const std::size_t CHUNK_SIZE = 1024 * 1024; // 1024 bytes * 1024 = 1 MB

		AllocationVector m_allocs;
		std::vector<Chunk> m_chunks;
		std::vector<Chunk>::size_type m_current_chunk_idx = 0;
		AllocationVector::size_type   m_next_free_allocated_block_idx = 0;

	public:
		/*
		The GarbageCollector needs a pointer to the top of the stack. If the whole program shall be checked then a pointer to the supplied arguments
		in main(int argc, const char* argv) would be ideal. For Example: GarbageCollector gc(&argc). 
		This makes sure that no pointer in the same scope as the collector are missed because the compiler places them above the collector on the stack,
		even though they appear later in the code.
		If that is not necessary, the pointer may be omitted and the address of the collector (this) is used as the top of the stack.
		*/
		GarbageCollector(void* stack_top = nullptr):
			m_top(stack_top == nullptr ? this : stack_top)
		{
			Chunk chunk = alloc_chunk();
			
			m_chunks.push_back(std::move(chunk));
			m_allocs.emplace_back(chunk.ptr, CHUNK_SIZE, BlockState::Unused, m_current_chunk_idx);
		}
		
		void * allocate(std::size_t size);
		
		template<typename T>
		T allocate(std::size_t size);

		template<typename T, typename... Args>
		T* gc_new(Args&&... args);
		
		void collect();
    private:

		Chunk alloc_chunk(std::size_t size = CHUNK_SIZE);
		void free_chunk(std::vector<Chunk>::size_type chunk_idx, std::stringstream& log_stream);
		void free_chunk(Chunk& chunk, std::stringstream& log_stream);
		void realease_block(AllocatedBlock& block);
		void scan_stack(void* top, void** current, std::stringstream& log_stream);
		void combine_unused_blocks(std::stringstream& log_stream);
		bool are_blocks_adjacent(const AllocatedBlock& block1, const AllocatedBlock& block2);

		void* m_top;
};

void* GarbageCollector::allocate(std::size_t size) {

	// search for unused memory in which the new allocation could fit:
	// There should be enough memory present in the current chunk. Try to fit the allocation in the next free allocated memory block:
	void* pointer = nullptr;
	auto& next_free_block = m_allocs[m_next_free_allocated_block_idx];
	if (next_free_block.mark == BlockState::Unused && next_free_block.size >= size) { // make sure that the next free block really is free (mark == -1) and its size is big enough.
		pointer = next_free_block.ptr;
		m_chunks[next_free_block.chunk_idx].used_size += size;
#ifdef DEBUG
		std::clog << "Allocating " << size << " bytes of memory at " << pointer << std::endl;
#endif

		if (next_free_block.size > size) { // only if there is still memory free after the allocation, the allocated block is divided by creating a new allocated block 
			// warning: order is important here, because adding to m_allocs, invalidates all pointers and references to its content!

			next_free_block.ptr = create_pointer_by_offset(next_free_block.ptr, size); // The already existing allocated block consists of the remaining free memory 

			next_free_block.size -= size;											// and its fields are adjusted accordingly.
			auto chunk_idx = next_free_block.chunk_idx;								// we need to obtain the chunk idx, before calling emplace. emplace() first allocates 
																					// new memory which invalidates the next_free_block reference.
			// After altering the next_free_block reference we can now safely add something to m_allocs and possibly invalidating all pointers/references.
			m_allocs.emplace_back(pointer, size, 0, chunk_idx);
		} else { // the requested size is as big as the allocated block. Just leave it in place and reset it in use by changing the mark to zero.
			next_free_block.mark = 0;
		}
	} else {
		for (auto& alloc : m_allocs) {
			if (alloc.mark == BlockState::Unused && alloc.size >= size) {
				pointer = alloc.ptr;
				m_allocs.emplace_back(pointer, size, BlockState::Used, alloc.chunk_idx);

				alloc.ptr = create_pointer_by_offset(alloc.ptr, size);
				alloc.size -= size;
				m_chunks[alloc.chunk_idx].used_size += size;
			}
		}
	}

	if (pointer == nullptr) { // there was no unused memory block big enough to accommodate our request.
								// Therefore request a new chunk of memory from the os.
		auto chunk = alloc_chunk(size);

		chunk.used_size = size;

		m_chunks.push_back(std::move(chunk));
		m_current_chunk_idx++;

		pointer = chunk.ptr;
		
		// create the allocated block for the requested memory.
		m_allocs.emplace_back(pointer, size, BlockState::Used, m_current_chunk_idx);

		// create an allocated block for the remaining unused memory.
		auto ptr = create_pointer_by_offset(pointer, size);

		m_allocs.emplace_back(ptr, size, BlockState::Unused, m_current_chunk_idx);

		m_next_free_allocated_block_idx = m_allocs.size() - 1;
	}

	assert(pointer != nullptr);
	return pointer;
}

template<typename T>
T GarbageCollector::allocate(std::size_t size){
	return static_cast<T>(allocate(size));
}

template<typename T, typename... Args>
T* GarbageCollector::gc_new(Args&&... args){
	T* pointer = allocate<T*>(sizeof(T));
	
	return new(pointer)T(std::forward<Args>(args)...);
}

// This will only collect pointers _directly_ on the stack.
void GarbageCollector::collect() {
	// We expect for this to work, the GarbageCollector must be allocated at the top of the stack.
	void * top = m_top;

	// Then we grab the address of the stack at the current point.
	void ** current = (void **)&top;
	std::stringstream log_stream;
	log_stream << "Collecting from " << current << "(" << reinterpret_cast<int>(current) << ") to " << top << "(" << reinterpret_cast<int>(top) << ")" << std::endl;
	log_stream << "diff: " << (reinterpret_cast<int>(top)-reinterpret_cast<int>(current)) / sizeof(void*) << std::endl;
	
	// Clear all the marks.
	for (auto& root : m_allocs) {
		root.mark = root.mark > 0 ? 0 : root.mark; // we can't just set the mark to zero because we would erase the marks(-1) of the unused blocks. That information needs to be preserved.
	}

	scan_stack(top, current, log_stream);

	unsigned freed = 0;
	unsigned survived = 0;
	//Scan through all roots again and free any items that were not marked.
	auto root = m_allocs.begin();
	while ( root != m_allocs.end()) {
		if (root->mark == 0) {
			log_stream << "Releasing " << root->size << " bytes at " << root->ptr << std::endl;

			realease_block(*root);

			++freed;
		} else if (root->mark > 0) {
			survived++;
		}
		root++;
	}

	// Free chunks that are completely released.
	int i = 0;
	for (auto& chunk : m_chunks) {
		if (chunk.used_size == 0) {
			free_chunk(chunk, log_stream);
			// ToDo: What happens with the unused allocated blocks referencing the freed chunk?
			for (auto& block : m_allocs) {
				if (block.chunk_idx == i) {
					block.mark = BlockState::Freed;
					block.ptr = nullptr;
				}
			}
		}
		++i;
	}

	// combine adjacent unused blocks to bigger unused blocks:
	combine_unused_blocks(log_stream);

	log_stream << "freed: " << freed << " survived: " << survived << std::endl;
	std::cout << log_stream.rdbuf();
}

GarbageCollector::Chunk GarbageCollector::alloc_chunk(std::size_t size){
	std::size_t size_to_allocate = size < CHUNK_SIZE ? CHUNK_SIZE : size;
	void * pointer = malloc(size_to_allocate); // the minimal amount of memory requested from the os is at least one size of a chunk.

	if (pointer) {
		std::cout << "Allocating Chunk " << m_current_chunk_idx + 1 << " with " << CHUNK_SIZE/1024.0 << " kilobytes of memory at " << pointer << std::endl;
		return{ pointer, 0, size_to_allocate };
	} else {
		throw AllocationException(("Error allocating Chunk of " + std::to_string(CHUNK_SIZE/1024.0) + " kB").c_str());
	}
}

void GarbageCollector::scan_stack(void* top, void** current, std::stringstream& log_stream){
	unsigned i = 0;
	// We scan the stack and mark all pointers we find.
	log_stream << "[i]\tstack pos.\tpoints to" << std::endl;
	while (current < top) {
		void * pointer = *current;
		log_stream << "[" << i << "]:\t" << current << "\t" << pointer << std::endl;//" dec: " << reinterpret_cast<int>(pointer) << std::endl;

		auto it = std::find_if(m_allocs.begin(), m_allocs.end(), [pointer](const AllocatedBlock& alloc_info){ return pointer == alloc_info.ptr; });

		if (it != m_allocs.end()) {
			log_stream << "Found allocation " << pointer << " at " << current << std::endl;
			it->mark += 1;
		}

		// Move to next pointer
		current++;
		i++;
	}
}

void GarbageCollector::free_chunk(std::vector<Chunk>::size_type chunk_idx, std::stringstream& log_stream){
#ifdef DEBUG
	log_stream << "freeing chunk " << chunk_idx << ":";
#endif
	free_chunk(m_chunks[chunk_idx], log_stream);
}

void GarbageCollector::free_chunk(Chunk& chunk, std::stringstream& log_stream){
	free(chunk.ptr);
#ifdef DEBUG
	log_stream << " Chunk pointing to " << chunk.ptr << " containing " << chunk.max_size / 1024.0 << " kilobytes freed\n";
#endif
	chunk.ptr = nullptr;
	chunk.used_size = 0;
	chunk.max_size = 0;
}

void GarbageCollector::realease_block(AllocatedBlock& block){
	assert(block.ptr != nullptr);

	block.mark = BlockState::Unused; // releasing an allocated block means to allow it's memory to be reused. This is signed by -1.

	m_chunks[block.chunk_idx].used_size -= block.size;
}

bool GarbageCollector::are_blocks_adjacent(const GarbageCollector::AllocatedBlock& block1, const GarbageCollector::AllocatedBlock& block2){
	return create_pointer_by_offset(block1.ptr, block1.size) == block2.ptr;
}

void GarbageCollector::combine_unused_blocks(std::stringstream& log_stream){
	for (size_t i = 0; i < m_allocs.size(); i++) {
		auto& block1 = m_allocs[i];
		if (block1.ptr != nullptr && block1.mark == BlockState::Unused) {
			for (size_t j = 0; j < m_allocs.size(); j++) {
				auto& block2 = m_allocs[j];
				if (block2.mark == BlockState::Unused && are_blocks_adjacent(block1, block2)) {
					block1.size += block2.size;
#ifdef DEBUG
					log_stream << "Combining block " << i << " beginning at " << block1.ptr << " with " << block1.size << " bytes with block " << j << " with " << block2.size << " bytes\n";
#endif
					block2.mark = BlockState::Freed;
					block2.size = 0;
				}
			}
		}

	}
}

void * operator new (std::size_t size, GarbageCollector & gc)
{
	return gc.allocate(size);
}

const char* helloWorld (GarbageCollector & gc)
{
	char * buffer = gc.allocate<char*>(256);

	strcpy(buffer, "00,1,2,3,4,5,6,7,8,9,10,11,12,13,14,15,16,17,18,19,20,21,22,23,24,25,"
				   "26,27,28,29,30,31,32,33,34,35,36,37,38,39,40,41,42,43,44,45,46,47,48,"
				   "49,50,51,52,53,54,55,56,57,58,59,60,61,62,63,64,65,66,67,68,69,70,71,"
				   "72,73,74,75,76,77,78,79,80,81,82,83,84,85,86,87.");
	
	std::cout << "Buffer pointer at " << &buffer << ", content: " << buffer << std::endl;

	return buffer;
}

struct GcTestStruct{

	GcTestStruct(char one_, double two_, const GcTestStruct* other_ = nullptr) :
		one(one_),
		two(two_),
		other(other_)
	{}
	double two = 3.14;
	int i = 0;
	const GcTestStruct* other = nullptr;
	char one = 'A';
};

int main (int argc, const char * argv[])
{
	GarbageCollector gc(&argc);

    std::cout << "How many allocations? ";
    unsigned allocs = 0;
    std::cin >> allocs;
    const char* buf = nullptr;
	std::size_t test = 0;

	std::cout << &argc << " " << &buf << " " << &test << " " << &gc << "\n";
	for (std::size_t i = 0; i < allocs; i++)
    {
		buf = helloWorld(gc);
        if( i == 0)
            test = reinterpret_cast<std::size_t>(buf);
    }
	//std::cout << "&buf: " << &buf << ", &test: " << &test << ", test: " << test << std::endl;
	std::system("pause");
	auto test_struct = gc.gc_new<GcTestStruct>('B', 6.28);
	std::cout << "test_struct->one: " << test_struct->one << ", test_struct->two: " << test_struct->two << std::endl;
	std::system("pause");
	test_struct = new(gc) GcTestStruct('B', 6.28, test_struct);
	std::cout << "test_struct->one: " << test_struct->one << ", test_struct->two: " << test_struct->two << std::endl;
	std::system("pause");
	gc.collect();
	std::system("pause"); std::cout << "test_struct->other->one: " << test_struct->other->one << ", test_struct->other->two: " << test_struct->other->two << std::endl;
	std::system("pause");
    return 0;
}
