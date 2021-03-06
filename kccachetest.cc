/*************************************************************************************************
 * The test cases of the cache hash database
 *                                                               Copyright (C) 2009-2012 FAL Labs
 * This file is part of Kyoto Cabinet.
 * This program is free software: you can redistribute it and/or modify it under the terms of
 * the GNU General Public License as published by the Free Software Foundation, either version
 * 3 of the License, or any later version.
 * This program is distributed in the hope that it will be useful, but WITHOUT ANY WARRANTY;
 * without even the implied warranty of MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.
 * See the GNU General Public License for more details.
 * You should have received a copy of the GNU General Public License along with this program.
 * If not, see <http://www.gnu.org/licenses/>.
 *************************************************************************************************/


#include <kccachedb.h>
#include "cmdcommon.h"
#include <atomic>
#include <string>
#include <unistd.h>
#include <stdlib.h>
#include <iostream>
 #include <pthread.h>
#include <sched.h>

static const char * method = METHOD;
static const char * algo = nullptr;

// global variables
const char* g_progname;                  // program name
uint32_t g_randseed;                     // random seed
int64_t g_memusage;                      // memory usage


int MarsagliaXOR(int *p_seed) {
    int seed = *p_seed;

    if (seed == 0) {
        seed = 1;
    }

    seed ^= seed << 6;
    seed ^= ((unsigned)seed) >> 21;
    seed ^= seed << 7;

    *p_seed = seed;

    return seed & 0x7FFFFFFF;
}

static const int loaderThreads = LOADERS;

int myrandmarsaglia(int range, int * seedp) {
  return MarsagliaXOR(seedp) % range;
}

// function prototypes
int main(int argc, char** argv);
static void usage();
static void dberrprint(kc::BasicDB* db, int32_t line, const char* func);
static void dbmetaprint(kc::BasicDB* db, bool verbose);
static int32_t runorder(int argc, char** argv);
static int32_t runqueue(int argc, char** argv);
static int32_t runwicked(int argc, char** argv);
static int32_t runtran(int argc, char** argv);
static int32_t procorder(int64_t rnum, int32_t thnum, bool rnd, bool etc, bool tran,
                         int32_t opts, int64_t bnum, int64_t capcnt, int64_t capsiz, bool lv);
static int32_t procqueue(int64_t rnum, int32_t thnum, int32_t itnum, bool rnd,
                         int32_t opts, int64_t bnum, int64_t capcnt, int64_t capsiz, bool lv);
static int32_t procwicked(int64_t rnum, int32_t thnum, int32_t itnum,
                          int32_t opts, int64_t bnum, int64_t capcnt, int64_t capsiz, bool lv);
static int32_t proctran(int64_t rnum, int32_t thnum, int32_t itnum,
                        int32_t opts, int64_t bnum, int64_t capcnt, int64_t capsiz, bool lv);

static void procsanity(int, int);

int staticrands[THREADMAX] = {
    0b011011100001111001010111110001,
    0b010000000111110111011001100111,
    0b110111001101101111110110101001,
    0b111011011010000111000100011001,
    0b100111100000001011100001110010,
    0b001011100000001001110111110101,
    0b110111000000001001000100001010,
    0b100000011010110101100101110100,
    0b110011001000010110001110100000,
    0b001010001010100110110110101000,
    0b011100111000110000000100111001,
    0b001000011011010000110100000000,
    0b110110110101100001100100111110,
    0b001010011011110100011011110101,
    0b010110111000010111010111100001,
    0b110100101110001100100010100011
};

#define OUTPUT(metric)\
  std::cout << #metric << ":"  << metric << std::endl;

struct OutputMetrics {
    size_t initial_count {};
    size_t final_count {};
    size_t initial_size {};
    size_t final_size {};
    double actual_time {};
    long read_attempts {};
    long read_success {};
    long add_attempts {};
    long add_success {};
    long remove_attempts {};
    long remove_success {};

    long opcount() {
      return read_attempts + add_attempts  + remove_attempts;
    }

    void merge (const OutputMetrics &other) {
      read_attempts += other.read_attempts;
      read_success += other.read_success;

      add_attempts += other.add_attempts;
      add_success += other.add_success;

      remove_attempts += other.remove_attempts;
      remove_success += other.remove_success;
    }

    void print() {
      long ops = opcount();
      long actual_pcntreads = ((float)read_attempts/ops) * 100;
      size_t initial_sizemb = initial_size >> 20;
      size_t final_sizemb = final_size >> 20;

      long read_successrate = ((float)read_success/read_attempts) * 100;
      long add_successrate = ((float)add_success/add_attempts) * 100;
      long remove_successrate = ((float)remove_success/remove_attempts) * 100;
      long actual_pcntremove = ((float)remove_attempts/ops) * 100;

      OUTPUT(initial_count);
      OUTPUT(final_count);
      OUTPUT(initial_size);
      OUTPUT(initial_sizemb);
      OUTPUT(final_size);
      OUTPUT(final_sizemb);
      OUTPUT(read_successrate);
      OUTPUT(add_successrate);
      OUTPUT(remove_successrate);
      printf("time:%.3f\n", actual_time); // formatting
      OUTPUT(ops);
      OUTPUT(actual_pcntreads);
      OUTPUT(actual_pcntremove);
    }
};

struct BenchParams {
public:
  int targetcnt() const {
    return targetcnt_;
  }

  size_t keysize() const {
    return kvsize_/2;
  }

  size_t valsize() const {
    return kvsize_/2;
  }

  int duration() const {
    return duration_;
  }

  int thnum() const {
    return thnum_;
  }

  bool rtt() const {
    return rtt_;
  }

  int keyrange() const {
    return targetcnt() * 2; // 50 percent chance of hitting a non-existing element
  }

  int readpercent() const {
    return readpercent_;
  }

  int reps() const {
    return reps_;
  }

  BenchParams() = default;
  BenchParams(size_t targetcnt, int thnum, size_t kvsize, int readpercent, int durations, bool rtt, int reps)
  : targetcnt_(targetcnt), thnum_(thnum), kvsize_(kvsize), readpercent_(readpercent), duration_(durations), rtt_(rtt), reps_(reps) {}

  void print() {
    OUTPUT(targetcnt_);
    OUTPUT(thnum_);
    OUTPUT(kvsize_);
    OUTPUT(readpercent_);
    OUTPUT(duration_);
    OUTPUT(rtt_);
  }

  size_t targetcnt_ = 0;
  int thnum_ = 0;
  size_t kvsize_ = 0; // keypair size target --> kvsize * cpcnt should be <= capsize
  int rnum_ = 0; // iterations
  int readpercent_ = 0; // between 0 and 100
  int duration_ = 0; // in seconds
  bool rtt_ = false;
  int reps_ = 1;
};


void set_key(char *keybuf, int range, int * seed) {
  sprintf(keybuf, "%d", myrandmarsaglia(range, seed));
  // make first few bytes be different by using the ascii rep instead of first few bits.
}


#define ERR(db)\
    do {\
      dberrprint(db, __LINE__, __FILE__);\
      } while (0)


static void loadbench(kc::CacheDB* db, struct BenchParams params, int seed) {
  using namespace std;

  char *keybuf = new char[params.keysize()];
  assert(params.keysize() >= 32);
  // Want max 2GB -> 100 bytes ->  max keys is 20 *(2**20) ~ 20 million, then use up to 40 million.
  // load keys.
  int share = params.targetcnt() / loaderThreads;
  int range = params.keyrange();

  assert(params.keysize() == params.valsize());
  for (int i = 0; i < share;) {
    set_key(keybuf, range, &seed);
    int ret = db->add(keybuf, params.keysize(), keybuf, params.valsize());
    if (ret) {
      //assert(db->error().code() == kc::BasicDB::Error::SUCCESS);
      ++i;
    } else if (db->error().code() == kc::BasicDB::Error::DUPREC){
      // try again
    } else {
      ERR(db);
      //printf("%s\n", kc::BasicDB::Error::codename(db->error().code()));
      //assert(false);
      abort();
    }
  }
}

static void runbench(kc::CacheDB* db, struct BenchParams params, int seed, std::atomic<int> * fl, OutputMetrics * out)
{
    using Error = kc::BasicDB::Error;
    using namespace std;

    char * keybuf = new char[params.keysize()];
    char * valbuf = new char[params.valsize()];
    memset(valbuf, '\0', params.valsize());
    memset(keybuf, '\0', params.keysize()); // init the buffer once.

    int iters = 0;
    const int period = 50;

    while (iters % period != 0 || fl->load() != 1) { // check flag every period

      ++iters;
      set_key(keybuf, params.keyrange(), &seed);
      //printf("%s\n", keybuf);

      if (myrandmarsaglia(100, &seed) <= params.readpercent()) { // do a read depending on readpercent
        auto r = db->get(keybuf, params.keysize(), valbuf, params.valsize());
        out->read_attempts++;
        out->read_success += (r >=0);
        if (r < 0 && db->error() != Error::NOREC) {
          ERR(db);
          abort();
        }
      } else if (myrandmarsaglia(2, &seed) == 1) { // do an insert or delete otherwise
        auto r = db->set(keybuf, params.keysize(), valbuf, params.valsize());
        out->add_attempts++;
        out->add_success += (!!r);
        if (!r && db->error() != Error::DUPREC) {
          ERR(db);
          abort();
        }
      } else {
        auto r = db->remove(keybuf, params.keysize());
        out->remove_attempts++;
        out->remove_success += (!!r);
        if (!r && db->error() != Error::NOREC) {
          ERR(db);
          abort();
        }
      }

    }

    assert(fl->load() == 1);
    assert(params.keysize() >= 32);
    // Want max 2GB -> 100 bytes ->  max keys is 20 *(2**20) ~ 20 million, then use up to 40 million
}


static void procbench(BenchParams params) {

  using namespace std;

  kc::CacheDB db;
  db.switch_rotation(params.rtt());
  uint32_t omode = kc::CacheDB::OWRITER | kc::CacheDB::OCREATE;
  int ropen = db.open("*", omode);
  myassert(ropen);

  class ThreadLoader : public kc::Thread {
  public:
    void setparams(kc::CacheDB *db, BenchParams params, int seed) {
      db_ = db;
      params_ = params;
      seed_ = seed;
    }

    void run()  {
      assert(db_);
      loadbench(db_, params_, seed_);
    }

  private:
    kc::CacheDB* db_ = nullptr;
    struct BenchParams params_;
    int seed_;
  };

  ThreadLoader lthreads[THREADMAX];

  for (int32_t i = 0; i < loaderThreads; i++) {
    lthreads[i].setparams(&db, params, i);
  }

  double start_loading = kc::time();
  for (int32_t i = 0; i < loaderThreads; i++) {
    lthreads[i].start();
  }

  for (int32_t i = 0; i < loaderThreads; i++) {
    lthreads[i].join();
  }

  double end_loading = kc::time();
  printf("load_time:%.03f\n", end_loading - start_loading);

  class ThreadBench : public kc::Thread {
  public:
    void setparams(kc::CacheDB *db, BenchParams params, int seed, int no) {
      db_ = db;
      params_ = params;
      seed_ = seed;
      no_ = no;
    }

    void run() {

      cpu_set_t cpuset;
      pthread_t thread = pthread_self();

      CPU_ZERO(&cpuset);

      {
        int start = 0;
        int end = 0;
        if (no_ <= 7){
          start = 0;
          end = 8;
        } else {
          start = 8;
          end = 16;
        }

        for (int j = start; j < end; j++) CPU_SET(j, &cpuset);
      }

      int s = pthread_setaffinity_np(thread, sizeof(cpu_set_t), &cpuset);

      if (s != 0){
        abort();
      }

      assert(db_);
      runbench(db_, params_, seed_, &flag_, &output_);

    }

    void setFlag() { // to communicate end of time period.
      flag_ = 1;
    }

    OutputMetrics get_output(){ // only call after join
      return output_;
    }

  private:
    kc::CacheDB* db_ = nullptr;
    struct BenchParams params_;
    int seed_;
    atomic<int> flag_ {0}; // used to signal end
    OutputMetrics output_ {};
    int no_ {};
  };

  const int maxth = params.thnum();

  for (int r = 0; r < params.reps(); ++r){
  for (int thnum = 1; thnum <= maxth; ++thnum) {
  BenchParams thisroundparams = params;
  thisroundparams.thnum_ = thnum;

  ThreadBench threads[THREADMAX];
  int bench_seed = 0b001001010000110101101101110001; // to make benchtime seed different from load time

  OutputMetrics output;
  output.initial_count = db.count();
  output.initial_size = db.size();

  for (int32_t i = 0; i < thnum; i++) {
    threads[i].setparams(&db, thisroundparams, bench_seed ^ staticrands[i], i);
  }

  double start = kc::time();
  for (int32_t i = 0; i < thnum; i++) {
    threads[i].start();
  }

  unsigned int sleept = thisroundparams.duration();
  assert(sleept > 0);
  while (sleept > 0) {
    sleept = sleep(sleept);
  }

  for (int32_t i = 0; i < thnum; i++) {
    threads[i].setFlag();
  }

  for (int32_t i = 0; i < thnum; i++) {
    threads[i].join();
  }

  double end = kc::time(); // call time before anything else
  output.final_size = db.size();  //size() and count() must be called before close.
  output.final_count = db.count();
  output.actual_time = end - start; // also

  uint64_t bnum_used = db.bnum_used();
  uint64_t bnum_total = db.bnum_total();

  //int rclose = db.close();
  //myassert(rclose);

  for (int32_t i = 0; i < thnum; i++) {
    output.merge(threads[i].get_output());
  }

  double throughput  = ((double)output.opcount()/output.actual_time);

  // report
  thisroundparams.print();
  output.print();
  printf("throughput:%.3f\n", throughput);
  OUTPUT(bnum_total);
  OUTPUT(bnum_used);
  float bnum_occupancy = ((double)bnum_used/bnum_total);
  printf("bnum_occupancy:%.3f\n", bnum_occupancy);
  float load_ratio = ((double)output.final_count/bnum_used);
  printf("load_ratio:%.3f\n", load_ratio);
  printf("algo:%s\n", algo);
  cout.flush();
  }
  //pthread_exit(0);
}
}



