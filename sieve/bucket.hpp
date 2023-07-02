#ifndef BUCKET_HPP_
#define BUCKET_HPP_

/*
 * Bucket sieving: radix-sort sieve updates as they are created.
 */

#include "cado_config.h"  // HAVE_SSE2
#include <type_traits> // enable_if is_same
#include <vector>
#include <cstddef>      // size_t NULL
#include <cstdint>
#define xxxSAFE_BUCKETS_SINGLE
#define xxxSAFE_BUCKET_ARRAYS
#if defined(SAFE_BUCKETS_SINGLE) || defined(SAFE_BUCKET_ARRAYS)
#include <exception>
#include <cstdio>
#include <limits>
#include <string>
#include <functional>
#endif
#include "fb-types.h"
#include "fb.hpp"
#include "las-config.h"
#include "las-memory.hpp"
#include "macros.h"
#include "misc.h"
#include "utils_cxx.hpp" // NonCopyable
struct las_output;
struct where_am_I;

// #include "electric_alloc.h"

/*
 * This bucket module provides a way to store elements (that are called
 * updates), while partially sorting them, according to some criterion (to
 * be defined externally): the incoming data is stored into several
 * buckets. The user says for each data to which bucket it belongs. This
 * module is supposed to perform this storage in a cache-friendly way and
 * so on...
 */

/*
 * Main available commands are 
 *   push_bucket_update(i, x)  :   add x to bucket number i
 *   get_next_bucket_update(i) :   iterator to read contents of bucket number i
 *
 * See the MAIN FUNCTIONS section below for the complete interface, with
 * prototypes of exported functions.
 */

/********  Data structure for the contents of buckets **************/

/* In principle, the typedef for the bucket_update can be changed without
 * affecting the rest of the code. 
 */

/*
 * For the moment, we store the bucket updates and a 16-bit field
 * that can contain, for instance, the low bits of p.
 */

template <typename TARGET_TYPE, typename SOURCE_TYPE>
TARGET_TYPE limit_cast(const SOURCE_TYPE &b)
{
  ASSERT_EXPENSIVE(b >= std::numeric_limits<TARGET_TYPE>::min());
  ASSERT_EXPENSIVE(b <= std::numeric_limits<TARGET_TYPE>::max());
  return static_cast<TARGET_TYPE>(b);
}

/* emptyhint_t is shorthint_t with no data -> its rtti key is s */
class emptyhint_t {
public:
  slice_offset_t hint_for_where_am_i() const { return 0; }
  emptyhint_t() {}
  emptyhint_t(const slice_offset_t slice_offset MAYBE_UNUSED) {}
  emptyhint_t(const fbprime_t p MAYBE_UNUSED,
              const slice_offset_t slice_offset MAYBE_UNUSED,
              const slice_index_t slice_index MAYBE_UNUSED)
  {}
  static constexpr char const * rtti = "s";
};

/* 16-bits */
class shorthint_t {
public:
  slice_offset_t hint;
  slice_offset_t hint_for_where_am_i() const { return hint; }
  shorthint_t() {}
  shorthint_t(const slice_offset_t slice_offset)
    : hint(slice_offset) {}
  shorthint_t(const fbprime_t p MAYBE_UNUSED,
              const slice_offset_t slice_offset,
              const slice_index_t slice_index MAYBE_UNUSED)
    : hint(slice_offset) {}
  static constexpr char const * rtti = "shorthint_t";
};

/* sizeof(slice_index_t), that is 4 bytes, + 2 hint bytes == 6 bytes */
/* When purging a bucket, we don't store pointer arrays to indicate where in
   the purged data a new slice begins, as each slice will have only very few
   updates surviving. Instead, we re-write each update to store both slice
   index and offset. */
class longhint_t {
public:
  slice_index_t index;
  slice_offset_t hint;
  slice_offset_t hint_for_where_am_i() const { return hint; }
  longhint_t(){}
  longhint_t(const slice_offset_t slice_offset,
             const slice_index_t slice_index)
    : index(slice_index), hint(slice_offset) {}
  longhint_t(const fbprime_t p MAYBE_UNUSED,
             const slice_offset_t slice_offset,
             const slice_index_t slice_index)
    : index(slice_index), hint(slice_offset) {}
  static constexpr char const * rtti = "longhint_t";
};

