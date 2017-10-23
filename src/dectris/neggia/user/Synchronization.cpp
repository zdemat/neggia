#include <boost/format.hpp>
#include <boost/date_time/posix_time/posix_time.hpp>
  
#include "Synchronization.h"

namespace Utils {
  std::ostream& operator<<(std::ostream& os, const put_now& obj)
  {
    auto t = std::time(nullptr);
    auto tm = *std::localtime(&t);
    os << std::put_time(&tm, "%Y-%m-%d %H:%M:%S");
    return os;
  }
}

namespace Synchronization {

  using SyncObj        = Shared::NeggiaDsetSyncObj;
  using SyncObjPtr     = Shared::shared_ptr<SyncObj>;
  using SyncObjWeakPtr = Shared::weak_ptr<SyncObj>;
    
  using SyncMtx        = Shared::NeggiaDsetSyncMtx;
  using SyncMtxPtr     = Shared::shared_ptr<SyncMtx>;
  using SyncMtxWeakPtr = Shared::weak_ptr<SyncMtx>;
  
namespace Shared {
  
  std::ostream& operator<<(std::ostream& os, const put_SegmentInfo& obj)
  {
    segment smt;
    
    try {
      smt = segment(bip::open_only, obj.name.c_str());
    }
    catch(bip::interprocess_exception &ex) {
      os << boost::format("No shared segment: %s\n") % obj.name;
      return os;
    }
    
    os<< boost::format("< === Segment: %s ===\n") % obj.name;
    
    auto _printSegmentInfo = [&]() {  
      typedef segment::const_named_iterator const_named_it;
      const_named_it named_beg = smt.named_begin();
      const_named_it named_end = smt.named_end();

      for(; named_beg != named_end; ++named_beg) {
        const segment::char_type *oname = named_beg->name();
        os << boost::format(" * %s\n") % oname;
      }
    };
    smt.atomic_func(_printSegmentInfo);
    os << boost::format(" --- Segment: %s --- >\n") % obj.name;
    return os;
  }
  
} // namespace Shared

  std::ostream& operator<<(std::ostream& os, const put_NegiaSyncObjShmInfo& obj)
  {
    using namespace Shared;
    
    segment smt;
    
    try {
      smt = segment(bip::open_only, neggia_shm_id);  
    }
    catch(bip::interprocess_exception &ex) {
      os << boost::format("No shared segment: %s\n") % neggia_shm_id;
      return os;
    }
    
    os << boost::format("< === NeggiaSyncObjShmInfo ===\n");    
    
    auto _print = [&]() {  
      typedef segment::const_named_iterator const_named_it;
      const_named_it named_beg = smt.named_begin();
      const_named_it named_end = smt.named_end();

      for(; named_beg != named_end; ++named_beg) {
        const segment::char_type *oname = named_beg->name();
        if( strncmp(oname,"mtx:",4)==0 ) {
          auto ret = smt.find<SyncMtx>(oname);
          if( ret.second!=0 ) {
            assert(oname=="mtx:"+ret.first->name);
            os << " * " << (*ret.first) << "\n";
          }
        } else if (strncmp(oname,"DsetSyncObj:",12)==0 ) {
          auto ret = smt.find<SyncObj>(oname);
          if( ret.second!=0 ) {
            //assert(oname=="mtx:"+ret.first->name);
            os << " * " << (*ret.first) << "\n";
          }
        } else if (strncmp(oname,"ptr_mtx:",8)==0 ) {
          auto ret = smt.find<SyncMtxPtr>(oname);
          if( ret.second!=0 ) {
            os << " * " << boost::format("NeggiaDsetSyncMtxPtr(name=%s) [(use_count-1)=%d]\n")
                            % (oname+8) % (ret.first->use_count()-1);
          }
	} else if (strncmp(oname,"ptr_obj:",8)==0 ) {
          auto ret = smt.find<SyncObjPtr>(oname);
          if( ret.second!=0 ) {
            os << " * " << boost::format("NeggiaDsetSyncObjPtr(name=%s) [(use_count-1)=%d]\n")
	      % (oname+8) % (ret.first->use_count()-1);
          }
        } else {
          os << boost::format(" * UnhandledObject(name=%s)\n") % oname;
        }
      }
    };  
    smt.atomic_func(_print);
    os << boost::format("--- NeggiaSyncObjShmInfo --- >\n");
    return os;
  }
  
  SharedSegment::SharedSegment(const char* name) :
    name(name), mtx(bip::open_or_create, (std::string(name)+"_mutex").c_str())
  {
    //Lock
    bip::scoped_lock<bip::named_mutex> lock(mtx);
    //Sanitize TODO::
    //Create shared memory
    smt = Shared::segment(bip::open_or_create, name, 65536);
    //std::cout << Utils::put_now() << boost::format(" SharedSegment(%s): segment ready\n") % name;
  }
  