// parse arguments of order command
BenchParams parsebench(int argc, char** argv) {
  struct BenchParams ans;

  //defaults
  int thnum = 1;
  int targetcnt = 100000;
  size_t kvsize = 100;
  // => expected default size 2 * 100 * 1million = 20 MB
  int readpcnt = 90;
  int durations = 5;
  int reps = 1;
  bool rtt = false;
  for (int32_t i = 2; i < argc; i++) {
    if (argv[i][0] == '-') {
      if (!std::strcmp(argv[i], "-th")) {
        if (++i >= argc) usage();
        thnum = kc::atoix(argv[i]);
      } else if (!std::strcmp(argv[i], "-targetcnt")) { //capcnt, capsize must be set relative to targetcnt and kvsize
        if (++i >= argc) usage();
        targetcnt= kc::atoix(argv[i]);
      } else if (!std::strcmp(argv[i], "-kvsize")) {
        if (++i >= argc) usage();
        kvsize = kc::atoix(argv[i]);
      } else if (!std::strcmp(argv[i], "-readpcnt")) { // 0 to 100
        if (++i >= argc) usage();
        readpcnt = kc::atoix(argv[i]);
      } else if (!std::strcmp(argv[i], "-durations")) { //seconds
        if (++i >= argc) usage();
        durations = kc::atoix(argv[i]);
      } else if (!std::strcmp(argv[i], "-rtt")) { //rtt flag
        rtt = true;
      } else if (!std::strcmp(argv[i], "-rep")) { //reps
        if (++i >= argc) usage();
        reps = kc::atoix(argv[i]);
      } else {
        printf("arg parse error\n");
        exit(1);
    }
  }}

  return BenchParams(targetcnt, thnum, kvsize, readpcnt, durations, rtt, reps);
}


// main routine
int main(int argc, char** argv) {
  algo = getenv("ITM_DEFAULT_METHOD");
  if (method == std::string("tm") && !algo){
    algo = "unknown";
  } else if (method != std::string("tm")){
    algo = method;
  }
  assert(algo); // at this point it must be set to something;
  printf("algo is %s\n", algo);
  g_progname = argv[0];
  const char* ebuf = kc::getenv("KCRNDSEED");
  g_randseed = ebuf ? (uint32_t)kc::atoi(ebuf) : (uint32_t)(kc::time() * 1000);
  mysrand(g_randseed);
  g_memusage = memusage();
  kc::setstdiobin();
  if (argc < 2) usage();
  int32_t rv = 0;
  if (!std::strcmp(argv[1], "order")) {
    rv = runorder(argc, argv);
  } else if (!std::strcmp(argv[1], "queue")) {
    rv = runqueue(argc, argv);
  } else if (!std::strcmp(argv[1], "wicked")) {
    rv = runwicked(argc, argv);
  } else if (!std::strcmp(argv[1], "tran")) {
    rv = runtran(argc, argv);
  } else if (!std::strcmp(argv[1], "sanity")){
    assert(argc == 4);
    procsanity(atoi(argv[2]), atoi(argv[3]));
  } else if (!std::strcmp(argv[1], "bench")){
    BenchParams p = parsebench(argc, argv);
    procbench(p);
  } else {
    usage();
  }
  if (rv != 0) {
    oprintf("FAILED: KCRNDSEED=%u PID=%ld", g_randseed, (long)kc::getpid());
    for (int32_t i = 0; i < argc; i++) {
      oprintf(" %s", argv[i]);
    }
    oprintf("\n\n");
  }
  return rv;
}


// print the usage and exit
static void usage() {
  eprintf("%s: test cases of the cache hash database of Kyoto Cabinet\n", g_progname);
  eprintf("\n");
  eprintf("usage:\n");
  eprintf("  %s order [-th num] [-rnd] [-etc] [-tran] [-tc] [-bnum num]"
          " [-capcnt num] [-capsiz num] [-lv] rnum\n", g_progname);
  eprintf("  %s queue [-th num] [-it num] [-rnd] [-tc] [-bnum num]"
          " [-capcnt num] [-capsiz num] [-lv] rnum\n", g_progname);
  eprintf("  %s wicked [-th num] [-it num] [-tc] [-bnum num]"
          " [-capcnt num] [-capsiz num] [-lv] rnum\n", g_progname);
  eprintf("  %s tran [-th num] [-it num] [-tc] [-bnum num]"
          " [-capcnt num] [-capsiz num] [-lv] rnum\n", g_progname);
  eprintf("\n");
  std::exit(1);
}


// print the error message of a database
static void dberrprint(kc::BasicDB* db, int32_t line, const char* func) {
  const kc::BasicDB::Error& err = db->error();
  oprintf("%s: %d: %s: %s: %d: %s: %s\n",
          g_progname, line, func, db->path().c_str(), err.code(), err.name(), err.message());
}


// print members of a database
static void dbmetaprint(kc::BasicDB* db, bool verbose) {
  if (verbose) {
    std::map<std::string, std::string> status;
    status["opaque"] = "";
    status["bnum_used"] = "";
    if (db->status(&status)) {
      uint32_t type = kc::atoi(status["type"].c_str());
      oprintf("type: %s (%s) (type=0x%02X)\n",
              kc::BasicDB::typecname(type), kc::BasicDB::typestring(type), type);
      uint32_t rtype = kc::atoi(status["realtype"].c_str());
      if (rtype > 0 && rtype != type)
        oprintf("real type: %s (%s) (realtype=0x%02X)\n",
                kc::BasicDB::typecname(rtype), kc::BasicDB::typestring(rtype), rtype);
      uint32_t chksum = kc::atoi(status["chksum"].c_str());
      oprintf("format version: %s (libver=%s.%s) (chksum=0x%02X)\n", status["fmtver"].c_str(),
              status["libver"].c_str(), status["librev"].c_str(), chksum);
      oprintf("path: %s\n", status["path"].c_str());
      int32_t flags = kc::atoi(status["flags"].c_str());
      oprintf("status flags:");
      if (flags & kc::CacheDB::FOPEN) oprintf(" open");
      if (flags & kc::CacheDB::FFATAL) oprintf(" fatal");
      oprintf(" (flags=%d)", flags);
      if (kc::atoi(status["recovered"].c_str()) > 0) oprintf(" (recovered)");
      if (kc::atoi(status["reorganized"].c_str()) > 0) oprintf(" (reorganized)");
      oprintf("\n", flags);
      int32_t opts = kc::atoi(status["opts"].c_str());
      oprintf("options:");
      if (opts & kc::CacheDB::TSMALL) oprintf(" small");
      if (opts & kc::CacheDB::TLINEAR) oprintf(" linear");
      if (opts & kc::CacheDB::TCOMPRESS) oprintf(" compress");
      oprintf(" (opts=%d)\n", opts);
      if (status["opaque"].size() >= 16) {
        const char* opaque = status["opaque"].c_str();
        oprintf("opaque:");
        if (std::count(opaque, opaque + 16, 0) != 16) {
          for (int32_t i = 0; i < 16; i++) {
            oprintf(" %02X", ((unsigned char*)opaque)[i]);
          }
        } else {
          oprintf(" 0");
        }
        oprintf("\n");
      }
      int64_t bnum = kc::atoi(status["bnum"].c_str());
      int64_t bnumused = kc::atoi(status["bnum_used"].c_str());
      int64_t count = kc::atoi(status["count"].c_str());
      double load = 0;
      if (count > 0 && bnumused > 0) {
        load = (double)count / bnumused;
        if (!(opts & kc::CacheDB::TLINEAR)) load = std::log(load + 1) / std::log(2.0);
      }
      oprintf("buckets: %lld (used=%lld) (load=%.2f)\n",
              (long long)bnum, (long long)bnumused, load);
      std::string cntstr = unitnumstr(count);
      int64_t capcnt = kc::atoi(status["capcnt"].c_str());
      oprintf("count: %lld (%s) (capcnt=%lld)\n", count, cntstr.c_str(), (long long)capcnt);
      int64_t size = kc::atoi(status["size"].c_str());
      std::string sizestr = unitnumstrbyte(size);
      int64_t capsiz = kc::atoi(status["capsiz"].c_str());
      oprintf("size: %lld (%s) (capsiz=%lld)\n", size, sizestr.c_str(), (long long)capsiz);
    }
  } else {
    oprintf("count: %lld\n", (long long)db->count());
    oprintf("size: %lld\n", (long long)db->size());
  }
  int64_t musage = memusage();
  if (musage > 0) oprintf("memory: %lld\n", (long long)(musage - g_memusage));
}


// parse arguments of order command
static int32_t runorder(int argc, char** argv) {
  bool argbrk = false;
  const char* rstr = NULL;
  int32_t thnum = 1;
  bool rnd = false;
  bool etc = false;
  bool tran = false;
  int32_t opts = 0;
  int64_t bnum = -1;
  int64_t capcnt = -1;
  int64_t capsiz = -1;
  bool lv = false;
  for (int32_t i = 2; i < argc; i++) {
    if (!argbrk && argv[i][0] == '-') {
      if (!std::strcmp(argv[i], "--")) {
        argbrk = true;
      } else if (!std::strcmp(argv[i], "-th")) {
        if (++i >= argc) usage();
        thnum = kc::atoix(argv[i]);
      } else if (!std::strcmp(argv[i], "-rnd")) {
        rnd = true;
      } else if (!std::strcmp(argv[i], "-etc")) {
        etc = true;
      } else if (!std::strcmp(argv[i], "-tran")) {
        tran = true;
      } else if (!std::strcmp(argv[i], "-tc")) {
        opts |= kc::CacheDB::TCOMPRESS;
      } else if (!std::strcmp(argv[i], "-bnum")) {
        if (++i >= argc) usage();
        bnum = kc::atoix(argv[i]);
      } else if (!std::strcmp(argv[i], "-capcnt")) {
        if (++i >= argc) usage();
        capcnt = kc::atoix(argv[i]);
      } else if (!std::strcmp(argv[i], "-capsiz")) {
        if (++i >= argc) usage();
        capsiz = kc::atoix(argv[i]);
      } else if (!std::strcmp(argv[i], "-lv")) {
        lv = true;
      } else {
        usage();
      }
    } else if (!rstr) {
      argbrk = true;
      rstr = argv[i];
    } else {
      usage();
    }
  }
  if (!rstr) usage();
  int64_t rnum = kc::atoix(rstr);
  if (rnum < 1 || thnum < 1) usage();
  if (thnum > THREADMAX) thnum = THREADMAX;
  int32_t rv = procorder(rnum, thnum, rnd, etc, tran, opts, bnum, capcnt, capsiz, lv);
  return rv;
}


// parse arguments of queue command
static int32_t runqueue(int argc, char** argv) {
  bool argbrk = false;
  const char* rstr = NULL;
  int32_t thnum = 1;
  int32_t itnum = 1;
  bool rnd = false;
  int32_t opts = 0;
  int64_t bnum = -1;
  int64_t capcnt = -1;
  int64_t capsiz = -1;
  bool lv = false;
  for (int32_t i = 2; i < argc; i++) {
    if (!argbrk && argv[i][0] == '-') {
      if (!std::strcmp(argv[i], "--")) {
        argbrk = true;
      } else if (!std::strcmp(argv[i], "-th")) {
        if (++i >= argc) usage();
        thnum = kc::atoix(argv[i]);
      } else if (!std::strcmp(argv[i], "-it")) {
        if (++i >= argc) usage();
        itnum = kc::atoix(argv[i]);
      } else if (!std::strcmp(argv[i], "-rnd")) {
        rnd = true;
      } else if (!std::strcmp(argv[i], "-tc")) {
        opts |= kc::CacheDB::TCOMPRESS;
      } else if (!std::strcmp(argv[i], "-bnum")) {
        if (++i >= argc) usage();
        bnum = kc::atoix(argv[i]);
      } else if (!std::strcmp(argv[i], "-capcnt")) {
        if (++i >= argc) usage();
        capcnt = kc::atoix(argv[i]);
      } else if (!std::strcmp(argv[i], "-capsiz")) {
        if (++i >= argc) usage();
        capsiz = kc::atoix(argv[i]);
      } else if (!std::strcmp(argv[i], "-lv")) {
        lv = true;
      } else {
        usage();
      }
    } else if (!rstr) {
      argbrk = true;
      rstr = argv[i];
    } else {
      usage();
    }
  }
  if (!rstr) usage();
  int64_t rnum = kc::atoix(rstr);
  if (rnum < 1 || thnum < 1 || itnum < 1) usage();
  if (thnum > THREADMAX) thnum = THREADMAX;
  int32_t rv = procqueue(rnum, thnum, itnum, rnd, opts, bnum, capcnt, capsiz, lv);
  return rv;
}


