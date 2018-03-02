#ifndef NEGGIA_SYNCHRONIZATION_H
#define NEGGIA_SYNCHRONIZATION_H

#include <boost/interprocess/managed_shared_memory.hpp>
#include <boost/interprocess/sync/named_mutex.hpp>
#include <boost/interprocess/sync/named_recursive_mutex.hpp>
#include <boost/interprocess/smart_ptr/shared_ptr.hpp>
#include <boost/interprocess/smart_ptr/weak_ptr.hpp>
#include <boost/interprocess/containers/string.hpp>
#include <string>
#include <iostream>
#include <boost/format.hpp>

// ------------------------------------------------------------------------------------------------------------
/* Install customized boost assertion msg including stacktrace */
#if BOOST_VERSION >= 106500
// BOOST_ENABLE_ASSERT_DEBUG_HANDLER is defined for the whole project
#include <stdexcept>    // std::logic_error
#include <iostream>     // std::cerr
#include <boost/stacktrace.hpp>

namespace boost {
  inline void assertion_failed_msg(char const* expr, char const* msg, char const* function, char const* file, long line) {
    std::cerr << "?????????" << ": "
	      << file << ":" << line << ": "
	      << function << ": Assertion `"
	      << expr << "' failed.\n"
	      << "Backtrace:\n" << boost::stacktrace::stacktrace() << '\n';
    std::abort();
  }

  inline void assertion_failed(char const* expr, char const* function, char const* file, long line) {
    ::boost::assertion_failed_msg(expr, 0 /*nullptr*/, function, file, line);
  }
} // namespace boost
#endif /*  BOOST_VERSION >= 116500 */
// ------------------------------------------------------------------------------------------------------------

/* Faked std::put_time for GCC<5 */
#if __GNUC__ < 5
namespace std {
template<typename _CharT>
  struct _Put_time
{
  const std::tm* _tmb;
  const _CharT* _fmt;
};

template<typename _CharT>
inline _Put_time<_CharT>
put_time(const std::tm* __tmb, const _CharT* __fmt)
{ return { __tmb, __fmt }; }

template<typename _CharT, typename _Traits>
basic_ostream<_CharT, _Traits>&
operator<<(basic_ostream<_CharT, _Traits>& __os, _Put_time<_CharT> __f)
{
  __os << "(obsolate compiler)";
  return __os;
}
} // namespace std
#endif /* __GNUC__ < 5 */

namespace Utils {
  
class put_now { // insert current date and time into the stream
public:
  friend std::ostream& operator<<(std::ostream& os, const put_now& obj);
};
  
} // namespace Utils

namespace Synchronization {

static const char* neggia_shm_id = "neggia_synchronization_shm";
static const char* default_shm_id = neggia_shm_id;
  
using atomic_lock = pthread_spinlock_t;

namespace bip = boost::interprocess;

template < typename Alloc = std::allocator<char> >
struct BasicNeggiaDsetSyncMtx {
  using string = bip::basic_string<char, std::char_traits<char>, typename Alloc::template rebind<char>::other>;

  template<typename T>
  BasicNeggiaDsetSyncMtx(T&& name, Alloc alloc = {}) :
    name(std::forward<T>(name), alloc)
  { 
    //std::cout << Utils::put_now() << boost::format(" MtxConstructor: %s\n") % name;
    pthread_spin_init(&mtx, PTHREAD_PROCESS_SHARED);
  }

  ~BasicNeggiaDsetSyncMtx()
  { 
    pthread_spin_destroy(&mtx);
    //std::cout << Utils::put_now() << boost::format(" MtxDestructor: %s\n") % name;
  }

  string      name;
  atomic_lock mtx;
  
