#define MAX_SCHEDTRACE 1000 // equals 2 physical pages (4 KiB * 2)
enum schedtrace_type { SCHEDTRACE_PROC_STATE_CHANGE, SCHEDTRACE_PRIO_CHANGE };

struct sched_trace
{
  int tick;                   // when this trace was recorded
  int pid;                    // current pid
  uint8 pstate;               // current process's state
  int prio;                   // priority. used in priority-based policies like mlfq
  enum schedtrace_type type;
};

// a simple circular queue containing scheduler trace records.
struct sched_tracer
{
  struct sched_trace sched_traces[MAX_SCHEDTRACE];
  struct spinlock lk;
  int head;
  int tail;
};

