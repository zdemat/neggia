#include <boost/format.hpp>
#include <iomanip> // put_time

#include "Synchronization.h"

// for uid
#include <pwd.h>
#include <sys/types.h>
#include <unistd.h>
#include <stdio.h>

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

namespace detail {
  
  _str_neggia_shm_id::_str_neggia_shm_id(): std::string("neggia_synchronization") {
    // add "uid"
    struct passwd *p;
    uid_t  uid;
    if ((p = getpwuid(uid = getuid())) != NULL) {
      append("_");
      append(p->pw_name);
    } else
      throw std::runtime_error("Synchronization::detail::_str_neggia_shm_id: Could not initialize shared segment name.");
    // add "shm" suffix
    append("_shm");
  };
  
}
  
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
        if( strlen(oname)==7 && strncmp(oname,"version",7)==0 ) {
          auto ret = smt.find<Shared::string>(oname);
          if( ret.second!=0 ) {
            os << " * " << "String(version:" << (*ret.first) << ")\n";
          }
        } else if( strncmp(oname,"mtx:",4)==0 ) {
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
  
  SharedSegment::SharedSegment(const char* name, const char* version) :
    name(name), version(version),
    mtx(bip::open_or_create, (std::string(name)+"_mutex").c_str())
  {
    //Lock
    bip::scoped_lock<bip::named_recursive_mutex> lock(mtx);
    //Sanitize TODO::
    //Create shared memory
    try {
      smt = Shared::segment(bip::open_or_create, name, 65536);
      //std::cout << Utils::put_now() << boost::format(" SharedSegment(%s): segment ready\n") % name;
    } 
    catch (std::exception& e) {
      std::cerr << "Synchronization::SharedSegment::SharedSegment: Exception thrown when creating/opening shared memory segment.\n";
      std::cerr << e.what() << std::endl;
      throw(e);
    }
    //Save version string
    std::pair<Shared::string* , std::size_t> d_version;
    try { 
      d_version = smt.find<Shared::string>("version");
    }
    catch (std::exception& e) {
      std::cerr << "Synchronization::SharedSegment::SharedSegment: Exception thrown when trying to find memory version string.\n";
      std::cerr << e.what() << std::endl;
      throw(e);
    }
    if(d_version.second>0) {
      ptr_smt_version = d_version.first;
      if(std::string(version).compare(d_version.first->c_str())!=0)
	std::cerr << "Warning: Shared memory segment and library are not same version\n";
    } else {
      //Save version string
      try {
	ptr_smt_version = smt.find_or_construct<Shared::string>("version")(version, smt.get_allocator<Shared::alloc<char>>());
      }
      catch (std::exception& e) {
	std::cerr << "Synchronization::SharedSegment::SharedSegment: Exception thrown when saving version string.\n";
	std::cerr << e.what() << std::endl;
	throw(e);
      }
    }
  }

  SharedSegment::SharedSegment(const SharedSegment& _) :
    name(_.name), version(_.version),
    mtx(bip::open_only, (_.name+"_mutex").c_str()) // (empty) implicit constructor may be not allowed
  {
    throw std::logic_error("THIS CODE IS NOT CLEAN. Do not call this!");
      //Lock
      bip::scoped_lock<bip::named_recursive_mutex> lock(mtx);
      //Sanitize TODO::
      //Create shared memory
      try {
        smt = Shared::segment(bip::open_only, name.c_str());
        //std::cout << Utils::put_now() << boost::format(" SharedSegment(%s): segment ready\n") % name;
      } 
      catch (std::exception& e) {
        std::cerr << "Synchronization::SharedSegment::SharedSegment: Exception thrown when opening shared memory segment." << std::endl;
        std::cerr << e.what() << '\n';
        throw(e);
      }
      //get segment version string
      std::pair<Shared::string* , std::size_t> d_version;
      try { 
	d_version = smt.find<Shared::string>("version");
      }
      catch (std::exception& e) {
	std::cerr << "Synchronization::SharedSegment::SharedSegment: Exception thrown when trying to find memory version string." << std::endl;
	std::cerr << e.what() << '\n';
	throw(e);
      }
      if(d_version.second>0) {
	ptr_smt_version = d_version.first;
	if(std::string(version).compare(d_version.first->c_str())!=0)
	  std::cerr << "Warning: Shared memory segment and library are not same version\n";
      } else
	throw std::runtime_error("Synchronization::SharedSegment::SharedSegment: Could not find `version` string in shared memory segment.");
  }
   
  SharedSegment::~SharedSegment()
  {
    bool rm_mtx = false;
    try {
      //Lock
      bip::scoped_lock<bip::named_recursive_mutex> lock(mtx);
      //Clean selfrefering(unique) shared pointers
      RemoveUniqueSharedPointers();
      //Sanitize TODO::
      
      //Remove shared memory if empty
      if( (smt.get_num_unique_objects()==0 && smt.get_num_named_objects()==0) ||
	  (smt.get_num_unique_objects()==0 && smt.get_num_named_objects()==1 && smt.find<Shared::string>("version").second>0) ) {
        bip::shared_memory_object::remove(name.c_str());
	rm_mtx = true;
        //std::cout << Utils::put_now() << boost::format(" ~SharedSegment(%s): segment removed\n") % name;
      }
    }
    catch (std::exception& e) {
      std::cerr << "Synchronization::SharedSegment::~SharedSegment: Exception thrown when trying to cleanup/remove shared memory segment." << std::endl;
      std::cerr << e.what() << '\n';
    }
    //if(rm_mtx)
    //  bip::named_recursive_mutex::remove((name+"_mutex").c_str());
  }
  
  template<typename T> void _SharedSegment_removeAllByName(Shared::segment &smt, const char* name, size_t len)
  {
      std::vector<std::string> list;
      typedef Shared::segment::const_named_iterator const_named_it;
      const_named_it named_beg = smt.named_begin();
      const_named_it named_end = smt.named_end();
      
      for(; named_beg != named_end; ++named_beg) {
        const Shared::segment::char_type *str = named_beg->name();
        if( strncmp(str,name,len)==0 )
          list.push_back(name);
      }

      for(auto it=list.begin(); it!=list.end(); ++it) {
	auto ret = smt.find<T>(it->c_str());
	if(ret.second>0 && ret.first->use_count()<=1)
	  smt.destroy<T>(it->c_str());
      }
  }

  void SharedSegment::RemoveUniqueSharedPointers()
  {
    //Lock
    bip::scoped_lock<bip::named_recursive_mutex> lock(mtx);

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
    //_findAllByName();

    for(auto it=list.begin(); it!=list.end(); ++it) {
      auto ret = smt.find<SyncObjPtr>(it->c_str());
      if(ret.second>0 && ret.first->use_count()<=1)
        smt.destroy<SyncObjPtr>(it->c_str());
    }

    /*auto _removeObjPtrsByName = [&]()
      { _SharedSegment_removeAllByName<SyncObjPtr>(smt, "ptr_obj:", 8); };

      smt.atomic_func(_removeObjPtrsByName);*/

    //'ptr_mtx:' pointers in shared memory
    list.clear();
    str = "ptr_mtx:"; num = 8;
       
    smt.atomic_func(_findAllByName);
    //_findAllByName();
    
    for(auto it=list.begin(); it!=list.end(); ++it) {
      auto ret = smt.find<SyncMtxPtr>(it->c_str());
      if(ret.second>0 && ret.first->use_count()<=1)
        smt.destroy<SyncMtxPtr>(it->c_str());
    }
    
      /*auto _removeMtxPtrsByName = [&]()
      { _SharedSegment_removeAllByName<SyncMtxPtr>(smt, "ptr_mtx:", 8); };

      smt.atomic_func(_removeMtxPtrsByName);*/
  }
    
  Shared::shared_ptr<Shared::NeggiaDsetSyncObj> Factory::find_or_create_dset(
    SharedSegment& shm,
    const std::string& filepath, const std::string& dsetpath)
  {
    // shared memory segment
    Shared::segment & smt = shm.smt;
    
    //Lock
    bip::scoped_lock<bip::named_recursive_mutex> lock(shm.mtx);
    
    //Cleanup
    try {
      shm.RemoveUniqueSharedPointers();
    }
    catch (...) {
      std::cerr << "Synchronization::Factory::find_or_create_dset: Exception thrown when cleaning dummy shared pointers." << std::endl;
      throw;
    }

    //canonize filepath, dsetpath TODO
    //Dataset name
    const std::string dset_name = filepath+":"+dsetpath;

    //Try to find ptr_mtx
    SyncMtxPtr ptr;

    size_t ex_counter = 0;
  ex_label:
    std::pair<SyncMtxPtr *, std::size_t> retm;
    try { 
      retm = smt.find<SyncMtxPtr>(("ptr_mtx:"+dset_name).c_str());
    }
    catch (...) {
      if (++ex_counter<10)
	  goto ex_label;
      std::cerr << "Synchronization::Factory::find_or_create: Exception thrown when trying to find ptr_mtx." << std::endl;
      std::cerr << "Synchronization::Factory::find_or_create_dset:";
      std::cerr << " pid@thread=" << boost::format("%ld@x%06x") % getpid() % pthread_self();
      std::cerr << ", dset_name=" << dset_name.c_str() << "\n";
      std::cerr << put_NegiaSyncObjShmInfo();
      throw;
    }

    if(retm.second>0) {
      ptr = *retm.first;
    } else {
      //Create mtx, ptr_mtx and save the in shared memory
      try {
	auto ret = smt.find<SyncMtx>(("mtx:"+dset_name).c_str());
	SyncMtx* p = (ret.second>0) ? ret.first : smt.construct<SyncMtx>(("mtx:"+dset_name).c_str())
	  (dset_name.c_str(),smt.get_allocator<Shared::alloc<char>>());
	ptr = bip::make_managed_shared_ptr(p,smt);
	ptr = *smt.construct<SyncMtxPtr>(("ptr_mtx:"+dset_name).c_str())(ptr);
      }
      catch (...) {
	if (++ex_counter<10)
	  goto ex_label;
	std::cerr << "Synchronization::Factory::find_or_create_dset: Exception thrown when trying to find/create mtx." << std::endl;
	std::cerr << "Synchronization::Factory::find_or_create_dset:";
	std::cerr << " pid@thread=" << boost::format("%ld@x%06x") % getpid() % pthread_self();
	std::cerr << ", dset_name=" << dset_name.c_str() << "\n";
	std::cerr << put_NegiaSyncObjShmInfo();
	throw;
      }
    }

    //PID and thread ID
    std::string id;
    try {
      std::ostringstream os;
      os << boost::format("%ld@x%06x") % getpid() % pthread_self();
      id = os.str();
    }
    catch (...) {
      std::cerr << "Synchronization::Factory::find_or_create_dset: Exception thrown when creating obj id." << std::endl;
      throw;
    }
    
    //Find or construct shared NeggiaDsetSyncObj as owner of the mutex above
    SyncObj* pobj;

    try {
      pobj = smt.find_or_construct<SyncObj>(("DsetSyncObj:"+id+":"+dset_name).c_str())
	(dset_name.c_str(),ptr,smt.get_allocator<Shared::alloc<char>>());
    }
    catch (...) {
      std::cerr << "Synchronization::Factory::find_or_create_dset: Exception thrown when trying to get NeggiaDsetSyncObj." << std::endl;
      throw;
    }

    //Try to find ptr_obj
    SyncObjPtr spobj;

    try {

      auto reto = smt.find<SyncObjPtr>(("ptr_obj:"+id+":"+dset_name).c_str());

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

    }
    catch (...) {
      std::cerr << "Synchronization::Factory::find_or_create_dset: Exception thrown when trying to find ptr_obj." << std::endl;
      throw;
    }

    return spobj;
  }
  
  void Factory::destroy_ptr(SharedSegment& shm, Shared::shared_ptr<Shared::NeggiaDsetSyncObj>* ptr)
  {
    // shared memory segment
    Shared::segment & smt = shm.smt;
    
    //Lock
    bip::scoped_lock<bip::named_recursive_mutex> lock(shm.mtx);
    
    //Cleanup
    try {
      shm.RemoveUniqueSharedPointers();
    }
    catch (...) {
      std::cerr << "Synchronization::Factory::find_or_create_dset: Exception thrown when cleaning dummy shared pointers." << std::endl;
      throw;
    }
    
    // destroy shared poiter
    smt.destroy_ptr<SyncObjPtr>(ptr);
  }
       
} // namespace Synchronization