/* logphint_t is the 'no resieve' equivalent of longhint_t. We only need
 * logp, here.
 */
class logphint_t {
public:
    char logp;
  slice_offset_t hint_for_where_am_i() const { return 0; }
  logphint_t(){}
  logphint_t(const fbprime_t p MAYBE_UNUSED,
             const slice_offset_t slice_offset MAYBE_UNUSED,
             const slice_index_t slice_index MAYBE_UNUSED) {}
  logphint_t(char logp) : logp(logp) {}
  static constexpr char const * rtti = "l";
};

template<bool WITH_HINTS> struct hints_proxy {
    typedef longhint_t l;
    typedef shorthint_t s;
};
template<> struct hints_proxy<false> {
    typedef logphint_t l;
    typedef emptyhint_t s;
};

/* An update with the complete prime, generated by line re-sieving */
class primehint_t {
public:
  fbprime_t p;
  primehint_t(){}
  primehint_t(const fbprime_t p)
    : p(p) {}
  primehint_t(const fbprime_t p,
              const slice_offset_t slice_offset MAYBE_UNUSED,
              const slice_index_t slice_index MAYBE_UNUSED)
    : p(p) {}
};


/* A bucket update type has two template parameters: the level of the bucket
   sieving where the update was created, and the type of factor base prime
   hint it stores, used to speed up factoring of survivors.

   The level is 1, 2, or 3. The data type for the position where the update
   hits within a bucket is 16, 24, and 32 bits wide, resp.

   When LOG_BUCKET_REGION > 16, however, the position will have a few bits
   more, so the types for levels 1 and 3 must be changed accordingly.
   This creates, of course, a large memory overhead. */
 
template<int LEVEL> struct bucket_update_size_per_level; // IWYU pragma: keep

/* This only works if LOG_BUCKET_REGION <= 16 !
 *
 * However, now that it's a runtime setting, we can't statically decide
 * here.
 */
template<> struct bucket_update_size_per_level<1> { typedef uint16_t type; };
/* TODO: create a fake 24-bit type as uint8_t[3]. */
template<> struct bucket_update_size_per_level<2> { typedef uint32_t type; };
template<> struct bucket_update_size_per_level<3> { typedef uint32_t type; };

#if 0
/* here are types that would fit larger bucket regions */
template<> struct bucket_update_size_per_level<1> { typedef uint32_t type; };
template<> struct bucket_update_size_per_level<2> { typedef uint32_t type; };
template<> struct bucket_update_size_per_level<3> { typedef uint64_t type; };
#endif

/* "bare" bucket updates only have the info for locating the update
 * within a bucket region. That's it.
 *
 * We use these types for the specific case where we do not need any sort
 * of hint be stored in the buckets, because we do no resieving at all.
 */
template <int LEVEL> struct bare_bucket_update_t {
    typedef typename bucket_update_size_per_level<LEVEL>::type br_index_t;
    br_index_t x;
    bare_bucket_update_t() = default;
    bare_bucket_update_t(const uint64_t x) : x(limit_cast<br_index_t>(x)) {}
};


template <int LEVEL, typename HINT> struct bucket_update_t; // IWYU pragma: keep

#define bu_explicit(LEVEL, HINT, ALIGNMENT_ATTRIBUTE)		\
    template <>								\
    struct bucket_update_t<LEVEL, HINT> :                               \
            public HINT,                                                \
            public bare_bucket_update_t<LEVEL>                          \
    {                                           			\
      typedef bare_bucket_update_t<LEVEL> bare_t;                       \
      static inline int level() { return LEVEL; }                       \
      bucket_update_t(){};						\
      bucket_update_t(const uint64_t x,                                 \
              HINT const & h) : HINT(h), bare_t(x) {}                   \
      bucket_update_t(const uint64_t x,                                 \
              const fbprime_t p,		                        \
              const slice_offset_t slice_offset,                        \
              const slice_index_t slice_index)                          \
        : HINT(p, slice_offset, slice_index),				\
          bare_t(x)                                                     \
        {}								\
    } ALIGNMENT_ATTRIBUTE

