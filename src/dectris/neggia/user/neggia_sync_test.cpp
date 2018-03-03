#include <iostream>
#include <sstream>
#include <iomanip>
#include <map>

#include "H5File.h"
#include "Dataset.h"

#include <chrono>
#include <boost/crc.hpp>
#include <boost/algorithm/string.hpp>
#include <boost/format.hpp>
#include <boost/program_options.hpp>

#include <dectris/neggia/data/Decode.h>

#include <utility>

#include <thread>

using namespace std;

bool skipDecompression = false;
bool g_syncLockEnabled = true;

double _time_reading1(const Dataset & dset, unsigned int * img)
{ 
  vector<size_t> chunkOffset(3,0);
  
  auto t1 = std::chrono::high_resolution_clock::now();

  for(size_t i=0; i<dset.dim()[0]; i++) {
    chunkOffset[0] = i;
    dset.read(img, chunkOffset);
  }

  auto t2 = std::chrono::high_resolution_clock::now();
  
  std::chrono::duration<double, std::milli> fp_ms = t2 - t1;
  
  return fp_ms.count();
}

struct ChunkIndexCacheData {
    const H5File  * h5f_pointer;
    std::shared_ptr<const Dataset> dset_pointer;
    vector< pair<const char *,size_t> > rawDataPointers;
};

pair<const char*, size_t> DatasetGetRawDataPointer(const Dataset & dset, const std::vector<size_t> &chunkOffset) {
  auto rawDataPointer = dset.getRawData(chunkOffset);
  return make_pair(rawDataPointer.data, rawDataPointer.size);
}

void DatasetReadRawBitshuffleData(const Dataset & dset, const std::pair<const char*, size_t> p, void *data, size_t s)
{
  Dataset::ConstDataPointer rawData;
  rawData.data = p.first;
  rawData.size = p.second;

  const size_t transf_sz = 1024*1024;

  //s = dset.getSizeOfOutData();
  
  //cout << dset._filterCdValues[2] << "\n";
  
  //dset.readBitshuffleData(rawData, data, s);
  
  // make a local data copy
  //char * raw_data = new char[rawData.size];
  void * raw_data = NULL;
  posix_memalign(&raw_data, sizeof(void*), (rawData.size>transf_sz) ? rawData.size : transf_sz);

  //Synchronization::atomic_lock & mtx = dset._ptrSyncObj.get()->get()->ptr->mtx;
  Synchronization::atomic_lock & mtx = dset._ptrSyncObj.get()->ptr->mtx;
  
  size_t ntransf =  (rawData.size>transf_sz) ? (rawData.size+transf_sz-1)/transf_sz : 1;
  size_t sz = (rawData.size>transf_sz) ? rawData.size : transf_sz;
  size_t dsetRelEnd = (dset._dsetStart + dset._dsetSize) - (char*)rawData.data;
  //std::cout << rawData.size << "," << transf_sz << "," << ntransf << "," << dsetRelEnd << "\n";

  for(size_t i=0; i<ntransf; i++) {
    if (g_syncLockEnabled)
      pthread_spin_lock( &mtx );
    size_t transf_start = i*transf_sz;
    size_t transf_end = (sz<transf_sz) ? (transf_start+sz) : (transf_start+transf_sz);
    if (transf_end>dsetRelEnd) transf_end = dsetRelEnd;
    //std::cout << sz << ", " << transf_end-transf_start << "\n";
    memcpy((char*)raw_data+i*transf_sz, (char*)rawData.data+transf_start, transf_end-transf_start);
    if (g_syncLockEnabled)
      pthread_spin_unlock( &mtx );
    sz -=  transf_end-transf_start;
  }
  //cout << rawData.size << "\n";

  /*if(sz>transf_sz) {
    pthread_spin_lock( &mtx );
    memcpy(raw_data, rawData.data, transf_sz);
    pthread_spin_unlock( &mtx );
    sz -= transf_sz;
    memcpy((char*)raw_data+transf_sz, (char*)rawData.data+transf_sz, sz);
  } else {
    memcpy(raw_data, rawData.data, sz);
    }*/

  if (!skipDecompression)
	  bshufUncompressLz4((char*)raw_data,(char*)data,s,4);
  
  //delete[] raw_data;
  free((void*) raw_data);
  //cout << dset._filterId << " " << LZ4_FILTER << " " <<  BSHUF_H5FILTER << "\n"; 
  //dset.readLz4Data(rawData, data, s);
}

