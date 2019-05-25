/*
Copyright(c) 2019 Anatoliy Kuznetsov(anatoliy_kuznetsov at yahoo.com)

Licensed under the Apache License, Version 2.0 (the "License");
you may not use this file except in compliance with the License.
You may obtain a copy of the License at

    http://www.apache.org/licenses/LICENSE-2.0

Unless required by applicable law or agreed to in writing, software
distributed under the License is distributed on an "AS IS" BASIS,
WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
See the License for the specific language governing permissions and
limitations under the License.

For more information please visit:  http://bitmagic.io
*/


//#define BMSSE2OPT
//#define BMSSE42OPT
//#define BMAVX2OPT

#define BM64ADDR

#include <stdio.h>
#include <stdlib.h>
#undef NDEBUG
#include <cassert>
#include <time.h>
#include <math.h>
#include <string.h>

#include <iostream>
#include <iomanip>
#include <utility>
#include <memory>
#include <random>
#include <algorithm>
#include <stdarg.h>
#include <vector>


#include <bm.h>
#include <bmdbg.h>

using namespace bm;
using namespace std;


#include "gena.h"
#include "test_util.h"


#define POOL_SIZE 5000

//#define MEM_POOL


template<class T> T* pool_allocate(T** pool, int& i, size_t n)
{
    return i ? pool[i--] : (T*) ::malloc(n * sizeof(T));
}

inline void* pool_allocate2(void** pool, int& i, size_t n)
{
    return i ? pool[i--] : malloc(n * sizeof(void*));
}



template<class T> void pool_free(T** pool, int& i, T* p)
{
    i < POOL_SIZE ? (free(p),(void*)0) : pool[++i]=p;
}


class pool_block_allocator
{
public:

    static bm::word_t* allocate(size_t n, const void *)
    {
        int *idx = 0;
        bm::word_t** pool = 0;

        switch (n)
        {
        case bm::set_block_size:
            idx = &bit_blocks_idx_;
            pool = free_bit_blocks_;
            break;

        case 64:
            idx = &gap_blocks_idx0_;
            pool = gap_bit_blocks0_;
            break;

        case 128:
            idx = &gap_blocks_idx1_;
            pool = gap_bit_blocks1_;
            break;
        
        case 256:
            idx = &gap_blocks_idx2_;
            pool = gap_bit_blocks2_;
            break;

        case 512:
            idx = &gap_blocks_idx3_;
            pool = gap_bit_blocks3_;
            break;

        default:
            assert(0);
        }

        return pool_allocate(pool, *idx, n);
    }

    static void deallocate(bm::word_t* p, size_t n)
    {
        int *idx = 0;
        bm::word_t** pool = 0;

        switch (n)
        {
        case bm::set_block_size:
            idx = &bit_blocks_idx_;
            pool = free_bit_blocks_;
            break;

        case 64:
            idx = &gap_blocks_idx0_;
            pool = gap_bit_blocks0_;
            break;

        case 128:
            idx = &gap_blocks_idx1_;
            pool = gap_bit_blocks1_;
            break;
        
        case 256:
            idx = &gap_blocks_idx2_;
            pool = gap_bit_blocks2_;
            break;

        case 512:
            idx = &gap_blocks_idx3_;
            pool = gap_bit_blocks3_;
            break;

        default:
            assert(0);
        }

        pool_free(pool, *idx, p);
    }

private:
    static bm::word_t* free_bit_blocks_[];
    static int         bit_blocks_idx_;

    static bm::word_t* gap_bit_blocks0_[];
    static int         gap_blocks_idx0_;

    static bm::word_t* gap_bit_blocks1_[];
    static int         gap_blocks_idx1_;

    static bm::word_t* gap_bit_blocks2_[];
    static int         gap_blocks_idx2_;

    static bm::word_t* gap_bit_blocks3_[];
    static int         gap_blocks_idx3_;
};

bm::word_t* pool_block_allocator::free_bit_blocks_[POOL_SIZE];
int pool_block_allocator::bit_blocks_idx_ = 0;

bm::word_t* pool_block_allocator::gap_bit_blocks0_[POOL_SIZE];
int pool_block_allocator::gap_blocks_idx0_ = 0;

bm::word_t* pool_block_allocator::gap_bit_blocks1_[POOL_SIZE];
int pool_block_allocator::gap_blocks_idx1_ = 0;

bm::word_t* pool_block_allocator::gap_bit_blocks2_[POOL_SIZE];
int pool_block_allocator::gap_blocks_idx2_ = 0;

bm::word_t* pool_block_allocator::gap_bit_blocks3_[POOL_SIZE];
int pool_block_allocator::gap_blocks_idx3_ = 0;




class pool_ptr_allocator
{
public:

    static void* allocate(size_t n, const void *)
    {
        return pool_allocate2(free_ptr_blocks_, ptr_blocks_idx_, n);
    }

    static void deallocate(void* p, size_t)
    {
        pool_free(free_ptr_blocks_, ptr_blocks_idx_, p);
    }

private:
    static void*  free_ptr_blocks_[];
    static int    ptr_blocks_idx_;
};

void* pool_ptr_allocator::free_ptr_blocks_[POOL_SIZE];
int pool_ptr_allocator::ptr_blocks_idx_ = 0;

#if defined(BMSSE2OPT) || defined(BMSSE42OPT) || defined(BMAVX2OPT) || defined(BMAVX512OPT)
#else
# define MEM_DEBUG
#endif

#ifdef MEM_DEBUG


class dbg_block_allocator
{
public:
static size_t na_;
static size_t nf_;

    static bm::word_t* allocate(size_t n, const void *)
    {
        ++na_;
        assert(n);
        bm::word_t* p =
            (bm::word_t*) ::malloc((n+1) * sizeof(bm::word_t));
        if (!p)
        {
            std::cerr << "ERROR Failed allocation!" << endl;
            exit(1);
        }
        *p = (bm::word_t)n;
        return ++p;
    }

    static void deallocate(bm::word_t* p, size_t n)
    {
        ++nf_;
        --p;
        if (*p != n)
        {
            printf("Block memory deallocation ERROR! n = %i (expected %i)\n", (int)n, (int)*p);
            assert(0);
            exit(1);
        }
        ::free(p);
    }

    static size_t balance()
    {
        return nf_ - na_;
    }
};

size_t dbg_block_allocator::na_ = 0;
size_t dbg_block_allocator::nf_ = 0;

class dbg_ptr_allocator
{
public:
static size_t na_;
static size_t nf_;