#if 0
template <int LEVEL>
struct bucket_update_t<LEVEL, void> :
public bare_bucket_update_t<LEVEL>
{
    typedef bare_bucket_update_t<LEVEL> bare_t;
    static inline int level() { return LEVEL; }
    bucket_update_t(){};
    bucket_update_t(const uint64_t x,
            const fbprime_t, const slice_offset_t, const slice_index_t)
        : bare_t(x) {}
    bucket_update_t(const uint64_t x) : bare_t(x) {}
    static constexpr char const * rtti = "void";
};
#endif

bu_explicit(1, emptyhint_t, ATTR_ALIGNED(2));
bu_explicit(2, emptyhint_t, ATTR_ALIGNED(4));
bu_explicit(3, emptyhint_t, ATTR_ALIGNED(4));
static_assert(sizeof(bucket_update_t<1, emptyhint_t>) == 2, "wrong size");
static_assert(sizeof(bucket_update_t<2, emptyhint_t>) == 4, "wrong size");
static_assert(sizeof(bucket_update_t<3, emptyhint_t>) == 4, "wrong size");

/* we're only really considering using level 1 l someday here, and that
 * has no impact on the overall memory footprint anyway.
 */
bu_explicit(1, logphint_t, ATTR_ALIGNED(4));
bu_explicit(2, logphint_t, ATTR_ALIGNED(8));
static_assert(sizeof(bucket_update_t<1, logphint_t>) == 4, "wrong size");
static_assert(sizeof(bucket_update_t<2, logphint_t>) == 8, "wrong size");

/* it's admittedly somewhat unsatisfactory. I wish I could find a better
 * way. Maybe with alignas ? Is it a trick I can play at template scope
 * with CRTP or so ?
 *
 * (see also https://gcc.gnu.org/bugzilla/show_bug.cgi?id=48138)
 */
bu_explicit(1, shorthint_t, ATTR_ALIGNED(4));
static_assert(sizeof(bucket_update_t<1, shorthint_t>) == 4, "wrong size");

/* 4-byte slice index, 2-byte slice offset, and then 2 bytes of position
 * (bucket_update_size_per_level<1> is 16-bits).
 */
bu_explicit(1, longhint_t, ATTR_ALIGNED(8));
static_assert(sizeof(bucket_update_t<1, longhint_t>) == 8, "wrong size");

bu_explicit(1, primehint_t, ATTR_ALIGNED(8));
static_assert(sizeof(bucket_update_t<1, primehint_t>) == 8, "wrong size");

bu_explicit(2, shorthint_t, ATTR_ALIGNED(8));
static_assert(sizeof(bucket_update_t<2, shorthint_t>) == 8, "wrong size");

bu_explicit(2, longhint_t, ATTR_ALIGNED(16));
static_assert(sizeof(bucket_update_t<2, longhint_t>) == 16, "wrong size");

bu_explicit(3, shorthint_t, ATTR_ALIGNED(8));
static_assert(sizeof(bucket_update_t<3, shorthint_t>) == 8, "wrong size");

/******** Bucket array typedef **************/
/******** Bucket array implementation **************/
template <int LEVEL, typename HINT>
struct bucket_slice_alloc_defaults {
        static const slice_index_t initial = 64;
        static const slice_index_t increase = 64;
};

template <int LEVEL>
struct bucket_slice_alloc_defaults<LEVEL, longhint_t> {
        static const slice_index_t initial = 1;
        static const slice_index_t increase = 1;
};

template <int LEVEL>
struct bucket_slice_alloc_defaults<LEVEL, logphint_t> {
        static const slice_index_t initial = 1;
        static const slice_index_t increase = 1;
};


template <int LEVEL, typename HINT>
class bucket_array_t : private NonCopyable {
    /* We want to be able to reseat the reference in the course of the
     * computation.
     */
    las_output * output_p = nullptr;
    public:
  static const int level = LEVEL;
  typedef bucket_update_t<LEVEL, HINT> update_t;
    private:
  update_t *big_data = 0;
  size_t big_size = 0;                  // size of bucket update memory