void openDatasetsAndBuildChunkIndexCache(vector< std::shared_ptr<Dataset> > & dsets,
		                         map<const string, ChunkIndexCacheData> & chunkIndexCache,
                                         const H5File * h5f_pointer,
                                         const vector< string > & dataset_names)
{
  for(size_t i=0; i<dataset_names.size(); i++) {
    std::shared_ptr<Dataset> pdset = std::make_shared<Dataset>(*h5f_pointer, dataset_names[i]);
    dsets.push_back( pdset );
    // build chunk index
    ChunkIndexCacheData & record = chunkIndexCache[dataset_names[i]];
    record.h5f_pointer = h5f_pointer;
    record.dset_pointer = pdset;
    record.rawDataPointers.resize(pdset->dim()[0]);
    vector<size_t> chunkOffset(3,0);
    for(size_t j=0; j<pdset->dim()[0]; j++) {
      chunkOffset[0] = j;
      record.rawDataPointers[j] = DatasetGetRawDataPointer( *pdset, chunkOffset );
    }
  }
}

class NeggiaReader {
  public:
    /// Set of filename:dataset:range 
    class Set {
    public:
      string filename;
      string dsetpath;
      int irange[2];
      /// Constructor
      Set(const string _filename, const string _dsetpath, int i0, int i1):
        filename(_filename), dsetpath(_dsetpath) {
        irange[0] = i0;
        irange[1] = i1;
      };
      /// Copy constructor
      Set(const Set &s):filename(s.filename),dsetpath(s.dsetpath) {
        memcpy(irange, s.irange, sizeof(int)*2);
      };
    };
  public:
    /// Constructor
    NeggiaReader();
    /// Constructor
    NeggiaReader(const string h5filename, const string dsetname, int i0, int i1);
    /// Configuration method (safe to call only once), returns nb of images to read
    int AddSet(const string h5filename, const string dsetname="/entry/data/data_000006",
	       int i0=0, int i1=-1);
    /// Destructor
    virtual ~ NeggiaReader();
    /// Set chunk index cache
    void SetChunkIndexCache(map<const string, ChunkIndexCacheData> * cache);
    /// Read all datasets
    int ReadAll();
    /// Make map of raw data offsets (index) amd read all datasets
    int ReadAllWithOffsetCache();
    /// C pointer to ReadAll
    static void* ReadAll_handle(void *context);
    /// C pointer to ReadAllWithOffsetCache
    static void* ReadAllWithOffsetCache_handle(void *context);
  protected:
    /// Set of file:dataset:range to read
    vector<Set> sets;
    /// image height
    size_t iheight;
    /// image width
    size_t iwidth;
	  /// single value size
    size_t dsize;
    /// number of images
    size_t dlen;
    /// image space
    unsigned int * img;
    /// pointer to chunk index cache (NULL if unavailable)
    map<const string, ChunkIndexCacheData> * pchunkIndexCache;
};

NeggiaReader::NeggiaReader():
  iheight(-1), iwidth(-1), img(NULL), pchunkIndexCache(NULL)
{}

NeggiaReader::NeggiaReader(const string h5filename, const string dsetname, int i0, int i1)
{
  AddSet(h5filename, dsetname, i0, i1);
}

NeggiaReader::~NeggiaReader()
{
  // cleanup
  delete[] img;
  img = NULL;
}

int NeggiaReader::AddSet(const string h5filename, const string dsetname, int i0, int i1)
{
  H5File h5f(h5filename);
  Dataset dset(h5f, dsetname);

  i1 = (i1>=0) ? i1 : dset.dim()[0];

  iheight = dset.dim()[1];
  iwidth = dset.dim()[2];
	dsize = dset.dataSize();
	dlen = dset.dim()[0];
  if (img==NULL)
    img = new unsigned int[iheight*iwidth];

  sets.push_back(Set(h5filename, dsetname, i0, i1));
 
  return (i1>i0) ? (i1-i0) : 0;
}
   
void NeggiaReader::SetChunkIndexCache(map<const string, ChunkIndexCacheData> * cache)
{
  pchunkIndexCache = cache;
}
  