// parse arguments of wicked command
static int32_t runwicked(int argc, char** argv) {
  bool argbrk = false;
  const char* rstr = NULL;
  int32_t thnum = 1;
  int32_t itnum = 1;
  int32_t opts = 0;
  int64_t bnum = -1;
  int64_t capcnt = -1;
  int64_t capsiz = -1;
  bool lv = false;
  for (int32_t i = 2; i < argc; i++) {
    if (!argbrk && argv[i][0] == '-') {
      if (!std::strcmp(argv[i], "--")) {
        argbrk = true;
      } else if (!std::strcmp(argv[i], "-th")) {
        if (++i >= argc) usage();
        thnum = kc::atoix(argv[i]);
      } else if (!std::strcmp(argv[i], "-it")) {
        if (++i >= argc) usage();
        itnum = kc::atoix(argv[i]);
      } else if (!std::strcmp(argv[i], "-tc")) {
        opts |= kc::CacheDB::TCOMPRESS;
      } else if (!std::strcmp(argv[i], "-bnum")) {
        if (++i >= argc) usage();
        bnum = kc::atoix(argv[i]);
      } else if (!std::strcmp(argv[i], "-capcnt")) {
        if (++i >= argc) usage();
        capcnt = kc::atoix(argv[i]);
      } else if (!std::strcmp(argv[i], "-capsiz")) {
        if (++i >= argc) usage();
        capsiz = kc::atoix(argv[i]);
      } else if (!std::strcmp(argv[i], "-lv")) {
        lv = true;
      } else {
        usage();
      }
    } else if (!rstr) {
      argbrk = true;
      rstr = argv[i];
    } else {
      usage();
    }
  }
  if (!rstr) usage();
  int64_t rnum = kc::atoix(rstr);
  if (rnum < 1 || thnum < 1 || itnum < 1) usage();
  if (thnum > THREADMAX) thnum = THREADMAX;
  int32_t rv = procwicked(rnum, thnum, itnum, opts, bnum, capcnt, capsiz, lv);
  return rv;
}


// parse arguments of tran command
static int32_t runtran(int argc, char** argv) {
  bool argbrk = false;
  const char* rstr = NULL;
  int32_t thnum = 1;
  int32_t itnum = 1;
  int32_t opts = 0;
  int64_t bnum = -1;
  int64_t capcnt = -1;
  int64_t capsiz = -1;
  bool lv = false;
  for (int32_t i = 2; i < argc; i++) {
    if (!argbrk && argv[i][0] == '-') {
      if (!std::strcmp(argv[i], "--")) {
        argbrk = true;
      } else if (!std::strcmp(argv[i], "-th")) {
        if (++i >= argc) usage();
        thnum = kc::atoix(argv[i]);
      } else if (!std::strcmp(argv[i], "-it")) {
        if (++i >= argc) usage();
        itnum = kc::atoix(argv[i]);
      } else if (!std::strcmp(argv[i], "-tc")) {
        opts |= kc::CacheDB::TCOMPRESS;
      } else if (!std::strcmp(argv[i], "-bnum")) {
        if (++i >= argc) usage();
        bnum = kc::atoix(argv[i]);
      } else if (!std::strcmp(argv[i], "-capcnt")) {
        if (++i >= argc) usage();
        capcnt = kc::atoix(argv[i]);
      } else if (!std::strcmp(argv[i], "-capsiz")) {
        if (++i >= argc) usage();
        capsiz = kc::atoix(argv[i]);
      } else if (!std::strcmp(argv[i], "-lv")) {
        lv = true;
      } else {
        usage();
      }
    } else if (!rstr) {
      argbrk = true;
      rstr = argv[i];
    } else {
      usage();
    }
  }
  if (!rstr) usage();
  int64_t rnum = kc::atoix(rstr);
  if (rnum < 1 || thnum < 1 || itnum < 1) usage();
  if (thnum > THREADMAX) thnum = THREADMAX;
  int32_t rv = proctran(rnum, thnum, itnum, opts, bnum, capcnt, capsiz, lv);
  return rv;
}