    static void* allocate(size_t n, const void *)
    {
        ++na_;
        assert(sizeof(size_t) == sizeof(void*));
        void* p = ::malloc((n+1) * sizeof(void*));
        if (!p)
        {
            std::cerr << "ERROR! Failed allocation!" << endl;
            exit(1);
        }
        size_t* s = (size_t*) p;
        *s = n;
        return (void*)++s;
    }

    static void deallocate(void* p, size_t n)
    {
        ++nf_;
        size_t* s = (size_t*) p;
        --s;
        if(*s != n)
        {
            printf("Ptr memory deallocation ERROR!\n");
            assert(0);
            exit(1);
        }
        ::free(s);
    }

    static size_t balance()
    {
        return nf_ - na_;
    }

};

size_t dbg_ptr_allocator::na_ = 0;
size_t dbg_ptr_allocator::nf_ = 0;


typedef mem_alloc<dbg_block_allocator, dbg_ptr_allocator, alloc_pool<dbg_block_allocator, dbg_ptr_allocator> > dbg_alloc;

typedef bm::bvector<dbg_alloc> bvect64;
typedef bm::bvector<dbg_alloc> bvect;
//typedef bm::bvector_mini<dbg_block_allocator> bvect_mini;
typedef bm::rs_index<dbg_alloc> rs_ind;

#else

#ifdef MEM_POOL

typedef mem_alloc<pool_block_allocator, pool_ptr_allocator> pool_alloc;
typedef bm::bvector<pool_alloc> bvect64;
typedef bm::bvector<pool_alloc> bvect;
//typedef bm::bvector_mini<bm::block_allocator> bvect_mini;
typedef bm::rs_index<pool_block_allocator> rs_ind;


#else

typedef bm::bvector<> bvect64;
typedef bm::bvector<> bvect;
//typedef bm::bvector_mini<bm::block_allocator> bvect_mini;
typedef bm::rs_index<> rs_ind;

#endif

#endif

typedef std::vector<bm::id64_t> ref_vect;

static
void SyntaxTest()
{
    cout << "------------------------------------ SyntaxTest()" << endl;
    ref_vect vect;
    generate_vect_simpl0(vect);
    {
        bvect64 bv0;

        load_BV_set_ref(bv0, vect);
        compare_BV_set_ref(bv0, vect);

        
        auto idx = vect.size()-1;
        auto v = vect[idx];
        assert(bv0.test(v));
        
        bvect64 bv1(bv0);
        cout << bv0.count() << endl;
        cout << bv1.count() << endl;        
        compare_BV_set_ref(bv1, vect);
        
        bvect64 bv2;
        bv2 = bv0;
        compare_BV_set_ref(bv1, vect);
        
        bvect64 bv3(std::move(bv2));
        assert(bv2.none());
        compare_BV_set_ref(bv3, vect);
        
        bvect64 bv4;
        bv4.move_from(bv3);
        assert(bv3.none());
        compare_BV_set_ref(bv4, vect);
        
        bv0 &= bv4;
        compare_BV_set_ref(bv0, vect);
        bv0 |= bv4;
        compare_BV_set_ref(bv0, vect);
        bv0 -= bv4;
        assert(bv0.none());
        bv1 ^= bv4;
        assert(bv1.none());
    }
    
    {
        bvect64 bv0, bv1, bv2;

        load_BV_set_ref(bv0, vect);
        bv1 = bv2 = bv0;
        bool b = (bv1 == bv0);
        assert(b);
        b = (bv1 == bv2);
        assert(b);
     
        {
            bvect64 bv3;
            bv3 = bv1 | bv2;
            b = (bv1 == bv3);
            assert(b);
            compare_BV_set_ref(bv3, vect);
        }
        {
            bvect64 bv3;
            bv3 = bv1 & bv2;
            b = (bv1 == bv3);
            assert(b);
            compare_BV_set_ref(bv3, vect);
        }
        {
            bvect64 bv3;
            bv3 = bv1 - bv2;
            assert(bv3.none());
        }
        {
            bvect64 bv3;
            bv3 = bv1 ^ bv2;
            assert(bv3.count() == 0ULL);
        }
    }
    
    {
        bvect64 bv1;
        bvect64::reference ref = bv1[10];
        bool bn = !ref;
        assert(bn);
        bool bn2 = ~ref;
        assert(bn2);
        bv1[10] = bn2;
        bv1[10] = bn;
        bn = bn2 = false;
        assert(!bn);
        ref.flip();
        assert(!bv1.test(10));
        bv1[bm::id_max-1] = 1;
        assert(bv1[bm::id_max-1]);
        auto ref1 = bv1[bm::id_max-1];
        ref1.flip();
        assert(!ref1);
        assert(!bv1.test(bm::id_max-1));
    }
    {
        bvect64 bv1;
        auto ii = bv1.inserter();
        ii = bm::id_max / 2;
        assert(bv1.test(bm::id_max / 2));
    }
    
    {
        bvect64 bv1 {0, 10, 31, 32, 62, 63,
             (5 * bm::bits_in_array), (5 * bm::bits_in_array)+1,
             bm::id_max32-1, bm::id_max32, bm::id64_t(bm::id_max32)+1,
             bm::id_max48-1
            };
        compare_BV(bv1, vect);
    }
    cout << "------------------------------------ SyntaxTest() OK" << endl;
}

static
void GenericBVectorTest()
{
    cout << "------------------------------------ GenericBVectorTest()" << endl;
    
    {
        bvect64 bv0;
        ref_vect vect;
        generate_vect_simpl0(vect);
        
        load_BV_set_ref(bv0, vect);
        compare_BV_set_ref(bv0, vect);

        bvect64 bv1(bm::BM_GAP);
        load_BV_set_ref(bv1, vect);
        compare_BV_set_ref(bv1, vect);
        
        int cmp = bv0.compare(bv1);
        assert(cmp == 0);
        
        bvect64::statistics st1, st2, st3;
        bv0.calc_stat(&st1);
        assert(st1.ptr_sub_blocks == 5);
        bv0.optimize(0, bvect64::opt_compress, &st2);
        assert(st1.ptr_sub_blocks == st2.ptr_sub_blocks);
        bv1.calc_stat(&st3);
        assert(st1.ptr_sub_blocks == st3.ptr_sub_blocks);
        assert(st2.gap_blocks == st3.gap_blocks);
    }

    cout << "------------------------------------ GenericBVectorTest() OK" << endl;
}