  update_t ** bucket_write = 0;         // Contains pointers to first empty
                                        // location in each bucket
  update_t ** bucket_start = 0;         // Contains pointers to beginning of
                                        // buckets
  update_t ** bucket_read = 0;          // Contains pointers to first unread
                                        // location in each bucket
  slice_index_t * slice_index = 0;      // For each slice that gets sieved,
                                        // new index is added here
  update_t ** slice_start = 0;          // For each slice there are
                                        // n_bucket pointers, each
                                        // pointer tells where in the
                                        // corresponding bucket the
                                        // updates from that slice start
public:
  uint32_t n_bucket = 0;                // Number of buckets
private:
  size_t   size_b_align = 0;            // cacheline-aligned room for a
                                        // set of n_bucket pointers

  size_t   nr_slices = 0;               // Number of different slices
  size_t   alloc_slices = 0;            // number of checkpoints (each of size
                                        // size_b_align) we have allocated

  static constexpr slice_index_t initial_slice_alloc = bucket_slice_alloc_defaults<LEVEL, HINT>::initial;
  static constexpr slice_index_t increase_slice_alloc = bucket_slice_alloc_defaults<LEVEL, HINT>::increase;

  /* Get a pointer to the pointer-set for the i_slice-th slice */
  update_t ** get_slice_pointers(const slice_index_t i_slice) const {
    ASSERT_ALWAYS(i_slice < nr_slices);
    ASSERT_ALWAYS(size_b_align % sizeof(update_t *) == 0);
    return (slice_start + i_slice * size_b_align / sizeof(update_t *));
  }
  void free_slice_start();
  void realloc_slice_start(size_t);
  void log_this_update (const update_t update, uint64_t offset,
                        uint64_t bucket_number, where_am_I & w) const;
public:
  size_t nb_of_updates(const int i) const {
      ASSERT((uint32_t) i < n_bucket);
      return bucket_write[i] - bucket_start[i];
  }
  size_t room_allocated_for_updates(const int i) const {
      ASSERT((uint32_t) i < n_bucket);
      return bucket_start[i+1] - bucket_start[i];
  }
  /* Constructor sets everything to zero, and does not allocate memory.
     allocate_memory() does all the allocation. */
  bucket_array_t() = default;
  /* Destructor frees memory, if memory was allocated. If it wasn't, it's
     basically a no-op, except for destroying the bucket_array_t itself. */
  ~bucket_array_t();

  void reseat_output(las_output & o) { output_p = &o; }

  /* Lacking a move constructor before C++11, we make a fake one. We use this
     to store the bucket_array_t on the stack in fill_in_buckets(), to remove
     one level of pointer dereferencing.
     This method copies all the fields of other, then sets them to 0 in other.
     Deconstructing the other bucket_array_t after move() is a no-op, as if
     other had been constructed, but never run allocate_memory(). */
  void move(bucket_array_t &other);

  /* Allocate enough memory to be able to store at least _n_bucket buckets,
     each of at least _bucket_size entries. If at least as much memory had
     already been allocated, does not resize it.  */
  void allocate_memory(
          las_memory_accessor & memory,
          const uint32_t _n_bucket,
          const double fill_ratio,
          int logI,
          const slice_index_t prealloc_slices = initial_slice_alloc);

private:
  las_memory_accessor * used_accessor = nullptr;
  /* Return a begin iterator over the update_t entries in i_bucket-th
     bucket, generated by the i_slice-th slice */
  const update_t *begin(const size_t i_bucket, const slice_index_t i_slice) const {
    ASSERT_ALWAYS(i_slice < nr_slices);
    const update_t * const p = get_slice_pointers(i_slice)[i_bucket];
    /* The first slice we wrote must start at the bucket start */
    ASSERT_ALWAYS(i_slice != 0 || p == bucket_start[i_bucket]);
    return p;
  }
  /* Return an end iterator over the update_t entries in i_bucket-th
     bucket, generated by the i_slice-th slice */
  const update_t *end(const size_t i_bucket, const slice_index_t i_slice) const {
    ASSERT_ALWAYS(i_slice < nr_slices);
    return (i_slice + 1 < nr_slices) ? get_slice_pointers(i_slice + 1)[i_bucket] :
      bucket_write[i_bucket];
  }

public:
  /* proxy to allow range-based for loops */
  struct slice_range_accessor {
      bucket_array_t<LEVEL, HINT> const * parent;
      size_t i_bucket;
      slice_index_t i_slice;
      const update_t *begin() const { return parent->begin(i_bucket, i_slice); }
      const update_t *end() const { return parent->end(i_bucket, i_slice); }
  };