// perform order command
static int32_t procorder(int64_t rnum, int32_t thnum, bool rnd, bool etc, bool tran,
                         int32_t opts, int64_t bnum, int64_t capcnt, int64_t capsiz, bool lv) {
  oprintf("<In-order Test>\n  seed=%u  rnum=%lld  thnum=%d  rnd=%d  etc=%d  tran=%d"
          "  opts=%d  bnum=%lld  capcnt=%lld  capsiz=%lld  lv=%d\n\n",
          g_randseed, (long long)rnum, thnum, rnd, etc, tran,
          opts, (long long)bnum, (long long)capcnt, (long long)capsiz, lv);
  bool err = false;
  kc::CacheDB db;
  oprintf("opening the database:\n");
  double stime = kc::time();
  db.tune_logger(stdlogger(g_progname, &std::cout),
                 lv ? kc::UINT32MAX : kc::BasicDB::Logger::WARN | kc::BasicDB::Logger::ERROR);
  if (opts > 0) db.tune_options(opts);
  if (bnum > 0) db.tune_buckets(bnum);
  if (capcnt > 0) db.cap_count(capcnt);
  if (capsiz > 0) db.cap_size(capsiz);
  if (!db.open("*", kc::CacheDB::OWRITER | kc::CacheDB::OCREATE | kc::CacheDB::OTRUNCATE)) {
    dberrprint(&db, __LINE__, "DB::open");
    err = true;
  }
  double etime = kc::time();
  dbmetaprint(&db, false);
  oprintf("time: %.3f\n", etime - stime);
  oprintf("setting records:\n");
  stime = kc::time();
  class ThreadSet : public kc::Thread {
   public:
    void setparams(int32_t id, kc::BasicDB* db, int64_t rnum, int32_t thnum,
                   bool rnd, bool tran) {
      id_ = id;
      db_ = db;
      rnum_ = rnum;
      thnum_ = thnum;
      err_ = false;
      rnd_ = rnd;
      tran_ = tran;
    }
    bool error() {
      return err_;
    }
    void run() {
      int64_t base = id_ * rnum_;
      int64_t range = rnum_ * thnum_;
      for (int64_t i = 1; !err_ && i <= rnum_; i++) {
        if (tran_ && !db_->begin_transaction(false)) {
          dberrprint(db_, __LINE__, "DB::begin_transaction");
          err_ = true;
        }
        char kbuf[RECBUFSIZ];
        size_t ksiz = std::sprintf(kbuf, "%08lld",
                                   (long long)(rnd_ ? myrand(range) + 1 : base + i));
        if (!db_->set(kbuf, ksiz, kbuf, ksiz)) {
          dberrprint(db_, __LINE__, "DB::set");
          err_ = true;
        }
        if (rnd_ && i % 8 == 0) {
          switch (myrand(8)) {
            case 0: {
              if (!db_->set(kbuf, ksiz, kbuf, ksiz)) {
                dberrprint(db_, __LINE__, "DB::set");
                err_ = true;
              }
              break;
            }
            case 1: {
              if (!db_->append(kbuf, ksiz, kbuf, ksiz)) {
                dberrprint(db_, __LINE__, "DB::append");
                err_ = true;
              }
              break;
            }
            case 2: {
              if (!db_->remove(kbuf, ksiz) && db_->error() != kc::BasicDB::Error::NOREC) {
                dberrprint(db_, __LINE__, "DB::remove");
                err_ = true;
              }
              break;
            }
            case 3: {
              kc::DB::Cursor* cur = db_->cursor();
              if (cur->jump(kbuf, ksiz)) {
                switch (myrand(8)) {
                  default: {
                    size_t rsiz;
                    char* rbuf = cur->get_key(&rsiz, myrand(10) == 0);
                    if (rbuf) {
                      delete[] rbuf;
                    } else if (db_->error() != kc::BasicDB::Error::NOREC) {
                      dberrprint(db_, __LINE__, "Cursor::get_key");
                      err_ = true;
                    }
                    break;
                  }
                  case 1: {
                    size_t rsiz;
                    char* rbuf = cur->get_value(&rsiz, myrand(10) == 0);
                    if (rbuf) {
                      delete[] rbuf;
                    } else if (db_->error() != kc::BasicDB::Error::NOREC) {
                      dberrprint(db_, __LINE__, "Cursor::get_value");
                      err_ = true;
                    }
                    break;
                  }
                  case 2: {
                    size_t rksiz;
                    const char* rvbuf;
                    size_t rvsiz;
                    char* rkbuf = cur->get(&rksiz, &rvbuf, &rvsiz, myrand(10) == 0);
                    if (rkbuf) {
                      delete[] rkbuf;
                    } else if (db_->error() != kc::BasicDB::Error::NOREC) {
                      dberrprint(db_, __LINE__, "Cursor::get");
                      err_ = true;
                    }
                    break;
                  }
                  case 3: {
                    std::string key, value;
                    if (!cur->get(&key, &value, myrand(10) == 0) &&
                        db_->error() != kc::BasicDB::Error::NOREC) {
                      dberrprint(db_, __LINE__, "Cursor::get");
                      err_ = true;
                    }
                    break;
                  }
                  case 4: {
                    if (myrand(8) == 0 && !cur->remove() &&
                        db_->error() != kc::BasicDB::Error::NOREC) {
                      dberrprint(db_, __LINE__, "Cursor::remove");
                      err_ = true;
                    }
                    break;
                  }
                }
              } else if (db_->error() != kc::BasicDB::Error::NOREC) {
                dberrprint(db_, __LINE__, "Cursor::jump");
                err_ = true;
              }
              delete cur;
              break;
            }
            default: {
              size_t vsiz;
              char* vbuf = db_->get(kbuf, ksiz, &vsiz);
              if (vbuf) {
                delete[] vbuf;
              } else if (db_->error() != kc::BasicDB::Error::NOREC) {
                dberrprint(db_, __LINE__, "DB::get");
                err_ = true;
              }
              break;
            }
          }
        }
        if (tran_ && !db_->end_transaction(true)) {
          dberrprint(db_, __LINE__, "DB::end_transaction");
          err_ = true;
        }
        if (id_ < 1 && rnum_ > 250 && i % (rnum_ / 250) == 0) {
          oputchar('.');
          if (i == rnum_ || i % (rnum_ / 10) == 0) oprintf(" (%08lld)\n", (long long)i);
        }
      }
    }
   private:
    int32_t id_;
    kc::BasicDB* db_;
    int64_t rnum_;
    int32_t thnum_;
    bool err_;
    bool rnd_;
    bool tran_;
  };
  ThreadSet threadsets[THREADMAX];
  if (thnum < 2) {
    threadsets[0].setparams(0, &db, rnum / thnum, thnum, rnd, tran);
    threadsets[0].run();
    if (threadsets[0].error()) err = true;
  } else {
    for (int32_t i = 0; i < thnum; i++) {
      threadsets[i].setparams(i, &db, rnum / thnum, thnum, rnd, tran);
      threadsets[i].start();
    }
    for (int32_t i = 0; i < thnum; i++) {
      threadsets[i].join();
      if (threadsets[i].error()) err = true;
    }
  }
  etime = kc::time();
  dbmetaprint(&db, false);
  oprintf("time: %.3f\n", etime - stime);
  if (etc) {
    oprintf("adding records:\n");
    stime = kc::time();
    class ThreadAdd : public kc::Thread {
     public:
      void setparams(int32_t id, kc::BasicDB* db, int64_t rnum, int32_t thnum,
                     bool rnd, bool tran) {
        id_ = id;
        db_ = db;
        rnum_ = rnum;
        thnum_ = thnum;
        err_ = false;
        rnd_ = rnd;
        tran_ = tran;
      }
      bool error() {
        return err_;
      }
      void run() {
        int64_t base = id_ * rnum_;
        int64_t range = rnum_ * thnum_;
        for (int64_t i = 1; !err_ && i <= rnum_; i++) {
          if (tran_ && !db_->begin_transaction(false)) {
            dberrprint(db_, __LINE__, "DB::begin_transaction");
            err_ = true;
          }
          char kbuf[RECBUFSIZ];
          size_t ksiz = std::sprintf(kbuf, "%08lld",
                                     (long long)(rnd_ ? myrand(range) + 1 : base + i));
          if (!db_->add(kbuf, ksiz, kbuf, ksiz) &&
              db_->error() != kc::BasicDB::Error::DUPREC) {
            dberrprint(db_, __LINE__, "DB::add");
            err_ = true;
          }
          if (tran_ && !db_->end_transaction(true)) {
            dberrprint(db_, __LINE__, "DB::end_transaction");
            err_ = true;
          }
          if (id_ < 1 && rnum_ > 250 && i % (rnum_ / 250) == 0) {
            oputchar('.');
            if (i == rnum_ || i % (rnum_ / 10) == 0) oprintf(" (%08lld)\n", (long long)i);
          }
        }
      }
     private:
      int32_t id_;
      kc::BasicDB* db_;
      int64_t rnum_;
      int32_t thnum_;
      bool err_;
      bool rnd_;
      bool tran_;
    };
    ThreadAdd threadadds[THREADMAX];
    if (thnum < 2) {
      threadadds[0].setparams(0, &db, rnum / thnum, thnum, rnd, tran);
      threadadds[0].run();
      if (threadadds[0].error()) err = true;
    } else {
      for (int32_t i = 0; i < thnum; i++) {
        threadadds[i].setparams(i, &db, rnum / thnum, thnum, rnd, tran);
        threadadds[i].start();
      }
      for (int32_t i = 0; i < thnum; i++) {
        threadadds[i].join();
        if (threadadds[i].error()) err = true;
      }
    }
    etime = kc::time();
    dbmetaprint(&db, false);
    oprintf("time: %.3f\n", etime - stime);
  }
  if (etc) {
    oprintf("appending records:\n");
    stime = kc::time();
    class ThreadAppend : public kc::Thread {
     public:
      void setparams(int32_t id, kc::BasicDB* db, int64_t rnum, int32_t thnum,
                     bool rnd, bool tran) {
        id_ = id;
        db_ = db;
        rnum_ = rnum;
        thnum_ = thnum;
        err_ = false;
        rnd_ = rnd;
        tran_ = tran;
      }
      bool error() {
        return err_;
      }
      void run() {
        int64_t base = id_ * rnum_;
        int64_t range = rnum_ * thnum_;
        for (int64_t i = 1; !err_ && i <= rnum_; i++) {
          if (tran_ && !db_->begin_transaction(false)) {
            dberrprint(db_, __LINE__, "DB::begin_transaction");
            err_ = true;
          }
          char kbuf[RECBUFSIZ];
          size_t ksiz = std::sprintf(kbuf, "%08lld",
                                     (long long)(rnd_ ? myrand(range) + 1 : base + i));
          if (!db_->append(kbuf, ksiz, kbuf, ksiz)) {
            dberrprint(db_, __LINE__, "DB::append");
            err_ = true;
          }
          if (tran_ && !db_->end_transaction(true)) {
            dberrprint(db_, __LINE__, "DB::end_transaction");
            err_ = true;
          }
          if (id_ < 1 && rnum_ > 250 && i % (rnum_ / 250) == 0) {
            oputchar('.');
            if (i == rnum_ || i % (rnum_ / 10) == 0) oprintf(" (%08lld)\n", (long long)i);
          }
        }
      }
     private:
      int32_t id_;
      kc::BasicDB* db_;
      int64_t rnum_;
      int32_t thnum_;
      bool err_;
      bool rnd_;
      bool tran_;
    };
    ThreadAppend threadappends[THREADMAX];
    if (thnum < 2) {
      threadappends[0].setparams(0, &db, rnum / thnum, thnum, rnd, tran);
      threadappends[0].run();
      if (threadappends[0].error()) err = true;
    } else {
      for (int32_t i = 0; i < thnum; i++) {
        threadappends[i].setparams(i, &db, rnum / thnum, thnum, rnd, tran);
        threadappends[i].start();
      }
      for (int32_t i = 0; i < thnum; i++) {
        threadappends[i].join();
        if (threadappends[i].error()) err = true;
      }
    }
    etime = kc::time();
    dbmetaprint(&db, false);
    oprintf("time: %.3f\n", etime - stime);
    char* opaque = db.opaque();
    if (opaque) {
      std::memcpy(opaque, "1234567890123456", 16);
      if (!db.synchronize_opaque()) {
        dberrprint(&db, __LINE__, "DB::synchronize_opaque");
        err = true;
      }
    } else {
      dberrprint(&db, __LINE__, "DB::opaque");
      err = true;
    }
  }
  oprintf("getting records:\n");
  stime = kc::time();
  class ThreadGet : public kc::Thread {
   public:
    void setparams(int32_t id, kc::BasicDB* db, int64_t rnum, int32_t thnum,
                   bool rnd, bool tran) {
      id_ = id;
      db_ = db;
      rnum_ = rnum;
      thnum_ = thnum;
      err_ = false;
      rnd_ = rnd;
      tran_ = tran;
    }
    bool error() {
      return err_;
    }
    void run() {
      int64_t base = id_ * rnum_;
      int64_t range = rnum_ * thnum_;
      for (int64_t i = 1; !err_ && i <= rnum_; i++) {
        if (tran_ && !db_->begin_transaction(false)) {
          dberrprint(db_, __LINE__, "DB::begin_transaction");
          err_ = true;
        }
        char kbuf[RECBUFSIZ];
        size_t ksiz = std::sprintf(kbuf, "%08lld",
                                   (long long)(rnd_ ? myrand(range) + 1 : base + i));
        size_t vsiz;
        char* vbuf = db_->get(kbuf, ksiz, &vsiz);
        if (vbuf) {
          if (vsiz < ksiz || std::memcmp(vbuf, kbuf, ksiz)) {
            dberrprint(db_, __LINE__, "DB::get");
            err_ = true;
          }
          delete[] vbuf;
        } else if (!rnd_ || db_->error() != kc::BasicDB::Error::NOREC) {
          dberrprint(db_, __LINE__, "DB::get");
          err_ = true;
        }
        if (rnd_ && i % 8 == 0) {
          switch (myrand(8)) {
            case 0: {
              if (!db_->set(kbuf, ksiz, kbuf, ksiz)) {
                dberrprint(db_, __LINE__, "DB::set");
                err_ = true;
              }
              break;
            }
            case 1: {
              if (!db_->append(kbuf, ksiz, kbuf, ksiz)) {
                dberrprint(db_, __LINE__, "DB::append");
                err_ = true;
              }
              break;
            }
            case 2: {
              if (!db_->remove(kbuf, ksiz) &&
                  db_->error() != kc::BasicDB::Error::NOREC) {
                dberrprint(db_, __LINE__, "DB::remove");
                err_ = true;
              }
              break;
            }
            case 3: {
              kc::DB::Cursor* cur = db_->cursor();
              if (cur->jump(kbuf, ksiz)) {
                switch (myrand(8)) {
                  default: {
                    size_t rsiz;
                    char* rbuf = cur->get_key(&rsiz, myrand(10) == 0);
                    if (rbuf) {
                      delete[] rbuf;
                    } else if (db_->error() != kc::BasicDB::Error::NOREC) {
                      dberrprint(db_, __LINE__, "Cursor::get_key");
                      err_ = true;
                    }
                    break;
                  }
                  case 1: {
                    size_t rsiz;
                    char* rbuf = cur->get_value(&rsiz, myrand(10) == 0);
                    if (rbuf) {
                      delete[] rbuf;
                    } else if (db_->error() != kc::BasicDB::Error::NOREC) {
                      dberrprint(db_, __LINE__, "Cursor::get_value");
                      err_ = true;
                    }
                    break;
                  }
                  case 2: {
                    size_t rksiz;
                    const char* rvbuf;
                    size_t rvsiz;
                    char* rkbuf = cur->get(&rksiz, &rvbuf, &rvsiz, myrand(10) == 0);
                    if (rkbuf) {
                      delete[] rkbuf;
                    } else if (db_->error() != kc::BasicDB::Error::NOREC) {
                      dberrprint(db_, __LINE__, "Cursor::get");
                      err_ = true;
                    }
                    break;
                  }
                  case 3: {
                    std::string key, value;
                    if (!cur->get(&key, &value, myrand(10) == 0) &&
                        db_->error() != kc::BasicDB::Error::NOREC) {
                      dberrprint(db_, __LINE__, "Cursor::get");
                      err_ = true;
                    }
                    break;
                  }
                  case 4: {
                    if (myrand(8) == 0 && !cur->remove() &&
                        db_->error() != kc::BasicDB::Error::NOREC) {
                      dberrprint(db_, __LINE__, "Cursor::remove");
                      err_ = true;
                    }
                    break;
                  }
                }
              } else if (db_->error() != kc::BasicDB::Error::NOREC) {
                dberrprint(db_, __LINE__, "Cursor::jump");
                err_ = true;
              }
              delete cur;
              break;
            }
            default: {
              size_t vsiz;
              char* vbuf = db_->get(kbuf, ksiz, &vsiz);
              if (vbuf) {
                delete[] vbuf;
              } else if (db_->error() != kc::BasicDB::Error::NOREC) {
                dberrprint(db_, __LINE__, "DB::get");
                err_ = true;
              }
              break;
            }
          }
        }
        if (tran_ && !db_->end_transaction(true)) {
          dberrprint(db_, __LINE__, "DB::end_transaction");
          err_ = true;
        }
        if (id_ < 1 && rnum_ > 250 && i % (rnum_ / 250) == 0) {
          oputchar('.');
          if (i == rnum_ || i % (rnum_ / 10) == 0) oprintf(" (%08lld)\n", (long long)i);
        }
      }
    }
   private:
    int32_t id_;
    kc::BasicDB* db_;
    int64_t rnum_;
    int32_t thnum_;
    bool err_;
    bool rnd_;
    bool tran_;
  };
  ThreadGet threadgets[THREADMAX];
  if (thnum < 2) {
    threadgets[0].setparams(0, &db, rnum / thnum, thnum, rnd, tran);
    threadgets[0].run();
    if (threadgets[0].error()) err = true;
  } else {
    for (int32_t i = 0; i < thnum; i++) {
      threadgets[i].setparams(i, &db, rnum / thnum, thnum, rnd, tran);
      threadgets[i].start();
    }
    for (int32_t i = 0; i < thnum; i++) {
      threadgets[i].join();
      if (threadgets[i].error()) err = true;
    }
  }
  etime = kc::time();
  dbmetaprint(&db, false);
  oprintf("time: %.3f\n", etime - stime);
  if (etc) {
    oprintf("getting records with a buffer:\n");
    stime = kc::time();
    class ThreadGetBuffer : public kc::Thread {
     public:
      void setparams(int32_t id, kc::BasicDB* db, int64_t rnum, int32_t thnum,
                     bool rnd, bool tran) {
        id_ = id;
        db_ = db;
        rnum_ = rnum;
        thnum_ = thnum;
        err_ = false;
        rnd_ = rnd;
        tran_ = tran;
      }
      bool error() {
        return err_;
      }
      void run() {
        int64_t base = id_ * rnum_;
        int64_t range = rnum_ * thnum_;
        for (int64_t i = 1; !err_ && i <= rnum_; i++) {
          if (tran_ && !db_->begin_transaction(false)) {
            dberrprint(db_, __LINE__, "DB::begin_transaction");
            err_ = true;
          }
          char kbuf[RECBUFSIZ];
          size_t ksiz = std::sprintf(kbuf, "%08lld",
                                     (long long)(rnd_ ? myrand(range) + 1 : base + i));
          char vbuf[RECBUFSIZ];
          int32_t vsiz = db_->get(kbuf, ksiz, vbuf, sizeof(vbuf));
          if (vsiz >= 0) {
            if (vsiz < (int32_t)ksiz || std::memcmp(vbuf, kbuf, ksiz)) {
              dberrprint(db_, __LINE__, "DB::get");
              err_ = true;
            }
          } else if (!rnd_ || db_->error() != kc::BasicDB::Error::NOREC) {
            dberrprint(db_, __LINE__, "DB::get");
            err_ = true;
          }
          if (tran_ && !db_->end_transaction(true)) {
            dberrprint(db_, __LINE__, "DB::end_transaction");
            err_ = true;
          }
          if (id_ < 1 && rnum_ > 250 && i % (rnum_ / 250) == 0) {
            oputchar('.');
            if (i == rnum_ || i % (rnum_ / 10) == 0) oprintf(" (%08lld)\n", (long long)i);
          }
        }
      }
     private:
      int32_t id_;
      kc::BasicDB* db_;
      int64_t rnum_;
      int32_t thnum_;
      bool err_;
      bool rnd_;
      bool tran_;
    };
    ThreadGetBuffer threadgetbuffers[THREADMAX];
    if (thnum < 2) {
      threadgetbuffers[0].setparams(0, &db, rnum / thnum, thnum, rnd, tran);
      threadgetbuffers[0].run();
      if (threadgetbuffers[0].error()) err = true;
    } else {
      for (int32_t i = 0; i < thnum; i++) {
        threadgetbuffers[i].setparams(i, &db, rnum / thnum, thnum, rnd, tran);
        threadgetbuffers[i].start();
      }
      for (int32_t i = 0; i < thnum; i++) {
        threadgetbuffers[i].join();
        if (threadgetbuffers[i].error()) err = true;
      }
    }
    etime = kc::time();
    dbmetaprint(&db, false);
    oprintf("time: %.3f\n", etime - stime);
  }
  if (etc) {
    oprintf("traversing the database by the inner iterator:\n");
    stime = kc::time();
    int64_t cnt = db.count();
    class VisitorIterator : public kc::DB::Visitor {
     public:
      explicit VisitorIterator(int64_t rnum, bool rnd) :
          rnum_(rnum), rnd_(rnd), cnt_(0), rbuf_() {
        std::memset(rbuf_, '+', sizeof(rbuf_));
      }
      int64_t cnt() {
        return cnt_;
      }
     private:
      const char* visit_full(const char* kbuf, size_t ksiz,
                             const char* vbuf, size_t vsiz, size_t* sp) {
//        cnt_++;
//        const char* rv = NOP;
//        switch (rnd_ ? myrand(7) : cnt_ % 7) {
//          case 0: {
//            rv = rbuf_;
//            *sp = rnd_ ? myrand(sizeof(rbuf_)) : sizeof(rbuf_) / (cnt_ % 5 + 1);
//            break;
//          }
//          case 1: {
//            rv = REMOVE;
//            break;
//          }
//        }
//        if (rnum_ > 250 && cnt_ % (rnum_ / 250) == 0) {
//          oputchar('.');
//          if (cnt_ == rnum_ || cnt_ % (rnum_ / 10) == 0) oprintf(" (%08lld)\n", (long long)cnt_);
//        }
//        return rv;
        assert(0);// FIXME
        return nullptr;
      }
      int64_t rnum_;
      bool rnd_;
      int64_t cnt_;
      char rbuf_[RECBUFSIZ];
    } visitoriterator(rnum, rnd);
    if (tran && !db.begin_transaction(false)) {
      dberrprint(&db, __LINE__, "DB::begin_transaction");
      err = true;
    }
    if (!db.iterate(&visitoriterator, true)) {
      dberrprint(&db, __LINE__, "DB::iterate");
      err = true;
    }
    if (rnd) oprintf(" (end)\n");
    if (tran && !db.end_transaction(true)) {
      dberrprint(&db, __LINE__, "DB::end_transaction");
      err = true;
    }
    if (visitoriterator.cnt() != cnt) {
      dberrprint(&db, __LINE__, "DB::iterate");
      err = true;
    }
    etime = kc::time();
    dbmetaprint(&db, false);
    oprintf("time: %.3f\n", etime - stime);
  }
  if (etc) {
    oprintf("traversing the database by the outer cursor:\n");
    stime = kc::time();
    int64_t cnt = db.count();
    class VisitorCursor : public kc::DB::Visitor {
     public:
      explicit VisitorCursor(int64_t rnum, bool rnd) :
          rnum_(rnum), rnd_(rnd), cnt_(0), rbuf_() {
        std::memset(rbuf_, '-', sizeof(rbuf_));
      }
      int64_t cnt() {
        return cnt_;
      }
     private:
      const char* visit_full(const char* kbuf, size_t ksiz,
                             const char* vbuf, size_t vsiz, size_t* sp) {
//        cnt_++;
//        const char* rv = NOP;
//        switch (rnd_ ? myrand(7) : cnt_ % 7) {
//          case 0: {
//            rv = rbuf_;
//            *sp = rnd_ ? myrand(sizeof(rbuf_)) : sizeof(rbuf_) / (cnt_ % 5 + 1);
//            break;
//          }
//          case 1: {
//            rv = REMOVE;
//            break;
//          }
//        }
//        if (rnum_ > 250 && cnt_ % (rnum_ / 250) == 0) {
//          oputchar('.');
//          if (cnt_ == rnum_ || cnt_ % (rnum_ / 10) == 0) oprintf(" (%08lld)\n", (long long)cnt_);
//        }
//        return rv;
        assert(0); //FIXME
        return nullptr;
      }
      int64_t rnum_;
      bool rnd_;
      int64_t cnt_;
      char rbuf_[RECBUFSIZ];
    } visitorcursor(rnum, rnd);
    if (tran && !db.begin_transaction(false)) {
      dberrprint(&db, __LINE__, "DB::begin_transaction");
      err = true;
    }
    kc::CacheDB::Cursor cur(&db);
    if (!cur.jump() && db.error() != kc::BasicDB::Error::NOREC) {
      dberrprint(&db, __LINE__, "Cursor::jump");
      err = true;
    }
    kc::DB::Cursor* paracur = db.cursor();
    int64_t range = rnum * thnum;
    while (!err && cur.accept(&visitorcursor, true, !rnd)) {
      if (rnd) {
        char kbuf[RECBUFSIZ];
        size_t ksiz = std::sprintf(kbuf, "%08lld", (long long)myrand(range));
        switch (myrand(3)) {
          case 0: {
            if (!db.remove(kbuf, ksiz) && db.error() != kc::BasicDB::Error::NOREC) {
              dberrprint(&db, __LINE__, "DB::remove");
              err = true;
            }
            break;
          }
          case 1: {
            if (!paracur->jump(kbuf, ksiz) && db.error() != kc::BasicDB::Error::NOREC) {
              dberrprint(&db, __LINE__, "Cursor::jump");
              err = true;
            }
            break;
          }
          default: {
            if (!cur.step() && db.error() != kc::BasicDB::Error::NOREC) {
              dberrprint(&db, __LINE__, "Cursor::step");
              err = true;
            }
            break;
          }
        }
      }
    }
    if (db.error() != kc::BasicDB::Error::NOREC) {
      dberrprint(&db, __LINE__, "Cursor::accept");
      err = true;
    }
    oprintf(" (end)\n");
    delete paracur;
    if (tran && !db.end_transaction(true)) {
      dberrprint(&db, __LINE__, "DB::end_transaction");
      err = true;
    }
    if (!rnd && visitorcursor.cnt() != cnt) {
      dberrprint(&db, __LINE__, "Cursor::accept");
      err = true;
    }
    etime = kc::time();
    dbmetaprint(&db, false);
    oprintf("time: %.3f\n", etime - stime);
  }
  if (etc) {
    oprintf("synchronizing the database:\n");
    stime = kc::time();
    if (!db.synchronize(false, NULL)) {
      dberrprint(&db, __LINE__, "DB::synchronize");
      err = true;
    }
    class SyncProcessor : public kc::BasicDB::FileProcessor {
     public:
      explicit SyncProcessor(int64_t rnum, bool rnd, int64_t size) :
          rnum_(rnum), rnd_(rnd), size_(size) {}
     private:
      bool process(const std::string& path, int64_t count, int64_t size) {
        if (size != size_) return false;
        return true;
      }
      int64_t rnum_;
      bool rnd_;
      int64_t size_;
    } syncprocessor(rnum, rnd, db.size());
    if (!db.synchronize(false, &syncprocessor)) {
      dberrprint(&db, __LINE__, "DB::synchronize");
      err = true;
    }
    if (!db.occupy(rnd ? myrand(2) == 0 : true, &syncprocessor)) {
      dberrprint(&db, __LINE__, "DB::occupy");
      err = true;
    }
    etime = kc::time();
    dbmetaprint(&db, false);
    oprintf("time: %.3f\n", etime - stime);
  }
  if (etc && capcnt < 1 && capsiz < 1 && db.size() < (256LL << 20)) {
    oprintf("dumping records into snapshot:\n");
    stime = kc::time();
    std::ostringstream ostrm;
    if (!db.dump_snapshot(&ostrm)) {
      dberrprint(&db, __LINE__, "DB::dump_snapshot");
      err = true;
    }
    etime = kc::time();
    dbmetaprint(&db, false);
    oprintf("time: %.3f\n", etime - stime);
    oprintf("loading records from snapshot:\n");
    stime = kc::time();
    int64_t cnt = db.count();
    if (rnd && myrand(2) == 0 && !db.clear()) {
      dberrprint(&db, __LINE__, "DB::clear");
      err = true;
    }
    const std::string& str = ostrm.str();
    std::istringstream istrm(str);
    if (!db.load_snapshot(&istrm) || db.count() != cnt) {
      dberrprint(&db, __LINE__, "DB::load_snapshot");
      err = true;
    }
    etime = kc::time();
    dbmetaprint(&db, false);
    oprintf("time: %.3f\n", etime - stime);
  }
  oprintf("removing records:\n");
  stime = kc::time();
  class ThreadRemove : public kc::Thread {
   public:
    void setparams(int32_t id, kc::BasicDB* db, int64_t rnum, int32_t thnum,
                   bool rnd, bool etc, bool tran) {
      id_ = id;
      db_ = db;
      rnum_ = rnum;
      thnum_ = thnum;
      err_ = false;
      rnd_ = rnd;
      etc_ = etc;
      tran_ = tran;
    }
    bool error() {
      return err_;
    }
    void run() {
      int64_t base = id_ * rnum_;
      int64_t range = rnum_ * thnum_;
      for (int64_t i = 1; !err_ && i <= rnum_; i++) {
        if (tran_ && !db_->begin_transaction(false)) {
          dberrprint(db_, __LINE__, "DB::begin_transaction");
          err_ = true;
        }
        char kbuf[RECBUFSIZ];
        size_t ksiz = std::sprintf(kbuf, "%08lld",
                                   (long long)(rnd_ ? myrand(range) + 1 : base + i));
        if (!db_->remove(kbuf, ksiz) &&
            ((!rnd_ && !etc_) || db_->error() != kc::BasicDB::Error::NOREC)) {
          dberrprint(db_, __LINE__, "DB::remove");
          err_ = true;
        }
        if (rnd_ && i % 8 == 0) {
          switch (myrand(8)) {
            case 0: {
              if (!db_->set(kbuf, ksiz, kbuf, ksiz)) {
                dberrprint(db_, __LINE__, "DB::set");
                err_ = true;
              }
              break;
            }
            case 1: {
              if (!db_->append(kbuf, ksiz, kbuf, ksiz)) {
                dberrprint(db_, __LINE__, "DB::append");
                err_ = true;
              }
              break;
            }
            case 2: {
              if (!db_->remove(kbuf, ksiz) &&
                  db_->error() != kc::BasicDB::Error::NOREC) {
                dberrprint(db_, __LINE__, "DB::remove");
                err_ = true;
              }
              break;
            }
            case 3: {
              kc::DB::Cursor* cur = db_->cursor();
              if (cur->jump(kbuf, ksiz)) {
                switch (myrand(8)) {
                  default: {
                    size_t rsiz;
                    char* rbuf = cur->get_key(&rsiz, myrand(10) == 0);
                    if (rbuf) {
                      delete[] rbuf;
                    } else if (db_->error() != kc::BasicDB::Error::NOREC) {
                      dberrprint(db_, __LINE__, "Cursor::get_key");
                      err_ = true;
                    }
                    break;
                  }
                  case 1: {
                    size_t rsiz;
                    char* rbuf = cur->get_value(&rsiz, myrand(10) == 0);
                    if (rbuf) {
                      delete[] rbuf;
                    } else if (db_->error() != kc::BasicDB::Error::NOREC) {
                      dberrprint(db_, __LINE__, "Cursor::get_value");
                      err_ = true;
                    }
                    break;
                  }
                  case 2: {
                    size_t rksiz;
                    const char* rvbuf;
                    size_t rvsiz;
                    char* rkbuf = cur->get(&rksiz, &rvbuf, &rvsiz, myrand(10) == 0);
                    if (rkbuf) {
                      delete[] rkbuf;
                    } else if (db_->error() != kc::BasicDB::Error::NOREC) {
                      dberrprint(db_, __LINE__, "Cursor::get");
                      err_ = true;
                    }
                    break;
                  }
                  case 3: {
                    std::string key, value;
                    if (!cur->get(&key, &value, myrand(10) == 0) &&
                        db_->error() != kc::BasicDB::Error::NOREC) {
                      dberrprint(db_, __LINE__, "Cursor::get");
                      err_ = true;
                    }
                    break;
                  }
                  case 4: {
                    if (myrand(8) == 0 && !cur->remove() &&
                        db_->error() != kc::BasicDB::Error::NOREC) {
                      dberrprint(db_, __LINE__, "Cursor::remove");
                      err_ = true;
                    }
                    break;
                  }
                }
              } else if (db_->error() != kc::BasicDB::Error::NOREC) {
                dberrprint(db_, __LINE__, "Cursor::jump");
                err_ = true;
              }
              delete cur;
              break;
            }
            default: {
              size_t vsiz;
              char* vbuf = db_->get(kbuf, ksiz, &vsiz);
              if (vbuf) {
                delete[] vbuf;
              } else if (db_->error() != kc::BasicDB::Error::NOREC) {
                dberrprint(db_, __LINE__, "DB::get");
                err_ = true;
              }
              break;
            }
          }
        }
        if (tran_ && !db_->end_transaction(true)) {
          dberrprint(db_, __LINE__, "DB::end_transaction");
          err_ = true;
        }
        if (id_ < 1 && rnum_ > 250 && i % (rnum_ / 250) == 0) {
          oputchar('.');
          if (i == rnum_ || i % (rnum_ / 10) == 0) oprintf(" (%08lld)\n", (long long)i);
        }
      }
    }
   private:
    int32_t id_;
    kc::BasicDB* db_;
    int64_t rnum_;
    int32_t thnum_;
    bool err_;
    bool rnd_;
    bool etc_;
    bool tran_;
  };
  ThreadRemove threadremoves[THREADMAX];
  if (thnum < 2) {
    threadremoves[0].setparams(0, &db, rnum / thnum, thnum, rnd, etc, tran);
    threadremoves[0].run();
    if (threadremoves[0].error()) err = true;
  } else {
    for (int32_t i = 0; i < thnum; i++) {
      threadremoves[i].setparams(i, &db, rnum / thnum, thnum, rnd, etc, tran);
      threadremoves[i].start();
    }
    for (int32_t i = 0; i < thnum; i++) {
      threadremoves[i].join();
      if (threadremoves[i].error()) err = true;
    }
  }
  etime = kc::time();
  dbmetaprint(&db, true);
  oprintf("time: %.3f\n", etime - stime);
  oprintf("closing the database:\n");
  stime = kc::time();
  if (!db.close()) {
    dberrprint(&db, __LINE__, "DB::close");
    err = true;
  }
  etime = kc::time();
  oprintf("time: %.3f\n", etime - stime);
  oprintf("%s\n\n", err ? "error" : "ok");
  return err ? 1 : 0;
}