int NeggiaReader::ReadAll()
{
  vector<size_t> chunkOffset(3,0);
  
  for(size_t iset=0; iset<sets.size(); iset++) {
    H5File h5f(sets[iset].filename);
    Dataset dset(h5f, sets[iset].dsetpath);
    
    // read data
    for(size_t i=sets[iset].irange[0]; i<sets[iset].irange[1]; i++) {
      chunkOffset[0] = i;
      dset.read(img, chunkOffset);
    }
  }

  return 0;
}

int NeggiaReader::ReadAllWithOffsetCache()
{
  // read all data
  for(size_t iset=0; iset<sets.size(); iset++) {

    if (sets[iset].irange[1]<=sets[iset].irange[0])
      continue;
    
    // get record from cache
    const ChunkIndexCacheData & record = pchunkIndexCache->at(sets[iset].dsetpath);
    size_t s = iwidth*iheight*dsize*dlen;
      
    // read data
    for(size_t i=sets[iset].irange[0]; i<sets[iset].irange[1]; i++) {
      //cout << boost::format("(%p,%ld)\n") % (void*)record.rawDataPointers[i].first % record.rawDataPointers[i].second;
      DatasetReadRawBitshuffleData( *(record.dset_pointer), record.rawDataPointers[i], img, s );
    }
  }

  return 0;
}

void* NeggiaReader::ReadAll_handle(void *context)
{
  ((NeggiaReader *)context)->ReadAll();
}

void* NeggiaReader::ReadAllWithOffsetCache_handle(void *context)
{
  ((NeggiaReader *)context)->ReadAllWithOffsetCache();
}

double _time_reading2(vector<NeggiaReader> & h5readers, int ntasks, pthread_t tasks[])
{   
  auto t1 = std::chrono::high_resolution_clock::now();

  for(size_t itask=0; itask<ntasks; itask++)
    pthread_create( &tasks[itask], NULL, &NeggiaReader::ReadAll_handle,
		    (void*) &h5readers[itask] );
  
  for(size_t itask=0; itask<ntasks; itask++)
      pthread_join( tasks[itask], NULL);
  
  auto t2 = std::chrono::high_resolution_clock::now();
  
  std::chrono::duration<double, std::milli> fp_ms = t2 - t1;
  
  return fp_ms.count();
}

double _time_reading3(vector<NeggiaReader> & h5readers, int ntasks, pthread_t tasks[])
{
  auto t1 = std::chrono::high_resolution_clock::now();

  for(size_t itask=0; itask<ntasks; itask++)
    pthread_create( &tasks[itask], NULL, &NeggiaReader::ReadAllWithOffsetCache_handle,
		    (void*) &h5readers[itask] );
  
  for(size_t itask=0; itask<ntasks; itask++)
      pthread_join( tasks[itask], NULL);
  
  auto t2 = std::chrono::high_resolution_clock::now();
  
  std::chrono::duration<double, std::milli> fp_ms = t2 - t1;
  
  return fp_ms.count();
}