static
void SetTest()
{
    cout << "------------------------------------ SetTest()" << endl;
    {
        bvect bv;
        bv.set();
        auto cnt = bv.count();
        assert (cnt == bm::id_max);
    }
    
    {
        bvect bv;
        bv.set();
        bv.set_range(10, bm::id_max, false);
        auto cnt = bv.count();
        assert(cnt == 10);
    }
    {
        bvect bv;
        bv.set_range(bm::id_max-65535, bm::id_max, true);
        auto cnt = bv.count();
        assert(cnt == 65536);
    }

    {
        bvect bv{ 0, 10, 65536, 10000, bm::id_max-1 };
        auto cnt = bv.count();
        assert (cnt == 5);

        bvect bv2;
        bv2.set(0).set(10).set(65536).set(10000).set(bm::id_max-1);

        if (bv != bv2)
        {
            cout << "Brace initialization comparison test failed!." << endl;
            assert(0);exit(1);
        }
    }
    {
        bvect bv;
        bv.set();

        auto cnt = bv.count();
        assert (cnt == bm::id_max);

        bv.invert();
        cnt = bv.count();
        assert (cnt == 0);

        bv.set(0);
        bv.set(bm::id_max - 1);
        cnt = bv.count();
        assert(cnt == 2);

        bv.invert();
        //print_stat(bv);
        cnt = bv.count();
        assert (cnt == bm::id_max - 2);

        bv.clear();
        bv[1] &= true;
        bool v = bv[1];
        assert (!v);

        bv[1] = true;
        bv[1] &= true;
        v = bv[1];
        assert(v);

        bv.clear(true);
        bv.invert();
        bv[1] &= true;
        v = bv[1];
        assert (v);
    }
    {
        bvect bv_full;
        bv_full.invert();
        assert(bv_full.test(bm::id_max/2));
    }
    {
        bvect bv1, bv2(BM_GAP);
        bvect::size_type cnt;
        bv1.set(0); bv2.set(0);
        bv1.set(bm::id_max-1);bv2.set(bm::id_max-1);
        bv1.set((bm::id_max-1)/2);bv2.set((bm::id_max-1)/2);
        for (unsigned i = 0; i < 2; ++i)
        {
            bv1.set();
            bv2.set();
            cnt = bv1.count();
            assert (cnt == bm::id_max);
            cnt = bv2.count();
            assert (cnt == bm::id_max);
        }
    }

    {
        bvect bv2;
        bv2[bm::id_max-1] = true;
        bv2[bm::id_max-1] = false;
        bvect::statistics stat1;
        bv2.calc_stat(&stat1);
        
        bv2.optimize();

        bvect::statistics stat2;
        bv2.calc_stat(&stat2);

        if (stat2.bit_blocks != 0 ||
            stat2.gap_blocks != 0 ||
            stat1.memory_used <= stat2.memory_used)
        {
            cout << "Optimization memory free test failed (2)!" << endl;
            assert(0);exit(1);
        }
    }
    
    {
        bvect bv3;
        bool changed;
        changed = bv3.set_bit_conditional(bm::id_max-10, true, true);
        bool v = bv3[10];
        if (v || changed) {
            cout << "Conditional bit set failed." << endl;
            assert(0);exit(1);
        }
        changed = bv3.set_bit_conditional(bm::id_max-10, true, false);
        v = bv3[bm::id_max-10];
        if (!v || !changed) {
            cout << "Conditional bit set failed." << endl;
            assert(0);exit(1);
        }
        changed = bv3.set_bit_conditional(bm::id_max-10, false, false);
        v = bv3[bm::id_max-10];
        if (!v || changed) {
            cout << "Conditional bit set failed." << endl;
            assert(0);exit(1);
        }
        changed = bv3.set_bit_conditional(bm::id_max-10, false, true);
        v = bv3[bm::id_max-10];
        if (v || !changed) {
            cout << "Conditional bit set failed." << endl;
            assert(0);exit(1);
        }
    }
    {
        bvect bv3(bm::BM_GAP);
        bool changed;
        changed = bv3.set_bit_conditional(10, true, true);
        bool v = bv3[10];
        if (v || changed) {
            cout << "Conditional bit set failed." << endl;
            exit(1);
        }
        changed = bv3.set_bit_conditional(10, true, false);
        v = bv3[10];
        if (!v || !changed) {
            cout << "Conditional bit set failed." << endl;
            exit(1);
        }
        changed = bv3.set_bit_conditional(10, false, false);
        v = bv3[10];
        if (!v || changed) {
            cout << "Conditional bit set failed." << endl;
            exit(1);
        }
        changed = bv3.set_bit_conditional(10, false, true);
        v = bv3[10];
        if (v || !changed) {
            cout << "Conditional bit set failed." << endl;
            exit(1);
        }
    }
    
    {
        bvect bv3(bm::BM_GAP);
        bv3.invert();
        bv3.optimize();
        bool changed;
        changed = bv3.set_bit_conditional(10, true, true);
        bool v = bv3[10];
        if (!v || changed) {
            cout << "Conditional bit set failed." << endl;
            exit(1);
        }
        changed = bv3.set_bit_conditional(10, true, false);
        v = bv3[10];
        if (!v || changed) {
            cout << "Conditional bit set failed." << endl;
            exit(1);
        }
        changed = bv3.set_bit_conditional(10, false, false);
        v = bv3[10];
        if (!v || changed) {
            cout << "Conditional bit set failed." << endl;
            exit(1);
        }
        changed = bv3.set_bit_conditional(10, false, true);
        v = bv3[10];
        if (v || !changed) {
            cout << "Conditional bit set failed." << endl;
            exit(1);
        }
        changed = bv3.set_bit_conditional(10, true, false);
        v = bv3[10];
        if (!v || !changed) {
            cout << "Conditional bit set failed." << endl;
            exit(1);
        }
    }

    {
        bvect::size_type new_size(1000001);
        bvect bv(0);
        bv.resize(new_size);
        bv[10] = true;
        bv.resize(new_size);
        bv[new_size-1] = 1;

        if (bv.size() != new_size)
        {
            cout << "Resize failed" << endl;
            exit(1);
        }
        if (bv.count() != 2ull)
        {
            cout << "Resize count failed" << endl;
            exit(1);
        }

        bv.resize(100);
        if (bv.size() != 100)
        {
            cout << "Resize failed" << endl;
            exit(1);
        }
        if (bv.count() != 1)
        {
            cout << "Resize count failed" << endl;
            exit(1);
        }
        
        bv.resize(60000100);
        bv.invert();
        bv.clear(true);


        if (bv.size() != 60000100)
        {
            cout << "Resize failed" << endl;
            exit(1);
        }
        if (bv.count() != 0)
        {
            cout << "Resize count failed" << endl;
            exit(1);
        }
    }
    
    {
        bvect bv(100);
        assert(bv.size()==100);
        bv[bm::id_max-1] = true;
        assert(bv.size() == bm::id_max);
        bv.set_bit(bm::id_max-1);
        assert(bv.size() == bm::id_max);
    }

    cout << "------------------------------------ SetTest() OK" << endl;
}