  SharedSegment::~SharedSegment()
  {
    try {
      //Lock
      bip::scoped_lock<bip::named_mutex> lock(mtx);
      //Clean selfrefering(unique) shared pointers
      RemoveUniqueSharedPointers();
      //Sanitize TODO::
      
      //Remove shared memory if empty
      if(smt.get_num_named_objects()==0 && smt.get_num_unique_objects()==0) {
        bip::shared_memory_object::remove(name.c_str());
        //std::cout << Utils::put_now() << boost::format(" ~SharedSegment(%s): segment removed\n") % name;
      }
    }
    catch (std::exception& e) {
      std::cerr << e.what() << '\n';
    }
    bip::named_mutex::remove((name+"_mutex").c_str());
  }
  
  void SharedSegment::RemoveUniqueSharedPointers()
  {
    std::vector<std::string> list;
    const char* str; size_t num;

    //'ptr_obj:' pointers in shared memory
    str = "ptr_obj:"; num = 8;

    auto _findAllByName = [&]() {
      typedef Shared::segment::const_named_iterator const_named_it;
      const_named_it named_beg = smt.named_begin();
      const_named_it named_end = smt.named_end();

      for(; named_beg != named_end; ++named_beg) {
        const Shared::segment::char_type *oname = named_beg->name();
        if( strncmp(oname,str,num)==0 )
          list.push_back(oname);
      }
    };

    smt.atomic_func(_findAllByName);

    for(auto it=list.begin(); it!=list.end(); ++it) {
      auto ret = smt.find<SyncObjPtr>(it->c_str());
      if(ret.second>0 && ret.first->use_count()<=1)
        smt.destroy<SyncObjPtr>(it->c_str());
    }

    //'ptr_mtx:' pointers in shared memory
    list.clear();
    str = "ptr_mtx:"; num = 8;
       
    smt.atomic_func(_findAllByName);
    
    for(auto it=list.begin(); it!=list.end(); ++it) {
      auto ret = smt.find<SyncMtxPtr>(it->c_str());
      if(ret.second>0 && ret.first->use_count()<=1)
        smt.destroy<SyncMtxPtr>(it->c_str());
    }
  }
    
  Shared::shared_ptr<Shared::NeggiaDsetSyncObj> Factory::find_or_create_dset(
    SharedSegment& shm,
    const std::string& filepath, const std::string& dsetpath)
  {
    // shared memory segment
    Shared::segment & smt = shm.smt;
    
    //Lock
    bip::scoped_lock<bip::named_mutex> lock(shm.mtx);
    
    //Cleanup
    shm.RemoveUniqueSharedPointers();
    
    //canonize filepath, dsetpath TODO
    //Dataset name
    const std::string dset_name = filepath+":"+dsetpath;

    //Try to find ptr_mtx
    auto retm = smt.find<SyncMtxPtr>(("ptr_mtx:"+dset_name).c_str());
    SyncMtxPtr ptr;
    
    if(retm.second>0) {
      ptr = *retm.first;
    } else {
      //Create mtx, ptr_mtx and save the in shared memory
      auto ret = smt.find<SyncMtx>(("mtx:"+dset_name).c_str());
      SyncMtx* p = (ret.second>0) ? ret.first : smt.construct<SyncMtx>(("mtx:"+dset_name).c_str())
        (dset_name.c_str(),smt.get_allocator<Shared::alloc<char>>());
      ptr = bip::make_managed_shared_ptr(p,smt);
      ptr = *smt.construct<SyncMtxPtr>(("ptr_mtx:"+dset_name).c_str())(ptr);
    }    
    
    //PID and thread ID
    std::string id;
    {
      std::ostringstream os;
      os << boost::format("%ld@x%06x") % getpid() % pthread_self();
      id = os.str();
    }
    
    //Find or construct shared NeggiaDsetSyncObj as owner of the mutex above
    SyncObj* pobj = smt.find_or_construct<SyncObj>(("DsetSyncObj:"+id+":"+dset_name).c_str())
      (dset_name.c_str(),ptr,smt.get_allocator<Shared::alloc<char>>());
 
    //Try to find ptr_obj:                                                                                                                                                          
    auto reto = smt.find<SyncObjPtr>(("ptr_obj:"+id+":"+dset_name).c_str());
    SyncObjPtr spobj;

    if(reto.second>0) {
      spobj = *reto.first;
    } else {
      //Create SyncObj (as owner of the mutex above), ptr_obj and save the in shared memory                  
      auto ret = smt.find<SyncObj>(("DsetSyncObj:"+id+":"+dset_name).c_str());
      SyncObj* p = (ret.second>0) ? ret.first : smt.construct<SyncObj>(("DsetSyncObj:"+id+":"+dset_name).c_str())
        (dset_name.c_str(),ptr,smt.get_allocator<Shared::alloc<char>>());
      spobj = bip::make_managed_shared_ptr(p,smt);
      spobj = *smt.construct<SyncObjPtr>(("ptr_obj:"+id+":"+dset_name).c_str())(spobj);
    }

    return spobj;
  }
  
} // namespace Synchronization