int main(int argc, char* argv[]) {

  {
    const char* env_var = std::getenv("NEGGIA_SYNC_DISABLE");
    if( env_var && ((strlen(env_var)==1 && (*env_var=='1' || *env_var=='y' || *env_var=='Y')) ||
		    (strlen(env_var)==3 && boost::iequals(env_var,"yes"))) )
      g_syncLockEnabled = false;
  }
  
  namespace po = boost::program_options;
  po::options_description desc("Allowed options");
  desc.add_options()
    ("help", "describe arguments")
    ("doSingle", "Do single dataset reading test")
    ("skipCreateIndexCache", "Do not create internal HDF5 chunk index cache")
    ("skipDecompression", "Just read raw data, do not uncompress them")
    ("bypassMetaData", "Minimize access to datasets HDF5 metadata")
    ("disableSync", "Disable synchronization mechanism (same as NEGGIA_SYNC_DISABLE=1");

  po::variables_map vm;
  po::store(po::parse_command_line(argc, argv, desc), vm);
  po::notify(vm);   

	if (vm.count("help")) {
    cout << "testneg masterfile.h5 entry_fmt dataset_range ntasks ntasks_per_dataset [options]\n";
		cout << "Usage: ./testneg ../data/tau1-tau_2_master.h5 /entry/data/data_%06d 1:10 1 1\n";
  	cout << desc << "\n";
		return 1;
	}

  // get parameters
  if ( argc < 6 ) {
    cout << "Usage: ./neggia_sync_test tau1-tau_2_master.h5 /entry/data/data_%06d 1:10 1 1\n";
    return 1;
  }

  const char * filename = argv[1];
  boost::format dsetfmt(argv[2]);
  const char * dsetrange = argv[3];
  const int ntasks = atoi( argv[4] );
  const int ntasks_per_dset = atoi( argv[5] );
  bool skip_single = true;
	bool createIndexCache = true;
	bool bypassMetaData = false;
  skipDecompression = false;

	if (vm.count("doSingle"))
		skip_single = false;

	if (vm.count("skipCreateIndexCache"))
		createIndexCache = false;

	if (vm.count("skipDecompression"))
		skipDecompression = true;

	if (vm.count("bypassMetaData"))
		bypassMetaData = true;

	if (vm.count("disableSync"))
		g_syncLockEnabled = false;

	if( !createIndexCache )
		bypassMetaData = false;

  if( !createIndexCache || !bypassMetaData)
		skipDecompression = false;

	
  if (ntasks % ntasks_per_dset != 0) {
    cout << "total number of tasks must be divisible by tasks per dataset\n";
    return 0;
  }
  
  int idset0, idset1;
  {
    vector<string> strs;
    boost::split(strs, dsetrange, boost::is_any_of(":"));
    idset0 = atoi( strs[0].c_str() );
    idset1 = atoi( strs[1].c_str() );
  }
  vector<string> dataset_names(idset1-idset0);
  for(size_t i=idset0, j=0; i<idset1; i++, j++)
    dataset_names[j] = (dsetfmt % i).str();

  cout << "h5filename: " << filename << '\n';
  cout << "datasets:";
  for(size_t i=0; i< dataset_names.size(); i++)
    cout << ' ' << dataset_names[i];
  cout << '\n';

  if ((ntasks/ntasks_per_dset) > (idset1-idset0)) {
    cout << "(ntasks/ntasks_per_dset) can not be larger than nb of datasets\n";
    return 0;
  }
    
  // ---------------------------------------------------------------------------------

  // Want to have explicit control over H5File and Dataset objects n order to close
  // them and release associated filesystem and system cache 
  H5File  * h5f_pointer = new H5File(filename);
  Dataset * dset_pointer = new Dataset(*h5f_pointer, dataset_names[0]);
  
  const H5File  & h5f  = *h5f_pointer;
  const Dataset & dset = *dset_pointer;
  
  // print info
  cout << "*** Dataset info: ***\n";
  cout << "       entry: " << dataset_names[0].c_str() << '\n';
  cout << "dataTypeId(): " << dset.dataTypeId() << '\n';
  cout << "  dataSize(): " << dset.dataSize() << '\n';
  cout << "  isSigned(): " << dset.isSigned() << '\n';
  cout << "       dim():";
  vector<size_t> v = dset.dim();
  for (vector<size_t>::const_iterator i = v.begin(); i != v.end(); ++i)
    cout << ' ' << *i;
  cout << '\n';
  cout << " isChunked(): " << dset.isChunked() << '\n';
  cout << " chunkSize():";
  v = dset.chunkSize();
  for (vector<size_t>::const_iterator i = v.begin(); i != v.end(); ++i)
    cout << ' ' << *i;
  cout << '\n';

  // parameters
  const size_t iheight = dset.dim()[1];
  const size_t iwidth  = dset.dim()[2];
  const size_t dset_dim0 = dset.dim()[0];
  
  // get data
  unsigned int * img = new unsigned int[iheight*iwidth];
  vector<size_t> chunkOffset(3,0);

  if (!skip_single) {
    chunkOffset[0] = 40;

    cout << "*** Trying to read data: ***\n";
    dset.read(img, chunkOffset);

    cout << "( 0, 0): " << img[0] << '\n';
    cout << "(-1,-1): " << img[iheight*iwidth-1+1000] << '\n';

    cout << "*** Printing part of the matrix: ***\n";
    cout << "(40,1935:1945,2430:2435)" << '\n';
    for(size_t i=1935; i<=1945; i++) {
      for(size_t j=2430; j<=2435; j++)
	cout << ' ' << setw(4) << img[i*iwidth+j];
        cout << '\n';
    }

    // calculate crc
    boost::crc_32_type crc;
    crc.process_bytes( (char*)img, sizeof(unsigned int)*iheight*iwidth);
    cout << "crc32.checksum: " << std::hex << crc.checksum() << '\n';
  
    cout << flush;
  
    cout << "*** Reading whole single dataset ... ***\n";
    for(int i=0; i<5; i++) {
      double t = _time_reading1(dset, img);
      cout << "Reading (" << i << ") took: " << fixed << setprecision(2) << t << " ms, ";
      cout << sizeof(unsigned int)*iheight*iwidth*dset.dim()[0]/(t/1.e3)/1.e9 << " pk GB/s\n";
    }
  } // if(!skip_single)

    // explicitely close dset (usage of its reference is from now an error)
  delete dset_pointer;
  dset_pointer = NULL;

  cout << "*** Test info: ***\n";
  cout << " createIndexCache: " << createIndexCache << '\n';
  cout << "skipDecompression: " << skipDecompression << '\n';
  cout << "   bypassMetaData: " << skipDecompression << '\n';
  cout << "      disableSync: " << !g_syncLockEnabled << '\n';
  
  // ----------------------------------------------------------------------------
  // build chunk offsets (chunk index) metadata cache
  map<const string, ChunkIndexCacheData> chunkIndexCache;
  // vector of all relevant datasets (we keep them open until end)
  vector< std::shared_ptr<Dataset> > dsets(0);
  
  if (createIndexCache) {
    auto t1 = std::chrono::high_resolution_clock::now();

    openDatasetsAndBuildChunkIndexCache(dsets, chunkIndexCache, h5f_pointer, dataset_names);

    auto t2 = std::chrono::high_resolution_clock::now();
    std::chrono::duration<double, std::milli> fp_ms = t2 - t1;
  
    cout << "Building chunk index cache took: " << fixed << setprecision(2) << fp_ms.count() << " ms\n";
  }
  // ----------------------------------------------------------------------------
  // read mutiple datasets
  int ntotalsets = (dataset_names.size()<=ntasks/ntasks_per_dset) ? ntasks : dataset_names.size()*ntasks_per_dset;
  
  // distribute tasks
  vector<NeggiaReader> h5readers(ntasks);
  pthread_t tasks[ntasks];

  size_t rangelen = dset_dim0/ntasks_per_dset +
    ((dset_dim0 % ntasks_per_dset != 0) ? 1 : 0);

  size_t nb_images = 0;
  size_t nb_raw_bytes = 0;
  
  for(size_t ireader=0; ireader<h5readers.size(); ireader++)
    h5readers[ireader].SetChunkIndexCache( &chunkIndexCache );
  
  for(size_t iset=0; iset<ntotalsets; iset++) {
    size_t idset = iset / ntasks_per_dset;
    size_t irange = iset % ntasks_per_dset;
    size_t irange1 = ((irange+1)*rangelen<=dset_dim0) ? (irange+1)*rangelen : dset_dim0;
    nb_images += h5readers[iset % ntasks].AddSet(filename,
						                                     dataset_names[idset],
						                                     irange*rangelen, irange1);
    // calculate raw data size
    if (createIndexCache)
      for (size_t i=irange*rangelen; i<irange1; i++)
	nb_raw_bytes += chunkIndexCache[dataset_names[idset]].rawDataPointers[i].second;
    cout << "Mapping set id=" << iset << " to task id=" << iset % ntasks;
    cout << ", dset=" << dataset_names[idset];
    cout << ", range=" << irange*rangelen << ":" << irange1 << "\n";
  }
  
  cout << "*** Reading multiple datasets ... ***\n";
  for(int i=0; i<5; i++) {
      double t;
			if (bypassMetaData)
				t = _time_reading3(h5readers, ntasks, tasks);
			else
				t = _time_reading2(h5readers, ntasks, tasks);
      cout << "Reading (" << i << ") took: " << fixed << setprecision(2) << t << " ms, ";
      cout << sizeof(unsigned int)*nb_images*iheight*iwidth/(t/1.e3)/1.e9;
      cout << " pk GB/s, " << setprecision(1) << nb_raw_bytes/(t/1.e3)/1.e6;
      cout << " MB/s, ntasks: " << ntasks << "\n";
  }
  
  // cleanup
  delete[] img;
  
  delete h5f_pointer;
  h5f_pointer = NULL;
    
  return 0;
}