static
void ExportTest()
{
    cout << "---------------------------- ExportTest..." << endl;

    {
        char buf[20] = {0,};

        buf[0] = 1;
        buf[1] = 1;
        buf[2]= (char)(1 << 1);

        bvect bv1;
        bm::export_array(bv1, buf + 0, buf + 20);

        auto cnt = bv1.count();
        assert(cnt == 3);
        assert(bv1.test(0));
        assert(bv1.test(8));
        assert(bv1.test(17));
    }

    {
        char buf[65536*10] = {0,};

        buf[0] = 1;
        buf[1] = 1;
        buf[2]= (char)(1 << 1);

        bvect bv1;
        export_array(bv1, buf + 0, buf + 65536*10);

        assert(bv1.count() == 3);
        assert(bv1.test(0));
        assert(bv1.test(8));
        assert(bv1.test(17));
    }

    {
        short buf[20] = {0,};

        buf[0] = 1;
        buf[1] = 1;
        buf[2]= (char)(1 << 1);

        bvect bv1;
        export_array(bv1, buf + 0, buf + 20);

        assert(bv1.count() == 3);
        assert(bv1.test(0));
        assert(bv1.test(16));
        assert(bv1.test(33));
    }

    {
        unsigned buf[20] = {0,};

        buf[0] = 1;
        buf[1] = 1;
        buf[2]= (char)(1 << 1);

        bvect bv1;
        export_array(bv1, buf + 0, buf + 20);

        assert(bv1.count() == 3);
        assert(bv1.test(0));
        assert(bv1.test(32));
        assert(bv1.test(65));
    }


    cout << "---------------------------- ExportTest Ok." << endl;
}

static
void ResizeTest()
{
    cout << "---------------------------- ResizeTest()" << endl;
    {{
        bvect bv(0);
        assert(bv.any() == false);
        assert(bv.count() == 0);
    }}

    {{
        bvect bv1(10);
        bvect bv2(bv1);
        assert(bv1.size() == bv2.size());
    }}

    {{
        bvect bv;
        bv.invert();
        bvect::size_type cnt = bv.count();
        assert(cnt == bm::id_max);
        assert(bv.test(bm::id_max-1));
    }}

    {{
        bvect bv(10);
        assert(bv.any() == false);
        assert(bv.count() == 0);
        bv.invert();
        auto cnt = bv.count();
        assert(cnt == 10);
    }}

    {{
        bvect bv1(10);
        bv1.set(1);
        bvect bv2(0);

        assert(bv1.size() == 10);
        assert(bv2.size() == 0);
        assert(bv1.count() == 1);
        assert(bv2.count() == 0);
        
        bv1.swap(bv2);

        assert(bv2.size() == 10);
        assert(bv2.count() == 1);
        assert(bv1.size() == 0);
        assert(bv1.count() == 0);
    }}

    {{
        bvect bv1;
        bv1.set(65536);
        bv1.set(100);
        assert(bv1.size() == bm::id_max);
        assert(bv1.count() == 2);
        bv1.resize(101);
        assert(bv1.size() == 101);
        assert(bv1.count() == 1);
        {{
            auto f = bv1.get_first();
            assert(f == 100);
            f = bv1.get_next(f);
            assert(f == 0);
        }}

        bv1.resize(10);
        assert(bv1.size() == 10);
        assert(bv1.count() == 0);
        auto f = bv1.get_first();
        assert(f == 0);
    }}

    {{
        bvect bv;
        //print_stat(bv);
        bv.set(100);
        bv.set(65536 + 10);
        print_stat(bv);
        bv.set_range(0, 65536*10, false);
        print_stat(bv);
    }}

    // test logical operations

    {{
        bvect bv1(65536 * 10);
        bvect bv2(65536 * 100);
        bv1.set(5);
        bv2.set(5);
        bv2.set(65536 * 2);
        bv2 &= bv1;
        assert(bv2.size() == 65536 * 100);
        assert(bv2.count() == 1);
        assert(bv2.get_first() == 5);
    }}

    {{
        bvect bv1(10);
        bvect bv2;
        bv1.set(5);
        bv2.set(5);
        bv2.set(65536 * 2);
        bv1 &= bv2;
        assert(bv1.size() == bv2.size());
        assert(bv1.count() == 1);
        assert(bv1.get_first() == 5);
    }}

    {{
        bvect bv1(10);
        bvect bv2;
        bv1.set(5);
        bv2.set(6);
        bv2.set(65536 * 2);
        bv1 |= bv2;
        assert(bv1.size() == bv2.size());
        assert(bv1.count() == 3);
    }}

    // comparison test

    {{
        int cmp;
        bvect bv1(10);
        bvect bv2;
        bv2.set(65536 * 2);

        cmp = bv1.compare(bv2);
        assert(cmp < 0);

        bv1.set(5);
        assert(cmp < 0);
        cmp = bv1.compare(bv2);
        assert(cmp > 0);
        cmp = bv2.compare(bv1);
        assert(cmp < 0);
    }}

    // inserter
    //
    {{
        bvect bv1(10);
        bm::id64_t maxs= bm::id_max - 100;
        {
            bvect::insert_iterator it(bv1);
            *it = 100 * 65536;
            *it = maxs;
        }
        auto sz = bv1.size();
        assert(sz == maxs+1);
    }}

    // serialization
    //
    {{
        const bvect::size_type test_size = bm::id_max - 100;
        bvect bv1(test_size);
        bv1.set(test_size - 1005);
        struct bvect::statistics st1;
        bv1.calc_stat(&st1);

        unsigned char* sermem = new unsigned char[st1.max_serialize_mem];
        unsigned slen2 = bm::serialize(bv1, sermem);
        cout << slen2 << endl;

        bvect bv2(0);
        bm::deserialize(bv2, sermem);
        delete [] sermem;

        assert(bv2.size() == test_size);
        assert(bv2.count() == 1);
        auto first = bv2.get_first();
        assert(first == test_size - 1005);
    }}

    {{
        bvect bv1(10);
        bv1.set(5);
        unsigned int arg[] = { 10, 65536, 65537, 65538 * 10000 };
        unsigned* it1 = arg;
        unsigned* it2 = arg + 4;
        combine_or(bv1, it1, it2);
        assert(bv1.size() == 65538 * 10000 + 1);
        bvect::enumerator en = bv1.first();
        while (en.valid())
        {
            cout << *en << " ";
            ++en;
        }
    }}
    cout << "---------------------------- ResizeTest() OK" << endl;
}

