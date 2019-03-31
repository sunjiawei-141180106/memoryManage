#include <iostream>
#include <stdio.h>
#include <sys/types.h>
#include <sys/unistd.h>

using std::cout;
using std::endl;

///////////////////////////// 对已分配内存缺乏记录，不便于内存管理 ///////////////////////////
void* malloc_s(size_t size)
{
    void* p;
    p = sbrk(0);        // 返回当前break指针
    // sbrk fail, return NULL
    if (sbrk(size) == (void*)-1)
        return NULL;
    return p;
}
///////////////////////////////////////////////////////////////////////////////////////////

////////////////////////////////////// malloc正式实现 /////////////////////////////////////
typedef struct s_block* t_block;

struct s_block
{
    size_t size;        // 数据区大小
    t_block next;       // 指向下个块的指针
    t_block prev;
    int free;           // 是否为空闲块
    void* ptr;          // 指向数据块区域，若b->ptr == b->data, b极可能是有效block
    char data[1];       // 虚拟字段，表示数据块第一个字节
};

const size_t BLOCK_SIZE = sizeof(struct s_block);

void* base = NULL;

// 寻找合适block
t_block find_block(t_block *last, size_t size)
{
    t_block b = (t_block)base;
    // 1. b是NULL时，last记录最后一个访问到的数据块，无可用block, 方便开辟新block时使用
    // 2. block空闲且满足所请求空间大小时，block就是所请求数据块
    while (b && !(b->free && b->size >= size))
    {
        *last = b;
        b = b->next;
    }
    return b;
}

// 开辟新的block
// find_block返回NULL, 在链表尾部开辟新block
t_block extend_heap(t_block last, size_t size)
{
    t_block b;
    b = (t_block)sbrk(0);        // 定位当前break位置
    if (sbrk(sizeof(struct s_block) + size) == (void*)-1)   // 尝试调整break位置
        return NULL;    // 没有足够空间
    b->size = size;
    b->next = NULL;
    if (last)           // 这个last是指向extend最后一个block
        last->next = b; 
    b->free = 0;
    return b;
}

// 分裂block
// 申请size远小于查找到的block，第一个满足条件的block,导致较大内存碎片产生
// 剩余数据区足够大，分裂成一个新的block
void split_block(t_block b, size_t size)
{
    t_block n;            // 新的空闲block = 待分裂block - 申请分配出去内存
    n = (t_block)(b->data + size);   // new定位到剩下的数据块区域
    n->size = b->size - size - BLOCK_SIZE;
    n->next = b->next;    // 链表插入
    n->free = 1;          // 空闲可用
    b->size = size;         // 申请空间大小调整为size
    b->next = n;          // 链表插入
}

inline size_t align4(size_t size)
{
    return (((size - 1) >> 2) << 2) + 4;
}

void* malloc(size_t size)
{
    t_block f, l;              // 寻找到的block，last
    size_t s = align4(size);  // 按4字节对齐请求内存大小
    if (base)
    {
        l = (t_block)base;            // base非空，从base开始找空闲block
        f = find_block(&l, s);   // 从base开始找合适block
        if (f)
        {
            // 超过一定字节(8),则进行分裂操作
            if (f->size >= s + BLOCK_SIZE + 8)
                split_block(f, s);
            f->free = 0;            // 设置空闲标记
			cout << "malloc: find block" << endl;
        }
        else
        {
            // 没有合适块，扩展堆
            f = extend_heap(l, s);
            if (!f)
                return NULL;
			cout << "malloc: extend heap success" << endl;
        }
    }
    else
    {
        f = extend_heap(NULL, s);
        // 分配不成功
        if (!f)
            return NULL;
        base = f;		
		cout << "malloc: base NULL, extend heap" << endl;
    }
	cout << "f->data: " << (void*)f->data << endl; 
	f->ptr = (void*)f->data;		// ptr成员指向数据快第一个字节
	cout << "f->size: " << f->size << endl;
	cout << "f->ptr: " << f->ptr << endl;
	cout << "malloc success" << endl;
    return f->data;
}
//////////////////////////////////////// malloc正式实现 //////////////////////////////

//////////////////////////////////////// calloc实现 //////////////////////////////
// 给一组对象分配内存
void* calloc(size_t numitems, size_t size)
{
    size_t* n;
    size_t s, i;
    n = (size_t*)malloc(numitems * size);
    if (n)
    {
        // 申请内存总是4的倍数，以4B为单位初始化
        s = (align4(numitems * size) >> 2);
		cout << "numitems: " << numitems << "\tsize: " << size << endl; 
		cout << s << endl;
        for (i = 0; i < s; ++i)
            n[i] = 0;
		cout << "calloc success and set to 0" << endl;
    }    
    return n;
}
/////////////////////////////////////// calloc实现  ///////////////////////////////


/////////////////////////////////////// free实现  /////////////////////////////////
// 1.验证传入地址是否有效(malloc分配)
// 2.碎片问题

// 相邻空闲内存合并
t_block fusion(t_block b)
{
    if (b->next && b->next->free)
    {
        b->size += BLOCK_SIZE + b->next->size;  // 合并后block大小
        b->next = b->next->next;                // 链表节点删除
        if (b->next)
            b->next->prev = b;                  // 双向链表
    }
    return b;
}

// 根据给定地址得到对应block
// 通过偏移量得到的block可能有效, 可能无效
t_block get_block(void* p)
{
    char* tmp = (char*)p;
	//cout << "get_block tmp: " << (void*)tmp << endl;
	tmp -= (BLOCK_SIZE - 8);
	//cout << "get_block tmp: " << (void*)tmp << endl;
	p = (void*)tmp;
	//cout << "((t_block)p)->ptr: " << ((t_block)p)->ptr << endl;
    return (t_block)p;
}