  friend struct slice_range_accessor;

  slice_range_accessor slice_range(size_t i_bucket, slice_index_t i_slice) const {
      return slice_range_accessor { this, i_bucket, i_slice };
  }

  void reset_pointers();
  slice_index_t get_nr_slices() const {return nr_slices;}
  slice_index_t get_slice_index(const slice_index_t i_slice) const {
    ASSERT_ALWAYS(i_slice < nr_slices);
    return slice_index[i_slice];
  }
  void add_slice_index(const slice_index_t new_slice_index) {
    /* Write new set of pointers for the new factor base slice */
    ASSERT_ALWAYS(nr_slices <= alloc_slices);
    if (nr_slices == alloc_slices) {
      /* We're out of allocated space for the checkpoints. Realloc to bigger
         size. We add space for increase_slice_alloc additional entries. */
      realloc_slice_start(increase_slice_alloc);
    }
    aligned_medium_memcpy((uint8_t *)slice_start + size_b_align * nr_slices, bucket_write, size_b_align);
    slice_index[nr_slices++] = new_slice_index;
  }
  double max_full (unsigned int * fullest_index = NULL) const;
  double average_full () const;
  /* Push an update to the designated bucket. Also check for overflow, if
     SAFE_BUCKET_ARRAYS is defined. */
  inline void push_update(const int i, const update_t &update);

  /* Create an update for a hit at location offset and push it to the
     coresponding bucket */
  inline void push_update(const uint64_t offset, const fbprime_t p,
      const slice_offset_t slice_offset, const slice_index_t slice_index,
      where_am_I & w);

  template<typename hh = HINT>
  inline
  typename std::enable_if<std::is_same<hh,emptyhint_t>::value, void>::type
  push_update(const uint64_t offset, where_am_I & w MAYBE_UNUSED)
  {
      int logB = LOG_BUCKET_REGIONS[LEVEL];
      const uint64_t bucket_number = offset >> logB;
      ASSERT_EXPENSIVE(bucket_number < n_bucket);
      update_t update(offset & ((UINT64_C(1) << logB) - 1));
      push_update(bucket_number, update);
  }
  template<typename hh = HINT>
  inline
  typename std::enable_if<std::is_same<hh,logphint_t>::value, void>::type
  push_update(const uint64_t offset, logphint_t const & logp, where_am_I & w MAYBE_UNUSED)
  {
      int logB = LOG_BUCKET_REGIONS[LEVEL];
      const uint64_t bucket_number = offset >> logB;
      ASSERT_EXPENSIVE(bucket_number < n_bucket);
      update_t update(offset & ((UINT64_C(1) << logB) - 1), logp);
      push_update(bucket_number, update);
  }
};

/* Downsort sorts the updates in the bucket_index-th bucket of a level-n
   bucket array into a level n-1 bucket array. The update type of the level n
   bucket array can be short or long hint; the level n-1 bucket array is
   always longhint. */
template <int INPUT_LEVEL>
void
downsort(fb_factorbase::slicing const &,
        bucket_array_t<INPUT_LEVEL - 1, longhint_t> &BA_out,
        const bucket_array_t<INPUT_LEVEL, shorthint_t> &BA_in,
        uint32_t bucket_index, where_am_I & w);

template <int INPUT_LEVEL>
void
downsort(fb_factorbase::slicing const &,
        bucket_array_t<INPUT_LEVEL - 1, longhint_t> &BA_out,
        const bucket_array_t<INPUT_LEVEL, longhint_t> &BA_in,
        uint32_t bucket_index, where_am_I & w);

/* And then we have the "other" downsort, that disregards the hints
 * completely. This is for the "no resieve" case.
 */