static
void CompareEnumerators(const bvect::enumerator& en1, const bvect::enumerator& en2)
{
    if (!en1.valid() && !en2.valid())
        return;
    bool fsm_equal = en1.compare_state(en2);
    if (!fsm_equal)
    {
        cerr << "Enumerators FSM comparison failed" << endl;
        assert(0);
        exit(1);
    }
}

static
void EmptyBVTest()
{
    cout << "---------------------------- Empty bvector test" << endl;

    {
        bvect bv1;
        bvect bv2;
        
        bvect bv3(bv1 & bv2);
        bvect bv4 = (bv1 & bv2);
        
        std::vector< bvect > v;
        v.push_back(bvect());
    }

    {
        bvect  bv1;
        auto cnt = bv1.count_range(0, 10);
        if (cnt)
        {
            cerr << "Failed count_range()" << endl;
            exit(1);
        }
        bool b = bv1.test(0);
        if (b)
        {
            cerr << "Failed test" << endl;
            exit(1);
        }
        
        b = bv1.any();
        if (b)
        {
            cerr << "Failed any" << endl;
            exit(1);
        }
        
        bv1.set_bit(0);
        if (!bv1.any())
        {
            cerr << "Failed set_bit" << endl;
            exit(1);
        }
    }
    {
        bvect  bv1;
        bool b = bv1.set_bit_and(0, false);
        if (bv1.any() || b)
        {
            cerr << "Failed set_bit" << endl;
            exit(1);
        }
    }
    {
        bvect  bv1;
        bv1.set_range(0, 1, false);
        if (bv1.any())
        {
            cerr << "Failed set_range" << endl;
            exit(1);
        }
        bv1.set_range(0, 1, true);
        if (bv1.count()!=2)
        {
            cerr << "Failed set_range(0,1)" << endl;
            exit(1);
        }
    }
    {
        bvect  bv1;
        bv1.clear_bit(0);
        if (bv1.any())
        {
            cerr << "Failed clear_bit" << endl;
            exit(1);
        }
    }
    {
        bvect  bv1;
        bv1.clear();
        if (bv1.any())
        {
            cerr << "Failed clear()" << endl;
            exit(1);
        }
    }
    {
        bvect  bv1;
        bv1.invert();
        if (!bv1.any())
        {
            cerr << "Failed invert()" << endl;
            exit(1);
        }
    }
    {
        bvect  bv1;
        bvect  bv2;
        bv1.swap(bv2);
        if (bv1.any() || bv2.any())
        {
            cerr << "Failed swap()" << endl;
            exit(1);
        }
    }
    {
        bvect  bv1;
        if (bv1.get_first() != 0)
        {
            cerr << "Failed get_first()" << endl;
            exit(1);
        }
    }
    {
        bvect  bv1;
        if (bv1.extract_next(0) != 0)
        {
            cerr << "Failed extract_next()" << endl;
            exit(1);
        }
    }
    {
        bvect  bv1;
        bvect::statistics st;
        bv1.calc_stat(&st);
        if (st.memory_used == 0)
        {
            cerr << "Failed calc_stat()" << endl;
            exit(1);
        }
    }
    {
        bvect  bv1;
        bvect  bv2;
        bvect  bv3;
        bv1.bit_or(bv2);
        if (bv1.any())
        {
            cerr << "Failed bit_or()" << endl;
            exit(1);
        }
        bv2.set_bit(bm::id_max-100);
        bv1.bit_or(bv2);
        if (!bv1.any())
        {
            cerr << "Failed bit_or()" << endl;
            exit(1);
        }
        bv1.bit_or(bv3);
        if (bv1.count()!=1)
        {
            cerr << "Failed bit_or()" << endl;
            exit(1);
        }
    }
    
    {
        bvect  bv1;
        bvect  bv2;
        bv1.bit_and(bv2);
        if (bv1.any())
        {
            cerr << "Failed bit_and()" << endl;
            exit(1);
        }
        bv2.set_bit(100000000);
        bv1.bit_and(bv2);
        if (bv1.any())
        {
            cerr << "Failed bit_and()" << endl;
            exit(1);
        }
        bv2.bit_and(bv1);
        if (bv2.count()!=0)
        {
            cerr << "Failed bit_and()" << endl;
            exit(1);
        }
    }
    
    {
        bvect  bv1;
        bvect::statistics st1;
        bv1.optimize(0, bvect::opt_compress, &st1);
        if (st1.memory_used == 0)
        {
            cerr << "Failed calc_stat()" << endl;
            exit(1);
        }
        bv1.optimize_gap_size();
    }
    
    {
        bvect  bv1;
        bvect  bv2;
        
        int r = bv1.compare(bv2);
        if (r != 0)
        {
            cerr << "Failed compare()" << endl;
            assert(0);
            exit(1);
            
        }
        bv2.set_bit(bm::id_max-1000);
        r = bv1.compare(bv2);
        if (r == 0)
        {
            cerr << "Failed compare()" << endl;
            exit(1);
            
        }
        r = bv2.compare(bv1);
        if (r == 0)
        {
            cerr << "Failed compare()" << endl;
            exit(1);
        }
    }
    
    {
        bvect  bv1;
        bvect::enumerator en1 = bv1.first();
        bvect::enumerator en2 = bv1.get_enumerator(0ULL);
        CompareEnumerators(en1, en2);
        if (en1.valid() || en2.valid())
        {
            cerr << "failed first enumerator" << endl;
            exit(1);
        }
    }
    
    cout << "---------------------------- Empty bvector test OK" << endl;
    
}