  friend std::ostream& operator<<(std::ostream& os, const BasicNeggiaDsetSyncMtx& obj)
  {
    os << "NeggiaDsetSyncMtx(name=" << obj.name << ")";
    return os;
  };
};

using NeggiaDsetSyncMtx = BasicNeggiaDsetSyncMtx<>; // just heap allocated

template < typename Tptr = std::shared_ptr<NeggiaDsetSyncMtx>, typename Alloc = std::allocator<char> >
struct BasicNeggiaDsetSyncObj {
  using string = bip::basic_string<char, std::char_traits<char>, typename Alloc::template rebind<char>::other>;

  template<typename T>
  BasicNeggiaDsetSyncObj(T&& name, Tptr ptr, Alloc alloc = {}) :
    name(std::forward<T>(name), alloc), pid(getpid()), threadid(pthread_self()), ptr(ptr)
  {
    //std::cout << Utils::put_now() << boost::format(" ObjConstructor: %s\n") % name;
  }

  ~BasicNeggiaDsetSyncObj()
  {
    //std::cout << Utils::put_now() << boost::format(" ObjDestructor: %s\n") % name;
  }

  string    name;
  pid_t     pid;
  pthread_t threadid;
  Tptr      ptr;
  
  friend std::ostream& operator<<(std::ostream& os, const BasicNeggiaDsetSyncObj& obj)
  {
    os << "NeggiaDsetSyncObj(name=" << obj.name << ")";
    os << " [PID=" << obj.pid << "]";
    os << " [threadID=" << boost::format("0x%06x") % obj.threadid << "]";
    return os;
  };
};

using NeggiaDsetSyncObj = BasicNeggiaDsetSyncObj<>; // just heap allocated

namespace Shared {

  using segment                      = bip::managed_shared_memory; // or managed_mapped_file
  using segment_manager              = segment::segment_manager;

  template<class T> using alloc      = bip::allocator<T, segment_manager >;
  template<class T> using deleter    = bip::deleter<T, segment_manager >;
  template<class T> using shared_ptr = typename bip::managed_shared_ptr<T, segment >::type; // eq bip::shared_ptr<T, alloc(void), deleter<T> >???
  template<class T> using weak_ptr   = typename bip::managed_weak_ptr<T, segment >::type; // eq bip::weak_ptr<T, alloc(void), deleter<T> >
  
  using string                       = bip::basic_string<char, std::char_traits<char>, alloc<char> >;

  using NeggiaDsetSyncMtx            = BasicNeggiaDsetSyncMtx< alloc<char> >; // shared memory version
  using NeggiaDsetSyncObj            = BasicNeggiaDsetSyncObj< shared_ptr<NeggiaDsetSyncMtx>, alloc<char> >; // shared memory version

  class put_SegmentInfo { // insert segment info into the stream
  protected:
    std::string name;
  public:
    put_SegmentInfo(const char* name) : name(name) { };
    friend std::ostream& operator<<(std::ostream& os, const put_SegmentInfo& obj);
  };
  
  std::ostream& printSegmentInfo(std::ostream& sout=std::cout, const char* name=default_shm_id);
} // namespace Shared
  
class put_NegiaSyncObjShmInfo { // insert Neggia synchronization object info into the stream
public:
  friend std::ostream& operator<<(std::ostream& os, const put_NegiaSyncObjShmInfo& obj);
};
 
class SharedSegment
{
  friend class Factory;
public:
  SharedSegment(const char* name=default_shm_id);
  SharedSegment(const SharedSegment& );
  ~SharedSegment();
protected:
  void RemoveUniqueSharedPointers();
protected:
  std::string name;
  Shared::segment smt;
  bip::named_recursive_mutex mtx;
};

class Factory
{
public:
  static Shared::shared_ptr<Shared::NeggiaDsetSyncObj> find_or_create_dset(
            SharedSegment& shm,
            const std::string& filepath, const std::string& dsetpath);
  static void destroy_ptr(SharedSegment& shm, Shared::shared_ptr<Shared::NeggiaDsetSyncObj>* ptr);
};

} // namespace Synchronization

#endif // NEGGIA_SYNCHRONIZATION_H
