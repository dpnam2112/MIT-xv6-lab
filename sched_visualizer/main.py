import re
import matplotlib.pyplot as plt
import matplotlib.patches as mpatches
import argparse
from collections import defaultdict, namedtuple
import sys
import os

# --- Configuration (State Mapping and Modern Color Palette) ---
STATE_MAP = {
    0: "UNUSED",
    1: "USED",
    2: "SLEEPING",
    3: "RUNNABLE",
    4: "RUNNING",
    5: "ZOMBIE"
}

# A modern, distinct color palette (Flat UI style)
STATE_COLORS = {
    "UNUSED":   '#CFD8DC',  # Light Blue-Gray
    "USED":     '#B0BEC5',  # Medium Blue-Gray
    "SLEEPING": '#7986CB',  # Muted Indigo
    "RUNNABLE": '#FFB74D',  # Warm Orange/Amber
    "RUNNING":  '#4DB6AC',  # Teal/Green (Active looking)
    "ZOMBIE":   '#37474F',  # Dark Charcoal
    "INITIAL":  'white'     # Placeholder
}

# Named tuple for clear data structure
TraceEvent = namedtuple('TraceEvent', ['tick', 'pid', 'state', 'prio', 'event'])

# --- Core Functions (Parsing & Processing remain the same) ---

def parse_trace_data(file_path):
    """Parses the trace data from a file into a list of TraceEvent objects."""
    events = []
    pattern = re.compile(r'(\w+)=(\d+)')
    try:
        with open(file_path, 'r') as f:
            for line in f:
                line = line.strip()
                if not line: continue
                matches = dict(pattern.findall(line))
                try:
                    event = TraceEvent(
                        tick=int(matches['tick']), pid=int(matches['pid']),
                        state=int(matches['state']), prio=int(matches['prio']),
                        event=int(matches['event'])
                    )
                    events.append(event)
                except (KeyError, ValueError): continue
    except FileNotFoundError:
        raise FileNotFoundError(f"Error: The file '{file_path}' was not found.")
    return events

def process_events_to_intervals(events):
    """Transforms the event list into time intervals for the Gantt chart."""
    process_intervals = defaultdict(list)
    current_state = {}

    for event in events:
        pid, state_val, tick = event.pid, event.state, event.tick
        state_name = STATE_MAP.get(state_val, "UNKNOWN")

        if pid not in current_state:
            current_state[pid] = (tick, "INITIAL") 

        last_tick, last_state_name = current_state[pid]
        
        if last_state_name != state_name:
            if last_state_name != "INITIAL" and last_tick < tick:
                process_intervals[pid].append((last_tick, tick, last_state_name))
            current_state[pid] = (tick, state_name)
        elif last_state_name == state_name:
            current_state[pid] = (tick, state_name)

    final_tick = max(e.tick for e in events) + 1 if events else 0
    for pid, (last_tick, last_state_name) in current_state.items():
        if last_state_name != "INITIAL" and last_tick < final_tick:
            process_intervals[pid].append((last_tick, final_tick, last_state_name))

    return process_intervals

# --- Visualization Logic (Modernized) ---

def plot_gantt_chart(process_intervals, filename_label, output_file=None):
    """
    Plots the process intervals as a modern, clean Gantt Chart.
    """
    if not process_intervals:
        print("No process intervals to plot.")
        return

    # 1. Setup Style
    # Use a clean seaborn style for better defaults (background, fonts)
    try:
        plt.style.use('seaborn-v0_8-whitegrid')
    except OSError:
        # Fallback if seaborn style isn't available
        plt.style.use('fast')

    pids = sorted(process_intervals.keys())
    # Sort PIDs in reverse so smaller PIDs are at the top
    pids.reverse() 
    pid_labels = [f"PID {p}" for p in pids]

    fig, ax = plt.subplots(figsize=(14, 7))

    # 2. Plotting Bars
    for i, pid in enumerate(pids):
        for start, end, state in process_intervals[pid]:
            duration = end - start
            color = STATE_COLORS.get(state, '#9E9E9E')
            
            # Modern look: No heavy black edges, slightly thicker bars (height=0.9)
            # Using a subtle white edge makes segments distinct without being harsh
            ax.barh(i, duration, left=start, height=0.9, color=color, edgecolor='white', linewidth=0.5)
            # Note: Removed in-bar text labels for a cleaner look

    # 3. Modernizing Axes and Grids
    ax.set_yticks(range(len(pids)))
    ax.set_yticklabels(pid_labels, fontsize=11, fontweight='medium', color='#546E7A')
    
    ax.set_xlabel("Time (Ticks)", fontsize=12, labelpad=10, color='#546E7A')
    ax.tick_params(axis='x', colors='#90A4AE', labelsize=10)
    
    # Clean up spines (borders)
    ax.spines['top'].set_visible(False)
    ax.spines['right'].set_visible(False)
    ax.spines['left'].set_visible(False)
    ax.spines['bottom'].set_color('#CFD8DC')

    # Subtler grid lines
    ax.grid(axis='x', linestyle=':', linewidth=0.8, color='#CFD8DC', alpha=0.7)
    ax.grid(axis='y', visible=False) # Hide horizontal grid lines

    ax.set_title(f"CPU Scheduler Activity Trace\nFile: {filename_label}", fontsize=16, pad=20, color='#37474F', fontweight='bold')

    # 4. Clean Legend
    # Create legend handles manually for distinct states excluding 'INITIAL'
    legend_handles = []
    seen_states = set()
    for pid_intervals in process_intervals.values():
        for _, _, state in pid_intervals:
            seen_states.add(state)
            
    sorted_states = [s for s in STATE_COLORS.keys() if s in seen_states and s != "INITIAL"]
    
    for state in sorted_states:
        color = STATE_COLORS[state]
        # Use patches for a cleaner legend icon
        handle = mpatches.Patch(color=color, label=state)
        legend_handles.append(handle)
    
    # Place legend outside top right, horizontally
    leg = ax.legend(handles=legend_handles, title="Process State", 
              loc='upper left', bbox_to_anchor=(1.0, 1.02), 
              frameon=False, fontsize=11, title_fontsize=12)
    leg.get_title().set_color('#546E7A')

    plt.tight_layout(rect=[0, 0, 0.85, 1]) # Adjust layout for legend space

    # 5. Output
    if output_file:
        plt.savefig(output_file, dpi=150, bbox_inches='tight')
        print(f"\n✨ Modern chart saved successfully to: {output_file}")
    else:
        print("\n🖥️  Displaying modern chart...")
        plt.show()

# --- CLI Setup ---

def main():
    parser = argparse.ArgumentParser(
        description="A modern visualizer for CPU scheduler trace logs.",
        epilog="Example: python scheduler_cli_modern.py trace.txt -o my_schedule.png"
    )
    parser.add_argument("trace_file", help="Path to the trace file.")
    parser.add_argument("-o", "--output", help="Save chart to file (png/pdf).", default=None)
    args = parser.parse_args()

    print(f"--- Processing: {args.trace_file} ---")
    try:
        events = parse_trace_data(args.trace_file)
        if not events:
            print("No valid events found.")
            return
        intervals = process_events_to_intervals(events)
        plot_gantt_chart(intervals, os.path.basename(args.trace_file), args.output)

    except Exception as e:
        print(f"Error: {e}", file=sys.stderr)
        sys.exit(1)

if __name__ == "__main__":
    main()