static
void EnumeratorTest()
{
    cout << "-------------------------------------------- EnumeratorTest" << endl;

    {
    bvect bvect1;

    bvect1.set_bit(100);
    
    {
        auto n = bvect1.get_next(101);
        assert(!n);
    }

    bvect::enumerator en = bvect1.first();
    auto n = bvect1.get_next(0);
    
    bvect::enumerator en1 = bvect1.get_enumerator(n);
    if (*en != 100 || n != 100 || *en1 != 100)
    {
        cout << "1.Enumerator error !" << endl;
        exit(1);
    }
    CompareEnumerators(en, en1);

    bvect1.clear_bit(100);

    bvect1.set_bit(bm::id_max - 100);
    en.go_first();
    n = bvect1.get_next(0);
    en1.go_to(n);
    if (*en != bm::id_max - 100 || n != *en || *en1 != *en)
    {
        cout << "2. Enumerator error !" << endl;
        assert(0);
        exit(1);
    }
    CompareEnumerators(en, en1);

    bvect1.optimize();
    en = bvect1.first();
    n = bvect1.get_next(0);
    en1 = bvect1.first();
    en1.go_to(n);
    if (*en != bm::id_max - 100 || n != *en || *en1 != *en)
    {
        cout << "2. Enumerator error !" << endl;
        assert(0);
        exit(1);
    }
    CompareEnumerators(en, en1);

    }

    {
        bvect bvect1;
        bvect1.set_bit(0);
        bvect1.set_bit(10);
        bvect1.set_bit(35);
        bvect1.set_bit(1000);
        bvect1.set_bit(2016519);
        bvect1.set_bit(2034779);
        bvect1.set_bit(bm::id_max-1);

        bvect::enumerator en = bvect1.first();

        auto num = bvect1.get_first();

        bvect::enumerator end = bvect1.end();
        while (en < end)
        {
            cout << num << endl;
            bvect::enumerator en1 = bvect1.get_enumerator(num ? num-1 : num);
            if (*en != num || *en != *en1)
            {
                cout << "Enumeration comparison failed !" <<
                        " enumerator = " << *en <<
                        " get_next() = " << num <<
                        " goto enumerator = " << *en1 <<
                        endl;
                exit(1);
            }
            CompareEnumerators(en, en1);
            
            ++en;
            num = bvect1.get_next(num);
            ++en1;
            CompareEnumerators(en, en1);
            {
                auto num2 = num / 2;
                if (num2 < num)
                {
                    auto idx0 = bvect1.get_next(num2);
                    bvect::enumerator en3 = bvect1.get_enumerator(num2);
                    assert(idx0 == *en3);
                }
            }
        }
        if (num != 0)
        {
            cout << "Enumeration error!" << endl;
            exit(1);
        }
    }

    cout << "FULL bvector enumerator stress test (0)..." << endl;
    {
        bvect bvect1;
        bvect1.set();
        
        {
            bvect::enumerator en2(&bvect1, bm::id_max-1);
            ++en2;
            bool b = en2.valid();
            assert(!b);
        }

        bvect::enumerator en = bvect1.first();
        auto num = bvect1.get_first();
        while (en.valid())
        {
            if (*en != num)
            {
                cout << "Enumeration comparison failed !" <<
                        " enumerator = " << *en <<
                        " get_next() = " << num << endl;
                assert(0);
                exit(1);
            }

            ++en;
            num = bvect1.get_next(num);
            {
                bvect::enumerator en2(&bvect1, num);
                if (*en2 != num)
                {
                    cout << "Enumeration comparison failed !" <<
                            " enumerator = " << *en <<
                            " get_next() = " << num << endl;
                    assert(0);
                    exit(1);
                }
                CompareEnumerators(en, en2);
            }
            if (num > (bm::set_sub_array_size * bm::gap_max_bits * 2))
                break;
        } // while
    }
    cout << "FULL bvector enumerator stress test (0) ... OK" << endl;

    
    cout << "FULL bvector enumerator stress test (1)..." << endl;
    {
        bvect bvect1;
        bvect1.set();
        
        bvect::size_type start_idx = bm::id_max - (bm::set_sub_array_size * bm::gap_max_bits * 2);

        bvect::enumerator en(&bvect1, start_idx);
        while (en.valid())
        {
            bvect::size_type pos;
            bool b = bvect1.find(start_idx, pos);
            if (*en != pos || !b)
            {
                cout << "2. Enumeration comparison failed !" <<
                        " enumerator = " << *en <<
                        " find() = " << pos << endl;
                assert(0);
                exit(1);
            }

            ++en;
            pos = bvect1.get_next(pos);
            if (pos)
            {
                bvect::enumerator en2(&bvect1, pos);
                if (*en2 != pos)
                {
                    cout << "2. Enumeration comparison failed !" <<
                            " enumerator = " << *en <<
                            " get_next() = " << pos << endl;
                    assert(0);
                    exit(1);
                }
                CompareEnumerators(en, en2);
            }
            else
            {
                assert(start_idx == bm::id_max-1);
            }
            start_idx = pos;
        } // while
    }
    cout << "FULL bvector enumerator stress test (1) ... OK" << endl;
    
    {
        bvect bvect1;

        unsigned i;
        for(i = 0; i < 65536; ++i)
        {
            bvect1.set_bit(i);
        }
        for(i = 65536*10; i < 65536*20; i+=3)
        {
            bvect1.set_bit(i);
        }

        bvect::enumerator en = bvect1.first();
        bvect::size_type num = bvect1.get_first();
        while (en < bvect1.end())
        {
            bvect::enumerator en1 = bvect1.get_enumerator(num);
            if (*en != num || *en != *en1)
            {
                cout << "Enumeration comparison failed !" <<
                        " enumerator = " << *en <<
                        " get_next() = " << num <<
                        " goto enumerator = " << *en1
                        << endl;
                exit(1);
            }
            ++en;
            num = bvect1.get_next(num);
            if (num == 31)
            {
                num = num + 0;
            }
            ++en1;
            CompareEnumerators(en, en1);
        }
        if (num != 0)
        {
            cout << "Enumeration error!" << endl;
            exit(1);
        }
    }


    {
        bvect bvect1;
        bvect1.set_new_blocks_strat(bm::BM_GAP);
        bvect1.set_bit(100);

        bvect::enumerator en = bvect1.first();
        bvect::enumerator en1 = bvect1.get_enumerator(99);
        if (*en != 100 || *en != *en1)
        {
            cout << "Enumerator error !" << endl;
            exit(1);
        }
        CompareEnumerators(en, en1);

        bvect1.clear_bit(100);

        bvect1.set_bit(bm::id_max - 100);
        en.go_first();
        en1.go_to(10);

        if ((*en != bm::id_max - 100) || *en != *en1)
        {
            cout << "Enumerator error !" << endl;
            exit(1);
        }
        CompareEnumerators(en, en1);
        print_stat(bvect1);
    }

    {
        bvect bvect1;
        bvect1.set_new_blocks_strat(bm::BM_GAP);
        bvect1.set_bit(0);
        bvect1.set_bit(1);
        bvect1.set_bit(10);
        bvect1.set_bit(100);
        bvect1.set_bit(1000);

        bvect::enumerator en = bvect1.first();

        auto num = bvect1.get_first();

        while (en < bvect1.end())
        {
            bvect::enumerator en1 = bvect1.get_enumerator(num);
            if (*en != num || *en != *en1)
            {
                cout << "Enumeration comparison failed !" <<
                        " enumerator = " << *en <<
                        " get_next() = " << num <<
                        " goto enumerator = " << *en1 << endl;
                exit(1);
            }
            CompareEnumerators(en, en1);
            ++en;
            num = bvect1.get_next(num);
            ++en1;
            CompareEnumerators(en, en1);
        }
        if (num != 0)
        {
            cout << "Enumeration error!" << endl;
            exit(1);
        }
    }
}