template <int INPUT_LEVEL>
void
downsort(fb_factorbase::slicing const &,
        bucket_array_t<INPUT_LEVEL - 1, logphint_t> &BA_out,
        const bucket_array_t<INPUT_LEVEL, emptyhint_t> &BA_in,
        uint32_t bucket_index, where_am_I & w);

template <int INPUT_LEVEL>
void
downsort(fb_factorbase::slicing const &,
        bucket_array_t<INPUT_LEVEL - 1, logphint_t> &BA_out,
        const bucket_array_t<INPUT_LEVEL, logphint_t> &BA_in,
        uint32_t bucket_index, where_am_I & w);

/* A class that stores updates in a single "bucket".
   It's really just a container class with pre-allocated array for storage,
   a persistent read and write pointer. */
template <int LEVEL, typename HINT>
class bucket_single {
  typedef bucket_update_t<LEVEL, HINT> update_t;
  update_t *start; /* start is a "strong" reference */
  update_t *read;  /* read and write are "weak" references into the allocated memory */
  update_t *write;
  size_t _size;
  las_memory_accessor * used_accessor = nullptr;
public:
  bucket_single () {
    start = nullptr;
    read = start;
    write = start;
    _size = 0;
  }

  void allocate_memory(las_memory_accessor & memory, size_t size)
  {
      used_accessor = &memory;
      _size = size;
      start = (update_t *) used_accessor->alloc_frequent_size(_size * sizeof(update_t));
      read = start;
      write = start;
  }

  ~bucket_single() {
      if (start)
          used_accessor->free_frequent_size((update_t *) start, _size * sizeof(update_t));
      start = read  = write = NULL;
      _size = 0;
  }

  /* A few of the standard container methods */
  const update_t * begin() const {return start;}
  const update_t * end() const {return write;}
  size_t size() const {return write - start;}

  /* Main writing function: appends update to bucket number i.
   * If SAFE_BUCKETS_SINGLE is not #defined, then there is no checking that there is
   * enough room for the update. This could lead to a segfault, with the
   * current implementation!
   */
  inline void push_update (const update_t &update);
  inline const update_t &get_next_update ();
  void rewind_by_1() {if (read > start) read--;}
  bool is_end() const { return read == write; }

  void sort ();
};

/* Stores info containing the complete prime instead of only the low 16 bits */
class bucket_primes_t : public bucket_single<1, primehint_t> {
  typedef bucket_single<1, primehint_t> super;
public:  
  // allocate_memory (las_memory_accessor & memory, const size_t size) { super::allocate_memory(memory, size); }
  void purge (const bucket_array_t<1, shorthint_t> &BA, 
          int i, fb_factorbase::slicing const & fb, const unsigned char *S);
};

/* Stores info containing both slice index and offset instead of only the offset */
class bucket_array_complete : public bucket_single<1, longhint_t> {
    typedef bucket_single<1, longhint_t> super;
public:  
  // allocate_memory (las_memory_accessor & memory, const size_t size) { super::allocate_memory(memory, size); }
  template <typename HINT>
  void purge (const bucket_array_t<1, HINT> &BA, int i, const unsigned char *S);
  template <typename HINT>
  void purge (const bucket_array_t<1, HINT> &BA, int i,
              const unsigned char *S,
              const std::vector<typename bucket_update_t<1, HINT>::br_index_t> &survivors);
private:
#ifdef HAVE_SSE2
  template <typename HINT, int SIZE>
  void purge_1 (
      const bucket_array_t<1, HINT> &BA, const int i,
      const std::vector<typename bucket_update_t<1, HINT>::br_index_t> &survivors);
#endif
};

extern template class bucket_array_t<1, shorthint_t>;
extern template class bucket_array_t<2, shorthint_t>;
extern template class bucket_array_t<3, shorthint_t>;
extern template class bucket_array_t<1, longhint_t>;
extern template class bucket_array_t<2, longhint_t>;
extern template class bucket_array_t<1, emptyhint_t>;
extern template class bucket_array_t<2, emptyhint_t>;
extern template class bucket_array_t<3, emptyhint_t>;
extern template class bucket_array_t<1, logphint_t>;
extern template class bucket_array_t<2, logphint_t>;

#endif	/* BUCKET_HPP_ */