// perform queue command
static int32_t procqueue(int64_t rnum, int32_t thnum, int32_t itnum, bool rnd,
                         int32_t opts, int64_t bnum, int64_t capcnt, int64_t capsiz, bool lv) {
  oprintf("<Queue Test>\n  seed=%u  rnum=%lld  thnum=%d  itnum=%d  rnd=%d"
          "  opts=%d  bnum=%lld  capcnt=%lld  capsiz=%lld  lv=%d\n\n",
          g_randseed, (long long)rnum, thnum, itnum, rnd,
          opts, (long long)bnum, (long long)capcnt, (long long)capsiz, lv);
  bool err = false;
  kc::CacheDB db;
  db.tune_logger(stdlogger(g_progname, &std::cout),
                 lv ? kc::UINT32MAX : kc::BasicDB::Logger::WARN | kc::BasicDB::Logger::ERROR);
  if (opts > 0) db.tune_options(opts);
  if (bnum > 0) db.tune_buckets(bnum);
  if (capcnt > 0) db.cap_count(capcnt);
  if (capsiz > 0) db.cap_size(capsiz);
  for (int32_t itcnt = 1; itcnt <= itnum; itcnt++) {
    if (itnum > 1) oprintf("iteration %d:\n", itcnt);
    double stime = kc::time();
    uint32_t omode = kc::CacheDB::OWRITER | kc::CacheDB::OCREATE;
    if (itcnt == 1) omode |= kc::CacheDB::OTRUNCATE;
    if (!db.open("*", omode)) {
      dberrprint(&db, __LINE__, "DB::open");
      err = true;
    }
    class ThreadQueue : public kc::Thread {
     public:
      void setparams(int32_t id, kc::CacheDB* db, int64_t rnum, int32_t thnum, bool rnd,
                     int64_t width) {
        id_ = id;
        db_ = db;
        rnum_ = rnum;
        thnum_ = thnum;
        rnd_ = rnd;
        width_ = width;
        err_ = false;
      }
      bool error() {
        return err_;
      }
      void run() {
        kc::DB::Cursor* cur = db_->cursor();
        int64_t base = id_ * rnum_;
        int64_t range = rnum_ * thnum_;
        for (int64_t i = 1; !err_ && i <= rnum_; i++) {
          char kbuf[RECBUFSIZ];
          size_t ksiz = std::sprintf(kbuf, "%010lld", (long long)(base + i));
          if (!db_->set(kbuf, ksiz, kbuf, ksiz)) {
            dberrprint(db_, __LINE__, "DB::set");
            err_ = true;
          }
          if (rnd_) {
            if (myrand(width_ / 2) == 0) {
              if (!cur->jump() && db_->error() != kc::BasicDB::Error::NOREC) {
                dberrprint(db_, __LINE__, "Cursor::jump");
                err_ = true;
              }
              ksiz = std::sprintf(kbuf, "%010lld", (long long)myrand(range) + 1);
              switch (myrand(10)) {
                case 0: {
                  if (!db_->set(kbuf, ksiz, kbuf, ksiz)) {
                    dberrprint(db_, __LINE__, "DB::set");
                    err_ = true;
                  }
                  break;
                }
                case 1: {
                  if (!db_->append(kbuf, ksiz, kbuf, ksiz)) {
                    dberrprint(db_, __LINE__, "DB::append");
                    err_ = true;
                  }
                  break;
                }
                case 2: {
                  if (!db_->remove(kbuf, ksiz) &&
                      db_->error() != kc::BasicDB::Error::NOREC) {
                    dberrprint(db_, __LINE__, "DB::remove");
                    err_ = true;
                  }
                  break;
                }
              }
              int64_t dnum = myrand(width_) + 2;
              for (int64_t j = 0; j < dnum; j++) {
                if (myrand(2) == 0) {
                  size_t rsiz;
                  char* rbuf = cur->get_key(&rsiz);
                  if (rbuf) {
                    if (myrand(10) == 0 && !db_->remove(rbuf, rsiz) &&
                        db_->error() != kc::BasicDB::Error::NOREC) {
                      dberrprint(db_, __LINE__, "DB::remove");
                      err_ = true;
                    }
                    if (myrand(2) == 0 && !cur->jump(rbuf, rsiz) &&
                        db_->error() != kc::BasicDB::Error::NOREC) {
                      dberrprint(db_, __LINE__, "Cursor::jump");
                      err_ = true;
                    }
                    if (myrand(10) == 0 && !db_->remove(rbuf, rsiz) &&
                        db_->error() != kc::BasicDB::Error::NOREC) {
                      dberrprint(db_, __LINE__, "DB::remove");
                      err_ = true;
                    }
                    delete[] rbuf;
                  } else if (db_->error() != kc::BasicDB::Error::NOREC) {
                    dberrprint(db_, __LINE__, "Cursor::get_key");
                    err_ = true;
                  }
                }
                if (!cur->remove() && db_->error() != kc::BasicDB::Error::NOREC) {
                  dberrprint(db_, __LINE__, "Cursor::remove");
                  err_ = true;
                }
              }
            }
          } else {
            if (i > width_) {
              if (!cur->jump() && db_->error() != kc::BasicDB::Error::NOREC) {
                dberrprint(db_, __LINE__, "Cursor::jump");
                err_ = true;
              }
              if (!cur->remove() && db_->error() != kc::BasicDB::Error::NOREC) {
                dberrprint(db_, __LINE__, "Cursor::remove");
                err_ = true;
              }
            }
          }
          if (id_ < 1 && rnum_ > 250 && i % (rnum_ / 250) == 0) {
            oputchar('.');
            if (i == rnum_ || i % (rnum_ / 10) == 0) oprintf(" (%08lld)\n", (long long)i);
          }
        }
        delete cur;
      }
     private:
      int32_t id_;
      kc::CacheDB* db_;
      int64_t rnum_;
      int32_t thnum_;
      bool rnd_;
      int64_t width_;
      bool err_;
    };
    int64_t width = rnum / 10;
    ThreadQueue threads[THREADMAX];
    if (thnum < 2) {
      threads[0].setparams(0, &db, rnum / thnum, thnum, rnd, width);
      threads[0].run();
      if (threads[0].error()) err = true;
    } else {
      for (int32_t i = 0; i < thnum; i++) {
        threads[i].setparams(i, &db, rnum / thnum, thnum, rnd, width);
        threads[i].start();
      }
      for (int32_t i = 0; i < thnum; i++) {
        threads[i].join();
        if (threads[i].error()) err = true;
      }
    }
    int64_t count = db.count();
    if (!rnd && itcnt == 1 && count != width * thnum) {
      dberrprint(&db, __LINE__, "DB::count");
      err = true;
    }
    if ((rnd ? (myrand(2) == 0) : itcnt == itnum) && count > 0) {
      kc::DB::Cursor* cur = db.cursor();
      if (!cur->jump()) {
        dberrprint(&db, __LINE__, "Cursor::jump");
        err = true;
      }
      for (int64_t i = 1; i <= count; i++) {
        if (!cur->remove()) {
          dberrprint(&db, __LINE__, "Cursor::remove");
          err = true;
        }
        if (rnum > 250 && i % (rnum / 250) == 0) {
          oputchar('.');
          if (i == rnum || i % (rnum / 10) == 0) oprintf(" (%08lld)\n", (long long)i);
        }
      }
      if (rnd) oprintf(" (end)\n");
      delete cur;
      if (db.count() != 0) {
        dberrprint(&db, __LINE__, "DB::count");
        err = true;
      }
    }
    dbmetaprint(&db, itcnt == itnum);
    if (!db.close()) {
      dberrprint(&db, __LINE__, "DB::close");
      err = true;
    }
    oprintf("time: %.3f\n", kc::time() - stime);
  }
  oprintf("%s\n\n", err ? "error" : "ok");
  return err ? 1 : 0;
}

