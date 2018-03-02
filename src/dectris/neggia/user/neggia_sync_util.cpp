#include <boost/format.hpp>
#include <boost/program_options.hpp>
#include <boost/algorithm/string.hpp>
#include <boost/scoped_ptr.hpp>

#include "Synchronization.h"

#include <chrono>
#include <thread>

using namespace Synchronization;

using SyncObj    = Shared::NeggiaDsetSyncObj;
using SyncObjPtr = Shared::shared_ptr<SyncObj>;

int main (int argc, char* argv[])
{
  namespace po = boost::program_options;
  
  int sleep_tm = 0;
  std::string path;
  std::unique_ptr<SharedSegment> pshm;
  std::unique_ptr<SyncObjPtr>    ppobj;
  
  po::options_description desc("Allowed options");
  desc.add_options()
    ("help", "describe arguments")
    ("list", "list variables in Neggia-Synchronization shared memory")
    ("remove", "remove Neggia-Synchronization shared memory and mutex")
    ("sleep", po::value<int>(&sleep_tm), "sleep time in seconds before exit")
    ("add", po::value<std::string>(&path), "add record arg=/file:/entry");
  
  po::variables_map vm;
  po::store(po::parse_command_line(argc, argv, desc), vm);
  po::notify(vm);
  
  if(vm.count("help")) {
    std::cout << boost::format("%s [options] [args]\n") % argv[0];
    std::cout << boost::format("Usage: %s -l\n") % argv[0];
    std::cout << boost::format("Copyright (c) 2017  MAX IV Laboratory, Lund University, distributed under the MIT License\n");
    std::cout << desc << "\n";
    return 0;
  }

  if(vm.count("remove")) {
    std::cout << "Removing Neggia-Synchronization shared memory and mutex\n" << std::flush;
    bip::shared_memory_object::remove(neggia_shm_id);
    bip::named_recursive_mutex::remove((std::string(neggia_shm_id)+std::string("_mutex")).c_str());
  }
 
  if(vm.count("list")) {
    std::cout << Utils::put_now() << "\n" << put_NegiaSyncObjShmInfo();
  }

  if(vm.count("add")) {
    std::cout << Utils::put_now() << boost::format(" Adding: %s\n") % path;
    // split to filepath and dsetpath
    std::vector<std::string> strs;
    boost::split(strs, path, boost::is_any_of(":"));
    if(strs.size()!=2) {
      std::cerr << boost::format("Error: Full path %s is not of a form 'filepath:dsetpath'\n") % path;
      return -1;
    }
    pshm.reset( new SharedSegment(neggia_shm_id) );
    ppobj.reset( new SyncObjPtr(Factory::find_or_create_dset(*pshm.get(), strs[0], strs[1])) );
  }
   
  if(vm.count("sleep")) {
    std::cout << boost::format("Sleeping for %d seconds ...\n") % sleep_tm << std::flush;
    std::this_thread::sleep_for(std::chrono::seconds(sleep_tm));
  }

  return 0;
}