static
void VerifyCountRange(const bvect& bv,
                      const bvect::rs_index_type& bc_arr,
                      bvect::size_type from,
                      bvect::size_type to)
{
    for (bvect::size_type i = from; i < to; ++i)
    {
        bvect::size_type cnt1 = bv.count_range(0, i);
        bvect::size_type cnt2 = bv.count_to(i, bc_arr);
        auto cnt3 = bv.count_to_test(i, bc_arr);
        
        assert(cnt1 == cnt2);
        if (cnt1 != cnt2)
        {
            cerr << "VerifyCountRange failed!" << " count_range()=" << cnt1
                << " count_to()=" << cnt2 << endl;
        }
        if (cnt3 != cnt1)
        {
            bool b = bv.test(i);
            if (b)
            {
                cerr << "VerifyCountRange failed! count_to_test()" << cnt3 << " count_range()=" << cnt1
                     << endl;
            }
        }
        
        bvect::size_type cnt4 = bv.count_range(i, to);
        bvect::size_type cnt5 = bv.count_range(i, to, bc_arr);
        if (cnt4 != cnt5)
        {
            cnt5 = bv.count_range(i, to, bc_arr);
            assert(cnt4 == cnt5);
            exit(1);
        }
    } // for
}

static
void RSIndexTest()
{
    cout << "---------------------------- RSIndexTest() test" << endl;

    {
        rs_ind rsi;

        rsi.resize(bm::set_sub_array_size * 5);
        rsi.resize_effective_super_blocks(3);

        rsi.set_super_block(0, 1);
        rsi.set_super_block(1, 2);
        rsi.set_super_block(2, 3);
        rsi.set_null_super_block(3);
        rsi.set_full_super_block(4);

        auto sb_size = rsi.super_block_size();
        assert(sb_size == 6);

        auto rc = rsi.get_super_block_bcount(0);
        assert(rc == 1);
        rc = rsi.get_super_block_bcount(1);
        assert(rc == 2);
        rc = rsi.get_super_block_bcount(2);
        assert(rc == 3);

        auto bc = rsi.get_super_block_rcount(0);
        assert(bc == 1);
        bc = rsi.get_super_block_rcount(1);
        assert(bc == 3);
        bc = rsi.get_super_block_rcount(2);
        assert(bc == 6);

        unsigned i = rsi.find_super_block(1);
        assert(i == 0);
        i = rsi.find_super_block(2);
        assert(i == 1);
        i = rsi.find_super_block(3);
        assert(i == 1);
        i = rsi.find_super_block(4);
        assert(i == 2);
        i = rsi.find_super_block(200);
        assert(i == 4);
        /*
                i = rsi.find_super_block(bm::id_max);
                assert(i == 5);
        */
    }

    {
        unsigned bcount[bm::set_sub_array_size];
        unsigned sub_count1[bm::set_sub_array_size];
        unsigned sub_count2[bm::set_sub_array_size];
        for (unsigned i = 0; i < bm::set_sub_array_size; ++i)
        {
            bcount[i] = sub_count1[i] = sub_count2[i] = 0;
        } // for
        bcount[0] = 1;
        bcount[255] = 2;

        sub_count1[0] = 1;          // sub-range 0
        sub_count1[255] = 0;        // sub-3
        sub_count2[0] = 1 << 16;    // sub-2
        sub_count2[255] = 1 << 16;  // sub 2,3


        rs_ind rsi;
        // -----------------------------------------
        rsi.resize(bm::set_sub_array_size * 4);
        rsi.resize_effective_super_blocks(2);
        rsi.set_total(bm::set_sub_array_size * 4);


        rsi.set_null_super_block(0);
        rsi.register_super_block(1, &bcount[0], &sub_count1[0]);
        rsi.register_super_block(2, &bcount[0], &sub_count2[0]);
        rsi.set_full_super_block(3);
        auto tcnt = rsi.count();
        assert(tcnt == 6 + bm::set_sub_array_size * 65536);

        unsigned i = rsi.find_super_block(1);
        assert(i == 1);
        i = rsi.find_super_block(3);
        assert(i == 1);
        i = rsi.find_super_block(4);
        assert(i == 2);
        i = rsi.find_super_block(400);
        assert(i == 3);
        //        i = rsi.find_super_block(bm::id_max);
        //        assert(i == rsi.super_block_size());

        unsigned bc;
        rs_ind::size_type rbc;
        for (unsigned nb = 0; nb < bm::set_sub_array_size; ++nb)
        {
            bc = rsi.count(nb);
            assert(bc == 0);
            rbc = rsi.bcount(nb);
            assert(!rbc);
        }
        bc = rsi.count(bm::set_sub_array_size);
        assert(bc == 1);
        rbc = rsi.bcount(bm::set_sub_array_size);
        assert(rbc == 1);

        bc = rsi.count(bm::set_sub_array_size + 1);
        assert(bc == 0);
        rbc = rsi.bcount(bm::set_sub_array_size + 1);
        assert(rbc == 1);

        bc = rsi.count(bm::set_sub_array_size + 255);
        assert(bc == 2);
        rbc = rsi.bcount(bm::set_sub_array_size + 255);
        assert(rbc == 3);

        bc = rsi.count(bm::set_sub_array_size * 3);
        assert(bc == 65536);
        rbc = rsi.bcount(bm::set_sub_array_size * 3);
        assert(rbc == 65536 + 6);
        rbc = rsi.bcount(bm::set_sub_array_size * 3 + 1);
        assert(rbc == 65536 + 6 + 65536);


        // ==========================
        {
            auto nb = rsi.find(1);
            assert(nb == 256);

            nb = rsi.find(2);
            assert(nb == bm::set_sub_array_size + 255);
            nb = rsi.find(3);
            assert(nb == bm::set_sub_array_size + 255);

            nb = rsi.find(4);
            assert(nb == bm::set_sub_array_size + 255 + 1);

            nb = rsi.find(65536);
            assert(nb == 3 * bm::set_sub_array_size + 0);
            nb = rsi.find(65536 * 2);
            assert(nb == 3 * bm::set_sub_array_size + 1);
            nb = rsi.find(65536 * 3);
            assert(nb == 3 * bm::set_sub_array_size + 2);
        }
        // ==========================

        {
            bool b;
            rs_ind::size_type rank;
            rs_ind::block_idx_type nb;
            bm::gap_word_t sub_range;

            rank = 1;
            b = rsi.find(&rank, &nb, &sub_range);
            assert(b);
            assert(nb == 256);
            assert(sub_range == 0);
            assert(rank == 1);

            rank = 2;
            b = rsi.find(&rank, &nb, &sub_range);
            assert(b);
            assert(nb == bm::set_sub_array_size + 255);
            assert(sub_range == bm::rs3_border1 + 1);
            assert(rank == 1);

            rank = 3;
            b = rsi.find(&rank, &nb, &sub_range);
            assert(b);
            assert(nb == bm::set_sub_array_size + 255);
            assert(sub_range == bm::rs3_border1 + 1);
            assert(rank == 2);

            rank = 4;
            b = rsi.find(&rank, &nb, &sub_range);
            assert(b);
            assert(nb == bm::set_sub_array_size + 255 + 1);
            assert(sub_range == bm::rs3_border0 + 1);
            assert(rank == 1);

            rank = 5;
            b = rsi.find(&rank, &nb, &sub_range);
            assert(b);
            assert(nb == bm::set_sub_array_size + 256 + 255);
            assert(sub_range == bm::rs3_border0 + 1);
            assert(rank == 1);

            rank = 6;
            b = rsi.find(&rank, &nb, &sub_range);
            assert(b);
            assert(nb == bm::set_sub_array_size + 256 + 255);
            assert(sub_range == bm::rs3_border1 + 1);
            assert(rank == 1);

            rank = 65536;
            b = rsi.find(&rank, &nb, &sub_range);
            assert(b);
            assert(nb == 3 * bm::set_sub_array_size + 0);
            assert(sub_range == bm::rs3_border1 + 1);
            assert(rank == 65536 - 6 - bm::rs3_border1);

            rank = 65536 + 7;
            b = rsi.find(&rank, &nb, &sub_range);
            assert(b);
            assert(nb == 3 * bm::set_sub_array_size + 1);
            assert(sub_range == 0);
            assert(rank == 1);

            rank = bm::id_max;
            i = rsi.find_super_block(bm::id_max);
            b = rsi.find(&rank, &nb, &sub_range);
            assert(!b);


        }

    }


    cout << "---------------------------- RSIndexTest() test OK" << endl;
}