const int k_turns = 10;
int x = 0;

// perform wicked command
static int32_t procwicked(int64_t rnum, int32_t thnum, int32_t itnum,
                          int32_t opts, int64_t bnum, int64_t capcnt, int64_t capsiz, bool lv) {

  //sanity check correctness before running the benchmark.
  //procsanity(thnum, rnum); //removed to debug some timing issue.

  oprintf("<Wicked Test>\n  seed=%u  rnum=%lld  thnum=%d  itnum=%d"
          "  opts=%d  bnum=%lld  capcnt=%lld  capsiz=%lld  lv=%d\n\n",
          g_randseed, (long long)rnum, thnum, itnum,
          opts, (long long)bnum, (long long)capcnt, (long long)capsiz, lv);
  bool err = false;
  kc::CacheDB db;
  db.tune_logger(stdlogger(g_progname, &std::cout),
                 lv ? kc::UINT32MAX : kc::BasicDB::Logger::WARN | kc::BasicDB::Logger::ERROR);
  if (opts > 0) db.tune_options(opts);
  if (bnum > 0) db.tune_buckets(bnum);
  if (capcnt > 0) db.cap_count(capcnt);
  if (capsiz > 0) db.cap_size(capsiz);
  for (int32_t itcnt = 1; itcnt <= itnum; itcnt++) {
    if (itnum > 1) oprintf("iteration %d:\n", itcnt);
    uint32_t omode = kc::CacheDB::OWRITER | kc::CacheDB::OCREATE;
    if (itcnt == 1) omode |= kc::CacheDB::OTRUNCATE;
    if (!db.open("*", omode)) {
      dberrprint(&db, __LINE__, "DB::open");
      err = true;
    }
    class ThreadWicked : public kc::Thread {
      int seed_ =0;

     public:
      void setparams(int32_t id, kc::CacheDB* db, int64_t rnum, int32_t thnum,
                     const char* lbuf) {
        id_ = id;
        db_ = db;
        rnum_ = rnum;
        thnum_ = thnum;
        lbuf_ = lbuf;
        err_ = false;
        seed_ = rand();
      }

      int myrand(int i){
        return myrandmarsaglia(i, &seed_);
      }

      bool error() {
        return err_;
      }

      void run() {
        //printf("id: %d. rnum: %ld\n", id_, rnum_);
#ifdef CURSOR
        kc::DB::Cursor* cur = db_->cursor();
#endif
        int64_t range = rnum_ * thnum_ / 2;
        for (int64_t i = 1; !err_ && i <= rnum_; i++) {
        // 1/100th of the time, do a transaction.
//		  bool tran = false; // myrand(100) == 0; // TODO: disable transactions for now (SPAA 2014 ALE paper experience)
//          if (tran) {
//            if (myrand(2) == 0) {
//              if (!db_->begin_transaction(myrand(rnum_) == 0)) {
//                dberrprint(db_, __LINE__, "DB::begin_transaction");
//                tran = false;
//                err_ = true;
//              }
//            } else {
//              if (!db_->begin_transaction_try(myrand(rnum_) == 0)) {
//                if (db_->error() != kc::BasicDB::Error::LOGIC) {
//                  dberrprint(db_, __LINE__, "DB::begin_transaction_try");
//                  err_ = true;
//                }
//                tran = false;
//              }
//            }
//          }

          // key is randomly generated in the range 0 -> (rnum_ * thnum_ / 2)
          char kbuf[RECBUFSIZ];
          size_t ksiz = std::sprintf(kbuf, "%lld", (long long)(myrand(range) + 1));  // ie random. size of such an int will be about 20 chars.
//          if (myrand(1000) == 0) {
//            ksiz = myrand(RECBUFSIZ) + 1;
//            if (myrand(2) == 0) {
//              for (size_t j = 0; j < ksiz; j++) {  // 1/1000 * 1/2 of the time, we get 1,2,3,4...64 as the key (ie, not random, long.).
//                kbuf[j] = j;
//              }
//            } else {
//              for (size_t j = 0; j < ksiz; j++) {  // 1/1000 * 1/2 of the time, we get rand,rand,rand,... (64 times) as the key. ie, long.
//                kbuf[j] = myrand(256);
//              }
//            }
//          }
          const char* vbuf = kbuf;
          size_t vsiz = ksiz;
//          if (myrand(10) == 0) { // 1/10th of the time, we will use lbuf_ as the val, with a random size, a
//            vbuf = lbuf_;
//            vsiz = myrand(RECBUFSIZL) / (myrand(5) + 1);
//          }

          int turn = k_turns;
          do {
            switch (turn % 50) {
              case 0: {
                if (!db_->set(kbuf, ksiz, vbuf, vsiz)) {
                  dberrprint(db_, __LINE__, "DB::set");
                  err_ = true;
                }
                break;
              }
              case 1: {
                if (!db_->add(kbuf, ksiz, vbuf, vsiz) &&
                    db_->error() != kc::BasicDB::Error::DUPREC) {
                  dberrprint(db_, __LINE__, "DB::add");
                  err_ = true;
                }
                break;
              }
              case 2: {
                if (!db_->replace(kbuf, ksiz, vbuf, vsiz) &&
                    db_->error() != kc::BasicDB::Error::NOREC) {
                  dberrprint(db_, __LINE__, "DB::replace");
                  err_ = true;
                }
                break;
              }
              case 3: {
                if (!db_->append(kbuf, ksiz, vbuf, vsiz)) {
                  dberrprint(db_, __LINE__, "DB::append");
                  err_ = true;
                }
                break;
              }
//              case 4: {
//                if (myrand(2) == 0) {
//                  int64_t num = myrand(rnum_);
//                  int64_t orig = myrand(10) == 0 ? kc::INT64MIN : myrand(rnum_);
//                  if (myrand(10) == 0) orig = orig == kc::INT64MIN ? kc::INT64MAX : -orig;
//                  if (db_->increment(kbuf, ksiz, num, orig) == kc::INT64MIN &&
//                      db_->error() != kc::BasicDB::Error::LOGIC) {
//                    dberrprint(db_, __LINE__, "DB::increment");
//                    err_ = true;
//                  }
//                } else {
//                  double num = myrand(rnum_ * 10) / (myrand(rnum_) + 1.0);
//                  double orig = myrand(10) == 0 ? -kc::inf() : myrand(rnum_);
//                  if (myrand(10) == 0) orig = -orig;
//                  if (kc::chknan(db_->increment_double(kbuf, ksiz, num, orig)) &&
//                      db_->error() != kc::BasicDB::Error::LOGIC) {
//                    dberrprint(db_, __LINE__, "DB::increment_double");
//                    err_ = true;
//                  }
//                }
//                break;
//              }
//              case 5: {
//                if (!db_->cas(kbuf, ksiz, kbuf, ksiz, vbuf, vsiz) &&
//                    db_->error() != kc::BasicDB::Error::LOGIC) {
//                  dberrprint(db_, __LINE__, "DB::cas");
//                  err_ = true;
//                }
//                break;
//              }
              case 6: {
                if (!db_->remove(kbuf, ksiz) &&
                    db_->error() != kc::BasicDB::Error::NOREC) {
                  dberrprint(db_, __LINE__, "DB::remove");
                  err_ = true;
                }
                break;
              }
//              case 7: {
//                if (myrand(2) == 0) {
//                  if (db_->check(kbuf, ksiz) < 0 && db_->error() != kc::BasicDB::Error::NOREC) {
//                    dberrprint(db_, __LINE__, "DB::check");
//                    err_ = true;
//                  }
//                } else {
//                  size_t rsiz;
//                  char* rbuf = db_->seize(kbuf, ksiz, &rsiz);
//                  if (rbuf) {
//                    delete[] rbuf;
//                  } else if (db_->error() != kc::BasicDB::Error::NOREC) {
//                    dberrprint(db_, __LINE__, "DB::seize");
//                    err_ = true;
//                  }
//                }
//                break;
//              }
#ifdef CURSOR
              case 8: {
                if (myrand(10) == 0) {
                  if (!cur->jump(kbuf, ksiz) &&
                      db_->error() != kc::BasicDB::Error::NOREC) {
                    dberrprint(db_, __LINE__, "Cursor::jump");
                    err_ = true;
                  }
                } else {
                  class VisitorImpl : public kc::DB::Visitor {
                   public:
                    explicit VisitorImpl(const char* lbuf, ThreadWicked *th) :
                      lbuf_(lbuf), myrand3(th->myrand(3)), myrandrecbuf(th->myrand(RECBUFSIZL)), myrand5(th->myrand(5)) {}
                   private:
                    const char* visit_full(const char* kbuf, size_t ksiz,
                                           const char* vbuf, size_t vsiz, size_t* sp) {
                      const char* rv = NOP;
                      switch (myrand3) {
                        case 0: {
                          rv = lbuf_;
                          *sp = myrandrecbuf/ (myrand5 + 1);
                          break;
                        }
                        case 1: {
                          rv = REMOVE;
                          break;
                        }
                      }
                      return rv;
                    }
                    const char* lbuf_;
                    int myrand3 = 0;
                    int myrandrecbuf = 0;
                    int myrand5 = 0;
                  } visitor(lbuf_, this);
                  if (!cur->accept(&visitor, true, myrand(2) == 0) &&
                      db_->error() != kc::BasicDB::Error::NOREC) {
                    dberrprint(db_, __LINE__, "Cursor::accept");
                    err_ = true;
                  }
                  if (myrand(5) > 0 && !cur->step() &&
                      db_->error() != kc::BasicDB::Error::NOREC) {
                    dberrprint(db_, __LINE__, "Cursor::step");
                    err_ = true;
                  }
                }
                break;
              }
#endif
              default: {
                size_t rsiz;
                char* rbuf = db_->get(kbuf, ksiz, &rsiz);
                if (rbuf) {
                  delete[] rbuf;
                } else if (db_->error() != kc::BasicDB::Error::NOREC) {
                  dberrprint(db_, __LINE__, "DB::get");
                  err_ = true;
                }
                break;
              }
            }
          } while (--turn);

/*
 * These are bulk set/get/remove
 */
//          if (myrand(100) == 0) {  // 1/100 of the time, after done with other ops, also do some bulk ops
//            int32_t jnum = myrand(10);
//            switch (myrand(4)) {
//              case 0: {
//                std::map<std::string, std::string> recs;
//                for (int32_t j = 0; j < jnum; j++) {
//                  char jbuf[RECBUFSIZ];
//                  size_t jsiz = std::sprintf(jbuf, "%lld", (long long)(myrand(range) + 1));
//                  recs[std::string(jbuf, jsiz)] = std::string(kbuf, ksiz);
//                }
//                if (db_->set_bulk(recs, myrand(4)) != (int64_t)recs.size()) {
//                  dberrprint(db_, __LINE__, "DB::set_bulk");
//                  err_ = true;
//                }
//                break;
//              }
//              case 1: {
//                std::vector<std::string> keys;
//                for (int32_t j = 0; j < jnum; j++) {
//                  char jbuf[RECBUFSIZ];
//                  size_t jsiz = std::sprintf(jbuf, "%lld", (long long)(myrand(range) + 1));
//                  keys.push_back(std::string(jbuf, jsiz));
//                }
//                if (db_->remove_bulk(keys, myrand(4)) < 0) {
//                  dberrprint(db_, __LINE__, "DB::remove_bulk");
//                  err_ = true;
//                }
//                break;
//              }
//              default: {
//                std::vector<std::string> keys;
//                for (int32_t j = 0; j < jnum; j++) {
//                  char jbuf[RECBUFSIZ];
//                  size_t jsiz = std::sprintf(jbuf, "%lld", (long long)(myrand(range) + 1));
//                  keys.push_back(std::string(jbuf, jsiz));
//                }
//                std::map<std::string, std::string> recs;
//                if (db_->get_bulk(keys, &recs, myrand(4)) < 0) {
//                  dberrprint(db_, __LINE__, "DB::get_bulk");
//                  err_ = true;
//                }
//                break;
//              }
//            }
//            if (!db_->switch_rotation(myrand(4) > 0)) {
//              dberrprint(db_, __LINE__, "DB::switch_rotation");
//              err_ = true;
//            }
//          }
//          if (i == rnum_ / 2) {
//            if (myrand(thnum_ * 4) == 0) {
//              if (!db_->clear()) {
//                dberrprint(db_, __LINE__, "DB::clear");
//                err_ = true;
//              }
//            } else {
//              class SyncProcessor : public kc::BasicDB::FileProcessor {
//               private:
//                bool process(const std::string& path, int64_t count, int64_t size) {
//                  yield();
//                  return true;
//                }
//              } syncprocessor;
//              if (!db_->synchronize(false, &syncprocessor)) {
//                dberrprint(db_, __LINE__, "DB::synchronize");
//                err_ = true;
//              }
//            }
//          }
/*
 * These are for transaction
 */
//          if (tran) {
//            yield();
//            if (!db_->end_transaction(myrand(10) > 0)) {
//              dberrprint(db_, __LINE__, "DB::end_transactin");
//              err_ = true;
//            }
//          }
//          if (id_ < 1 && rnum_ > 250 && i % (rnum_ / 250) == 0) {
//            oputchar('.');
//            if (i == rnum_ || i % (rnum_ / 10) == 0) oprintf(" (%08lld)\n", (long long)i);
//          }
          assert(!err_);
        }
#ifdef CURSOR
       delete cur;
#endif
      }
     private:
      int32_t id_;
      kc::CacheDB* db_;
      int64_t rnum_;
      int32_t thnum_;
      const char* lbuf_;
      bool err_;
    };
    char lbuf[RECBUFSIZL];
    std::memset(lbuf, '*', sizeof(lbuf));
    ThreadWicked threads[THREADMAX];

//    if (false ) { //thnum < 2
//      threads[0].setparams(0, &db, rnum / thnum, thnum, lbuf);
//      threads[0].run();
//      if (threads[0].error()) err = true;
    for (int32_t i = 0; i < thnum; i++) {
      threads[i].setparams(i, &db, rnum / thnum, thnum, lbuf);
    }

    double stime = kc::time();
    for (int32_t i = 0; i < thnum; i++) {
      threads[i].start();
    }
    for (int32_t i = 0; i < thnum; i++) {
      threads[i].join();
    }
     double time = kc::time() - stime;

    for (int32_t i = 0; i < thnum; i++) {
      assert(!threads[i].error());
    }

    oprintf("rnum:%d\n", rnum);
    oprintf("turns:%d\n", k_turns);
    oprintf("threads:%d\n", thnum);
    oprintf("slotnum:%d\n", db.SLOTNUM);
    oprintf("time: %.3f\n", time);
    oprintf("throughput: %.3f\n", rnum*k_turns/time);

    dbmetaprint(&db, itcnt == itnum);
    if (!db.close()) {
      dberrprint(&db, __LINE__, "DB::close");
      err = true;
    }
  }
  assert(!err);
  oprintf("%s\n\n", err ? "error" : "ok");
  return err ? 1 : 0;
}