// 验证是否为有效block
int valid_addr(void* p)
{
    if (base)
    {
        if (p > base && p < sbrk(0))
		{
            return p == (get_block(p))->ptr;    // 验证两个字段是否相同
		}    
	}
    return 0;
}

void free(void* p)
{
    t_block b;
    if (valid_addr(p))      // 验证地址有效性
    {
		cout << "valid address" << endl;
        b = get_block(p);   // 得到对应block
		cout << "free b: " << (void*)b << endl;
        b->free = 1;

        // 如果相邻上一块内存空闲就合并
        // 合并之后上一块还是空闲就继续合并，直到无法合并c
		if (!b->prev)
			cout << "b->prev: NULL" << endl;
		int time = 0;
        while (b->prev && b->prev->free)
            b = fusion(b->prev);
		if (b->next)
		{
			cout << "b->next: not NULL" << endl;
			cout << "b->next: " << (void *)(b->next) << endl;
			cout << "size of b->next: " << b->next->size << endl;
		}
        // 同理合并后面的空闲块
        while (b->next && b->next->free)
		{
			cout << "b->next->size: " << b->next->size << endl;
			cout << "b->next->free: " << b->next->free << endl;
            b = fusion(b);
			cout << "time" << ++time << ": " << "size " << b->size << endl;
		}
		cout << "free merge success" << endl;
        // 当前block是最后的那个block, 调整break指针
        if (NULL == b->next)
        {
            if (b->prev)        // 当前block前面还有占用的block
                b->prev->next = NULL;
            else                // 最后一个block
                base = NULL;    // 重置base
            brk(b);             // 调整break到b地址位置
			cout << "free: modify break" << endl;
        }
    }
	cout << "free success" << endl;
}
/////////////////////////////////////// free实现  /////////////////////////////////


/////////////////////////////////////// realloc实现 ///////////////////////////////

void copy_block(t_block src, t_block dst)
{
    int *sdata, *ddata;
    size_t i;
    sdata = (int*)src->ptr;
    ddata = (int*)dst->ptr;

    for (i = 0; 4 * i < src->size && 4 * i < dst->size; ++i)
        ddata[i] = sdata[i];
}

void* realloc(void* p, size_t size)
{
    size_t s;
    t_block b, n;     // 存放原来block, 新block(必要时malloc)
    void* newp;

    if (!p)
	{
		cout << "realloc use malloc NULL" << endl;
        return malloc(size);
	}
	cout << "base: " << base << endl;
	cout << "sbrk(0): " << sbrk(0) << endl;
	cout << "p: " << p << endl;
	cout << "get_block(p)->ptr: " << get_block(p)->ptr << endl;
    if (valid_addr(p))
    {
		cout << "valid address" << endl;
        s = align4(size);       // 按4字节对齐
        b = get_block(p);       // 得到对应block
		cout << "b: " << (void*)b << endl;
        if (b->size >= s)       // 1. size变小考虑split
        {
            if (b->size - s >= (BLOCK_SIZE + 4))    // 2. 可分割
                split_block(b, s);
			cout << "realloc use raw block" << endl;
        }
        else    // 当前block不能满足size
        {
            // 3. 后继block free, 合并后大小满足size, 考虑合并
            if (b->next && b->next->free && 
                (b->size + BLOCK_SIZE + b->next->size) >= s)
            {
                fusion(b);
                // 合并后满足size, 看能不能split
                if (b->size - s >= (BLOCK_SIZE + 4))
                    split_block(b, s);
				cout << "realloc use merged block" << endl;
            }
            else    // 4. 都不满足，malloc新区域
            {
                newp = malloc(s);
                if (!newp)  // 分配失败
				{
					cout << "realloc fails with malloc" << endl;
                    return NULL;
				}	
                n = get_block(newp);  
                copy_block(b, n);
				cout << "realloc copy success" << endl;
                free(p);        // 释放原来malloc释放的内存
				cout << "realloc use malloc" << endl;
                return newp;
            }
        }
        return p;       // 不需要重新malloc时，当前block大于size时(可以是合并后)
    }
	cout << "Invalid address" << endl;
    return NULL;        // 无效地址
}
/////////////////////////////////////// realloc实现 ///////////////////////////////

int main(int argc, char const *argv[])
{
	cout << "----------------malloc--------------------" << endl;
    int* a = (int*)malloc(10);
	cout << "a: " << (void*)a << endl;
	t_block block_a = get_block(a);
	cout << "get_block(a): " << (void*)(block_a) << endl;
	cout << "block of a: " << (void *)(block_a->data) << endl;
	cout << "\tsize: " << block_a->size << endl;
	cout << "\tdata: " << (void*)block_a->data << endl;
	cout << "\tptr: " << block_a->ptr << endl;
    cout << sizeof(a) << endl;      // 4
	cout << "----------------malloc--------------------" << endl << endl;
	cout << "----------------calloc--------------------" << endl;
	int* p = (int*)calloc(80, sizeof(int));
	cout << "----------------calloc--------------------" << endl << endl;
	cout << "----------------realloc--------------------" << endl;
	cout << "a: " << (void*)((char*)a - BLOCK_SIZE) << endl;
	realloc(a, 80);
	cout << "----------------realloc--------------------" << endl << endl;
	cout << "-----------------free----------------------" << endl;
	free(a);
	cout << "-----------------free----------------------" << endl << endl;
    s_block b;
    cout << sizeof(b) << endl;      // 16
    return 0;
}
