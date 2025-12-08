// statistical info of a process
struct pstat {
  int pid;   // current pid using the pstat record
  uint64 ctime; // creation time
  uint64 stime; // time that the process starts
  uint64 etime; // time that the process finishes (ends)
  // these metrics are cumulative
  uint64 qtime; // time spent in the scheduling queue
  uint64 rptime; // cumulative response (rp) time
  struct spinlock lk;
};