// perform tran command
static int32_t proctran(int64_t rnum, int32_t thnum, int32_t itnum,
                        int32_t opts, int64_t bnum, int64_t capcnt, int64_t capsiz, bool lv) {
  oprintf("<Transaction Test>\n  seed=%u  rnum=%lld  thnum=%d  itnum=%d"
          "  opts=%d  bnum=%lld  capcnt=%lld  capsiz=%lld  lv=%d\n\n",
          g_randseed, (long long)rnum, thnum, itnum,
          opts, (long long)bnum, (long long)capcnt, (long long)capsiz, lv);
  bool err = false;
  kc::CacheDB db;
  kc::CacheDB paradb;
  db.tune_logger(stdlogger(g_progname, &std::cout),
                 lv ? kc::UINT32MAX : kc::BasicDB::Logger::WARN | kc::BasicDB::Logger::ERROR);
  paradb.tune_logger(stdlogger(g_progname, &std::cout), lv ? kc::UINT32MAX :
                     kc::BasicDB::Logger::WARN | kc::BasicDB::Logger::ERROR);
  if (opts > 0) db.tune_options(opts);
  if (bnum > 0) db.tune_buckets(bnum);
  if (capcnt > 0) db.cap_count(capcnt);
  if (capsiz > 0) db.cap_size(capsiz);
  for (int32_t itcnt = 1; itcnt <= itnum; itcnt++) {
    oprintf("iteration %d updating:\n", itcnt);
    double stime = kc::time();
    uint32_t omode = kc::CacheDB::OWRITER | kc::CacheDB::OCREATE;
    if (itcnt == 1) omode |= kc::CacheDB::OTRUNCATE;
    if (!db.open("*", omode)) {
      dberrprint(&db, __LINE__, "DB::open");
      err = true;
    }
    if (!paradb.open("para", omode)) {
      dberrprint(&paradb, __LINE__, "DB::open");
      err = true;
    }
    class ThreadTran : public kc::Thread {
     public:
      void setparams(int32_t id, kc::CacheDB* db, kc::CacheDB* paradb, int64_t rnum,
                     int32_t thnum, const char* lbuf) {
        id_ = id;
        db_ = db;
        paradb_ = paradb;
        rnum_ = rnum;
        thnum_ = thnum;
        lbuf_ = lbuf;
        err_ = false;
      }
      bool error() {
        return err_;
      }
      void run() {
        kc::DB::Cursor* cur = db_->cursor();
        int64_t range = rnum_ * thnum_;
        char kbuf[RECBUFSIZ];
        size_t ksiz = std::sprintf(kbuf, "%lld", (long long)(myrand(range) + 1));
        if (!cur->jump(kbuf, ksiz) && db_->error() != kc::BasicDB::Error::NOREC) {
          dberrprint(db_, __LINE__, "Cursor::jump");
          err_ = true;
        }
        bool tran = true;
        if (!db_->begin_transaction(false)) {
          dberrprint(db_, __LINE__, "DB::begin_transaction");
          tran = false;
          err_ = true;
        }
        bool commit = myrand(10) > 0;
        for (int64_t i = 1; !err_ && i <= rnum_; i++) {
          ksiz = std::sprintf(kbuf, "%lld", (long long)(myrand(range) + 1));
          const char* vbuf = kbuf;
          size_t vsiz = ksiz;
          if (myrand(10) == 0) {
            vbuf = lbuf_;
            vsiz = myrand(RECBUFSIZL) / (myrand(5) + 1);
          }
          class VisitorImpl : public kc::DB::Visitor {
           public:
            explicit VisitorImpl(const char* vbuf, size_t vsiz, kc::BasicDB* paradb) :
                vbuf_(vbuf), vsiz_(vsiz), paradb_(paradb) {}
           private:
            const char* visit_full(const char* kbuf, size_t ksiz,
                                   const char* vbuf, size_t vsiz, size_t* sp) {
              return visit_empty(kbuf, ksiz, sp);
            }
            const char* visit_empty(const char* kbuf, size_t ksiz, size_t* sp) {
              const char* rv = NOP;
//              switch (myrand(3)) {
//                case 0: {
//                  rv = vbuf_;
//                  *sp = vsiz_;
//                  if (paradb_) paradb_->set(kbuf, ksiz, vbuf_, vsiz_);
//                  break;
//                }
//                case 1: {
//                  rv = REMOVE;
//                  if (paradb_) paradb_->remove(kbuf, ksiz);
//                  break;
//                }
//              }
              assert(0);
              return rv;
            }
            const char* vbuf_;
            size_t vsiz_;
            kc::BasicDB* paradb_;
          } visitor(vbuf, vsiz, !tran || commit ? paradb_ : NULL);
          if (myrand(4) == 0) {
            if (!cur->accept(&visitor, true, myrand(2) == 0) &&
                db_->error() != kc::BasicDB::Error::NOREC) {
              dberrprint(db_, __LINE__, "Cursor::accept");
              err_ = true;
            }
          } else {
            if (!db_->accept(kbuf, ksiz, &visitor, true)) {
              dberrprint(db_, __LINE__, "DB::accept");
              err_ = true;
            }
          }
          if (myrand(1000) == 0) {
            ksiz = std::sprintf(kbuf, "%lld", (long long)(myrand(range) + 1));
            if (!cur->jump(kbuf, ksiz)) {
              if (db_->error() != kc::BasicDB::Error::NOREC) {
                dberrprint(db_, __LINE__, "Cursor::jump");
                err_ = true;
              } else if (!cur->jump() && db_->error() != kc::BasicDB::Error::NOREC) {
                dberrprint(db_, __LINE__, "Cursor::jump");
                err_ = true;
              }
            }
            std::vector<std::string> keys;
            keys.reserve(100);
            while (myrand(50) != 0) {
              std::string key;
              if (cur->get_key(&key)) {
                keys.push_back(key);
                if (!cur->get_value(&key) && kc::BasicDB::Error::NOREC) {
                  dberrprint(db_, __LINE__, "Cursor::get_value");
                  err_ = true;
                }
              } else {
                if (db_->error() != kc::BasicDB::Error::NOREC) {
                  dberrprint(db_, __LINE__, "Cursor::get_key");
                  err_ = true;
                }
                break;
              }
              if (!cur->step()) {
                if (db_->error() != kc::BasicDB::Error::NOREC) {
                  dberrprint(db_, __LINE__, "Cursor::jump");
                  err_ = true;
                }
                break;
              }
            }
            class Remover : public kc::DB::Visitor {
             public:
              explicit Remover(kc::BasicDB* paradb) : paradb_(paradb) {}
             private:
              const char* visit_full(const char* kbuf, size_t ksiz,
                                     const char* vbuf, size_t vsiz, size_t* sp) {
                if (myrand(200) == 0) return NOP;
                assert(0); //TODO
                //if (paradb_) paradb_->remove(kbuf, ksiz);
                return REMOVE;
              }
              kc::BasicDB* paradb_;
            } remover(!tran || commit ? paradb_ : NULL);
            std::vector<std::string>::iterator it = keys.begin();
            std::vector<std::string>::iterator end = keys.end();
            while (it != end) {
              if (myrand(50) == 0) {
                if (!cur->accept(&remover, true, false) &&
                    db_->error() != kc::BasicDB::Error::NOREC) {
                  dberrprint(db_, __LINE__, "Cursor::accept");
                  err_ = true;
                }
              } else {
                if (!db_->accept(it->c_str(), it->size(), &remover, true)) {
                  dberrprint(db_, __LINE__, "DB::accept");
                  err_ = true;
                }
              }
              ++it;
            }
          }
          if (tran && myrand(100) == 0) {
            if (db_->end_transaction(commit)) {
              yield();
              if (!db_->begin_transaction(false)) {
                dberrprint(db_, __LINE__, "DB::begin_transaction");
                tran = false;
                err_ = true;
              }
            } else {
              dberrprint(db_, __LINE__, "DB::end_transaction");
              err_ = true;
            }
          }
          if (id_ < 1 && rnum_ > 250 && i % (rnum_ / 250) == 0) {
            oputchar('.');
            if (i == rnum_ || i % (rnum_ / 10) == 0) oprintf(" (%08lld)\n", (long long)i);
          }
        }
        if (tran && !db_->end_transaction(commit)) {
          dberrprint(db_, __LINE__, "DB::end_transaction");
          err_ = true;
        }
        delete cur;
      }
     private:
      int32_t id_;
      kc::CacheDB* db_;
      kc::CacheDB* paradb_;
      int64_t rnum_;
      int32_t thnum_;
      const char* lbuf_;
      bool err_;
    };
    char lbuf[RECBUFSIZL];
    std::memset(lbuf, '*', sizeof(lbuf));
    ThreadTran threads[THREADMAX];
    if (thnum < 2) {
      threads[0].setparams(0, &db, &paradb, rnum / thnum, thnum, lbuf);
      threads[0].run();
      if (threads[0].error()) err = true;
    } else {
      for (int32_t i = 0; i < thnum; i++) {
		printf("th[%d] ops=%ld\n", i, rnum / thnum);
		threads[i].setparams(i, &db, &paradb, rnum / thnum, thnum, lbuf);
        threads[i].start();
      }
      for (int32_t i = 0; i < thnum; i++) {
        threads[i].join();
        if (threads[i].error()) err = true;
      }
    }
    oprintf("iteration %d checking:\n", itcnt);
    if (db.count() != paradb.count()) {
      dberrprint(&db, __LINE__, "DB::count");
      err = true;
    }
    class VisitorImpl : public kc::DB::Visitor {
     public:
      explicit VisitorImpl(int64_t rnum, kc::BasicDB* paradb) :
          rnum_(rnum), paradb_(paradb), err_(false), cnt_(0) {}
      bool error() {
        return err_;
      }
     private:
      const char* visit_full(const char* kbuf, size_t ksiz,
                             const char* vbuf, size_t vsiz, size_t* sp) {
//        cnt_++;
//        size_t rsiz;
        //char* rbuf = paradb_->get(kbuf, ksiz, &rsiz);
//        if (rbuf) {
//          delete[] rbuf;
//        } else {
//          dberrprint(paradb_, __LINE__, "DB::get");
//          err_ = true;
//        }
//        if (rnum_ > 250 && cnt_ % (rnum_ / 250) == 0) {
//          oputchar('.');
//          if (cnt_ == rnum_ || cnt_ % (rnum_ / 10) == 0) oprintf(" (%08lld)\n", (long long)cnt_);
//        }
        assert(0); // FIXME
        return NOP;
      }
      int64_t rnum_;
      kc::BasicDB* paradb_;
      bool err_;
      int64_t cnt_;
    } visitor(rnum, &paradb), paravisitor(rnum, &db);
    if (!db.iterate(&visitor, false)) {
      dberrprint(&db, __LINE__, "DB::iterate");
      err = true;
    }
    oprintf(" (end)\n");
    if (visitor.error()) err = true;
    if (!paradb.iterate(&paravisitor, false)) {
      dberrprint(&db, __LINE__, "DB::iterate");
      err = true;
    }
    oprintf(" (end)\n");
    if (paravisitor.error()) err = true;
    if (!paradb.close()) {
      dberrprint(&paradb, __LINE__, "DB::close");
      err = true;
    }
    dbmetaprint(&db, itcnt == itnum);
    if (!db.close()) {
      dberrprint(&db, __LINE__, "DB::close");
      err = true;
    }
    oprintf("time: %.3f\n", kc::time() - stime);
  }
  oprintf("%s\n\n", err ? "error" : "ok");
  return err ? 1 : 0;
}