static
void CountRangeTest()
{
    cout << "---------------------------- CountRangeTest..." << endl;
/*
    {{
        bvect bv1;
        bv1.set(0);
        bv1.set(1);
        
        bvect::rs_index_type bc_arr;
        bv1.running_count_blocks(&bc_arr);
        assert(bc_arr.count() == 2);
        
        for (bvect::size_type i = 0; i < bm::set_total_blocks; ++i)
        {
            assert(bc_arr.count(i) == 2);
        } // for
        
        VerifyCountRange(bv1, bc_arr, 0, 200000);
        
        bv1.optimize();
        bvect::rs_index_type bc_arr1;
        bv1.running_count_blocks(&bc_arr1);
        
        for (bvect::size_type i = 0; i < bm::set_total_blocks; ++i)
        {
            assert(bc_arr1.count(i) == 2);
        } // for
        
        VerifyCountRange(bv1, bc_arr1, 0, 200000);
    }}

    {{
        bvect bv1;
        bv1.set(0);
        bv1.set(1);
        
        bv1.set(65535+10);
        bv1.set(65535+20);
        bv1.set(65535+21);
        
        bv1.set(bm::id_max-100);

        
        bvect::rs_index_type bc_arr;
        bv1.running_count_blocks(&bc_arr);

        assert(bc_arr.bcount(0) == 2);
        assert(bc_arr.bcount(1) == 5);

        for (bvect::size_type i = 2; i < bm::set_total_blocks; ++i)
        {
            assert(bc_arr.bcount(i) == 5);
        } // for
        
        VerifyCountRange(bv1, bc_arr, bm::id_max-1, bm::id_max);
        for (unsigned i = 0; i < 2; ++i)
        {
            VerifyCountRange(bv1, bc_arr, 0, 200000);
            VerifyCountRange(bv1, bc_arr, bm::id_max-200000, bm::id_max);

            // check within empty region
            VerifyCountRange(bv1, bc_arr, bm::id_max/2-200000, bm::id_max/2+200000);

            bv1.optimize();
        }
    }}
*/
    cout << "check inverted bvector" << endl;
    {{
            bvect bv1;
        
            bv1.invert();

            bvect::rs_index_type bc_arr;
            bv1.build_rs_index(&bc_arr);
            auto cnt1 = bv1.count();
            auto cnt2 = bc_arr.count();
            assert(cnt1 == cnt2);

            VerifyCountRange(bv1, bc_arr, bm::id_max-1, bm::id_max-1);

//            VerifyCountRange(bv1, bc_arr, 0, 200000);
            VerifyCountRange(bv1, bc_arr, bm::id_max-200000, bm::id_max-1);
            VerifyCountRange(bv1, bc_arr, bm::id_max/2-200000, bm::id_max/2+200000);
    }}
    
    cout << "---------------------------- CountRangeTest OK" << endl;
}




int main(void)
{
    time_t      start_time = time(0);
    time_t      finish_time;
    
    // -----------------------------------------------------------------
/*
    SyntaxTest();
    GenericBVectorTest();
    SetTest();
    ExportTest();

    ResizeTest();
    EmptyBVTest();
    EnumeratorTest();
*/
    RSIndexTest();

    CountRangeTest();
    
    // -----------------------------------------------------------------

    finish_time = time(0);
    cout << "Test execution time = " << finish_time - start_time << endl;

#ifdef MEM_DEBUG
    cout << "[--------------  Allocation digest -------------------]" << endl;
    cout << "Number of BLOCK allocations = " <<  dbg_block_allocator::na_ << endl;
    cout << "Number of PTR allocations = " <<  dbg_ptr_allocator::na_ << endl << endl;

    if(dbg_block_allocator::balance() != 0)
    {
        cout << "ERROR! Block memory leak! " << endl;
        cout << dbg_block_allocator::balance() << endl;
        exit(1);
    }

    if(dbg_ptr_allocator::balance() != 0)
    {
        cout << "ERROR! Ptr memory leak! " << endl;
        cout << dbg_ptr_allocator::balance() << endl;
        exit(1);
    }
    cout << "[------------  Debug Allocation balance OK ----------]" << endl;
#endif

    return 0;

}