#ifndef NDEBUG
static void sanity(kc::CacheDB& db, int id, int rnum) {

  using namespace std;

  static const size_t maxsz = 1000;
  string key(to_string(id) + "keynumber" + to_string(id));

  for (int i = 0; i < rnum; ++i){
    char val0[maxsz];

    //should get nothing
    assert(db.get(key.c_str(), key.size(), val0, sizeof(val0)) == -1);

    //add should succeed
    auto firstval = key + "firstval";
    assert(db.add(key.c_str(), key.size(), firstval.c_str(), firstval.size()));
    // should read my write
    int res_raw = db.get(key.c_str(), key.size(), val0, sizeof(val0));
    assert (res_raw >= 0);
    assert((unsigned int)res_raw == firstval.size());
    //cout << string(val0) << " " << firstval << endl;
    assert(string(val0, res_raw) == firstval);

    // repeated add returns false
    assert(!db.add(key.c_str(), key.size(), firstval.c_str(), firstval.size()));

    // should read my write still
    res_raw = db.get(key.c_str(), key.size(), val0, sizeof(val0));
    assert (res_raw >= 0);
    assert((unsigned int)res_raw == firstval.size());
    assert(string(val0, res_raw) == firstval);

    auto secondval = key + "secondval";
    assert(db.set(key, secondval));

    // should read my  second write
    res_raw = db.get(key.c_str(), key.size(), val0, sizeof(val0));
    assert (res_raw >= 0);
    assert((unsigned int)res_raw == secondval.size());
    assert(string(val0, res_raw) == secondval);

    // delete
    int res_del = db.remove(key.c_str(), key.size());
    assert(res_del);
  }
}
#endif

static void procsanity(int thnum, int rnum) {
#ifndef NDEBUG
  kc::CacheDB db;
  uint32_t omode = kc::CacheDB::OWRITER | kc::CacheDB::OCREATE;
  assert(db.open("*", omode));

  class ThreadSanity : public kc::Thread {
  public:
    void setparams(kc::CacheDB *db, int id, int rnum){
      db_ = db;
      thid_ = id;
      rnum_ = rnum;
    }

    void run() {
      assert(db_ && rnum_);
      sanity(*db_, thid_, rnum_);
    }

  private:
    kc::CacheDB* db_ = nullptr;
    int thid_ = 0;
    int rnum_ = 0;
  };

  ThreadSanity threads[THREADMAX];

  for (int32_t i = 0; i < thnum; i++) {
    threads[i].setparams(&db, i, rnum);
  }

  for (int32_t i = 0; i < thnum; i++) {
    threads[i].start();
  }

  for (int32_t i = 0; i < thnum; i++) {
    threads[i].join();
  }

  assert(db.close());
#endif
}



// END OF FILE
