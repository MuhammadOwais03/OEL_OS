#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>
#include <stdbool.h>
#include <unistd.h>

#define MAX_PROCESSES 1000
#define MAX_FILENAME_LENGTH 256
#define TERMINAL_WIDTH 80
#define BAR_LENGTH 50

// ANSI color codes for terminal output
#define COLOR_RESET "\033[0m"
#define COLOR_RED "\033[1;31m"
#define COLOR_GREEN "\033[1;32m"
#define COLOR_YELLOW "\033[1;33m"
#define COLOR_BLUE "\033[1;34m"
#define COLOR_MAGENTA "\033[1;35m"
#define COLOR_CYAN "\033[1;36m"
#define COLOR_WHITE "\033[1;37m"
#define COLOR_BRIGHT_BLACK "\033[1;90m"
#define BOLD "\033[1m"
#define UNDERLINE "\033[4m"

// Structure for a memory block
typedef struct MemoryBlock {
    int start_address;
    int size;
    bool is_free;
    int process_id;  // -1 for free blocks
    int arrival_time;
    int allocation_time;
    struct MemoryBlock* next;
} MemoryBlock;

// Structure for a process
typedef struct Process {
    int pid;
    int size;
    int arrival_time;
    bool allocated;
    int allocation_time;
    int memory_address;
    int waiting_time;       // Time spent waiting to be allocated
    int execution_time;     // Total time process needs to run
    int remaining_time;     // Time remaining until process completes
    bool completed;         // Whether the process has completed execution
} Process;

// Structure for tracking simulation statistics
typedef struct SimulationStats {
    int successful_allocations;
    int failed_allocations;
    int total_fragmentation_events;
    double avg_waiting_time;
    int max_waiting_time;
    double memory_utilization;
    double simulation_duration;
    int completed_processes;
    double avg_turnaround_time;  // Time from arrival to completion
    double avg_execution_time;   // Average actual execution time
} SimulationStats;

// Global variables
MemoryBlock* memory_head = NULL;
Process processes[MAX_PROCESSES];
Process* waiting_queue[MAX_PROCESSES];
int waiting_queue_size = 0;
int allocated_processes[MAX_PROCESSES];
int allocated_count = 0;
int current_time = 0;
int total_memory_size = 0;
SimulationStats stats = {0};
bool color_enabled = true;

// Function prototypes
void initialize_memory(int size);
void display_memory_state();
bool allocate_memory(Process* process);
void deallocate_memory(int pid);
void merge_free_blocks();
void check_waiting_processes();
void simulate_time_step();
bool add_process(Process* process);
Process* create_sample_processes(int num_processes);
int read_processes_from_file(const char* filename, Process* processes);
void save_processes_to_file(Process* processes, int count, const char* filename);
void display_allocated_processes();
void free_memory();
void calculate_memory_utilization();
void display_simulation_stats();
void check_process_completion();
void print_separator(char symbol);
void print_centered_text(const char* text);
void print_progress_bar(double percentage, int width);
void display_simulation_header();
void display_welcome_screen();
void clear_screen();
Process* get_process_by_pid(int pid);

// Clear the terminal screen
void clear_screen() {
    #ifdef _WIN32
        system("cls");
    #else
        system("clear");
    #endif
}

// Print a separator line
void print_separator(char symbol) {
    int i;
    printf("%s", COLOR_BRIGHT_BLACK);
    for (i = 0; i < TERMINAL_WIDTH; i++) {
        printf("%c", symbol);
    }
    printf("%s\n", COLOR_RESET);
}

// Print centered text
void print_centered_text(const char* text) {
    int padding = (TERMINAL_WIDTH - strlen(text)) / 2;
    printf("%*s%s%*s\n", padding, "", text, padding, "");
}

// Print a progress bar
void print_progress_bar(double percentage, int width) {
    int i;
    int pos = width * percentage;
    
    printf("[");
    for (i = 0; i < width; i++) {
        if (i < pos) printf("%s█%s", COLOR_GREEN, COLOR_RESET);
        else printf(" ");
    }
    printf("] %s%.1f%%%s", COLOR_YELLOW, percentage * 100, COLOR_RESET);
}

// Display welcome screen
void display_welcome_screen() {
    clear_screen();
    print_separator('=');
    printf("%s", COLOR_CYAN);
    print_centered_text("MEMORY ALLOCATION SIMULATOR");
    printf("%s", COLOR_RESET);
    print_separator('-');
    printf("\n");
    printf("%s• Best-Fit Algorithm Implementation%s\n", COLOR_YELLOW, COLOR_RESET);
    printf("%s• Process Execution Simulation%s\n", COLOR_YELLOW, COLOR_RESET);
    printf("%s• Memory Utilization Tracking%s\n", COLOR_YELLOW, COLOR_RESET);
    printf("%s• External Fragmentation Analysis%s\n", COLOR_YELLOW, COLOR_RESET);
    printf("\n");
    print_separator('-');
    printf("\n");
}

// Display simulation header
void display_simulation_header() {
    print_separator('=');
    printf("%s%s", BOLD, COLOR_CYAN);
    print_centered_text("MEMORY ALLOCATION SIMULATION");
    printf("%s", COLOR_RESET);
    print_separator('-');
}

// Initialize memory with a single free block
void initialize_memory(int size) {
    total_memory_size = size;
    
    // Create the initial free block
    memory_head = (MemoryBlock*)malloc(sizeof(MemoryBlock));
    if (memory_head == NULL) {
        fprintf(stderr, "Memory allocation failed\n");
        exit(1);
    }
    
    memory_head->start_address = 0;
    memory_head->size = size;
    memory_head->is_free = true;
    memory_head->process_id = -1;
    memory_head->arrival_time = -1;
    memory_head->allocation_time = -1;
    memory_head->next = NULL;
}

// Helper function to get process by pid
Process* get_process_by_pid(int pid) {
    for (int i = 0; i < MAX_PROCESSES; i++) {
        if (processes[i].pid == pid) {
            return &processes[i];
        }
    }
    return NULL;
}

// Display the current state of memory
void display_memory_state() {
    printf("\n%s%s==== MEMORY STATE (Time: %d) ====%s\n", BOLD, COLOR_CYAN, current_time, COLOR_RESET);
    
    // Calculate total free and used memory
    int total_free = 0;
    int total_used = 0;
    MemoryBlock* current = memory_head;
    
    while (current != NULL) {
        if (current->is_free) {
            total_free += current->size;
        } else {
            total_used += current->size;
        }
        current = current->next;
    }
    
    double used_percentage = (double)total_used / total_memory_size;
    
    printf("\n%sTotal Memory:%s %d MB\n", COLOR_WHITE, COLOR_RESET, total_memory_size);
    printf("%sUsed Memory:%s %d MB (", COLOR_WHITE, COLOR_RESET, total_used);
    print_progress_bar(used_percentage, 20);
    printf(")\n");
    printf("%sFree Memory:%s %d MB (%.2f%%)\n", COLOR_WHITE, COLOR_RESET, total_free, (float)total_free / total_memory_size * 100);
    
    printf("\n%sMemory Blocks:%s\n", BOLD, COLOR_RESET);
    
    // Display each memory block as a visualization
    current = memory_head;
    int block_count = 0;
    while (current != NULL) {
        block_count++;
        
        // Display block with different colors based on state
        if (current->is_free) {
            printf("%s[%5d - %5d]%s %s(%4d MB)%s %sFREE%s\n", 
                  COLOR_BRIGHT_BLACK, current->start_address, 
                  current->start_address + current->size - 1, COLOR_RESET,
                  COLOR_BRIGHT_BLACK, current->size, COLOR_RESET,
                  COLOR_GREEN, COLOR_RESET);
        } else {
            Process* proc = get_process_by_pid(current->process_id);
            printf("%s[%5d - %5d]%s %s(%4d MB)%s %sP%-3d%s %s(remaining: %d)%s\n", 
                  COLOR_YELLOW, current->start_address, 
                  current->start_address + current->size - 1, COLOR_RESET,
                  COLOR_YELLOW, current->size, COLOR_RESET,
                  COLOR_RED, current->process_id, COLOR_RESET,
                  COLOR_BLUE, proc ? proc->remaining_time : 0, COLOR_RESET);
        }
        
        current = current->next;
    }
    
    // Display external fragmentation
    int free_block_count = 0;
    int largest_free_block = 0;
    
    current = memory_head;
    while (current != NULL) {
        if (current->is_free) {
            free_block_count++;
            if (current->size > largest_free_block) {
                largest_free_block = current->size;
            }
        }
        current = current->next;
    }
    
    if (free_block_count > 1) {
        stats.total_fragmentation_events++;
        printf("\n%sExternal Fragmentation:%s %d free blocks\n", COLOR_MAGENTA, COLOR_RESET, free_block_count);
        printf("%sLargest free block:%s %d MB\n", COLOR_MAGENTA, COLOR_RESET, largest_free_block);
    }
    
    print_separator('-');
}

// Best-fit algorithm for memory allocation
bool allocate_memory(Process* process) {
    MemoryBlock* best_fit = NULL;
    MemoryBlock* current = memory_head;
    MemoryBlock* prev_best = NULL;
    MemoryBlock* prev = NULL;
    
    // Find the smallest free block that can accommodate the process
    while (current != NULL) {
        if (current->is_free && current->size >= process->size) {
            if (best_fit == NULL || current->size < best_fit->size) {
                best_fit = current;
                prev_best = prev;
            }
        }
        prev = current;
        current = current->next;
    }
    
    // If no suitable block found
    if (best_fit == NULL) {
        stats.failed_allocations++;
        return false;
    }
    
    // If the block is exactly the size needed or slightly larger
    if (best_fit->size <= process->size + 3) { // Small threshold to avoid tiny fragments
        best_fit->is_free = false;
        best_fit->process_id = process->pid;
        best_fit->arrival_time = process->arrival_time;
        best_fit->allocation_time = current_time;
    } else {
        // Split the block: create a new block for the remaining space
        MemoryBlock* new_block = (MemoryBlock*)malloc(sizeof(MemoryBlock));
        if (new_block == NULL) {
            fprintf(stderr, "Memory allocation failed\n");
            return false;
        }
        
        new_block->start_address = best_fit->start_address + process->size;
        new_block->size = best_fit->size - process->size;
        new_block->is_free = true;
        new_block->process_id = -1;
        new_block->arrival_time = -1;
        new_block->allocation_time = -1;
        new_block->next = best_fit->next;
        
        // Update the allocated block
        best_fit->size = process->size;
        best_fit->is_free = false;
        best_fit->process_id = process->pid;
        best_fit->arrival_time = process->arrival_time;
        best_fit->allocation_time = current_time;
        best_fit->next = new_block;
    }
    
    // Update process information
    process->allocated = true;
    process->allocation_time = current_time;
    process->memory_address = best_fit->start_address;
    process->remaining_time = process->execution_time;
    
    // Calculate waiting time
    process->waiting_time = current_time - process->arrival_time;
    if (process->waiting_time > stats.max_waiting_time) {
        stats.max_waiting_time = process->waiting_time;
    }
    
    // Add to allocated processes
    allocated_processes[allocated_count++] = process->pid;
    stats.successful_allocations++;
    
    return true;
}

// Deallocate memory for a process and merge adjacent free blocks
void deallocate_memory(int pid) {
    MemoryBlock* current = memory_head;
    int found = 0;
    
    // Find the block allocated to this process
    while (current != NULL) {
        if (!current->is_free && current->process_id == pid) {
            // Free this block
            current->is_free = true;
            current->process_id = -1;
            current->allocation_time = -1;
            found = 1;
            break;
        }
        current = current->next;
    }
    
    if (found) {
        // Remove from allocated processes
        int i;
        for (i = 0; i < allocated_count; i++) {
            if (allocated_processes[i] == pid) {
                // Remove by shifting remaining elements
                memmove(&allocated_processes[i], &allocated_processes[i + 1], 
                        (allocated_count - i - 1) * sizeof(int));
                allocated_count--;
                break;
            }
        }
        
        // Mark the process as completed if it's not already marked
        Process* proc = get_process_by_pid(pid);
        if (proc != NULL && !proc->completed && proc->remaining_time <= 0) {
            proc->completed = true;
            stats.completed_processes++;
            printf("%sProcess %d completed execution and deallocated at time %d%s\n", 
                   COLOR_GREEN, pid, current_time, COLOR_RESET);
        }
        
        // Merge adjacent free blocks
        merge_free_blocks();
    } else {
        printf("%sProcess %d not found in allocated processes.%s\n", COLOR_RED, pid, COLOR_RESET);
    }
}

// Merge adjacent free blocks to reduce external fragmentation
void merge_free_blocks() {
    MemoryBlock* current = memory_head;
    
    while (current != NULL && current->next != NULL) {
        if (current->is_free && current->next->is_free) {
            // Merge blocks
            MemoryBlock* to_delete = current->next;
            current->size += to_delete->size;
            current->next = to_delete->next;
            free(to_delete);
            // Don't advance current since we need to check if the newly merged block
            // can be merged with the next one too
        } else {
            current = current->next;
        }
    }
}

// Check if any waiting processes can now be allocated
void check_waiting_processes() {
    if (waiting_queue_size == 0) {
        return;
    }
    
    int allocated_from_queue = 0;
    int i, j;
    
    for (i = 0; i < waiting_queue_size; i++) {
        if (allocate_memory(waiting_queue[i])) {
            printf("%sProcess %d allocated from waiting queue (time: %d)%s\n", 
                   COLOR_GREEN, waiting_queue[i]->pid, current_time, COLOR_RESET);
            allocated_from_queue++;
            
            // Remove from waiting queue by shifting remaining elements
            for (j = i; j < waiting_queue_size - 1; j++) {
                waiting_queue[j] = waiting_queue[j + 1];
            }
            waiting_queue_size--;
            i--; // Adjust i since we removed an element
        }
    }
    
    if (allocated_from_queue > 0) {
        printf("%sAllocated %d processes from waiting queue%s\n", COLOR_GREEN, allocated_from_queue, COLOR_RESET);
        if (waiting_queue_size > 0) {
            printf("%s%d processes still waiting%s\n", COLOR_YELLOW, waiting_queue_size, COLOR_RESET);
        }
    }
}

// Check for process completion and deallocate finished processes
void check_process_completion() {
    int i = 0;
    while (i < allocated_count) {
        int pid = allocated_processes[i];
        Process* proc = get_process_by_pid(pid);
        
        if (proc != NULL && !proc->completed) {
            // Decrement remaining time for the process
            proc->remaining_time--;
            
            // Check if process has completed
            if (proc->remaining_time <= 0) {
                printf("%sProcess %d has finished execution at time %d%s\n", 
                       COLOR_GREEN, pid, current_time, COLOR_RESET);
                deallocate_memory(pid);
                // Don't increment i since the array has shifted
            } else {
                i++; // Move to next process
            }
        } else {
            i++; // Move to next process
        }
    }
}

// Calculate current memory utilization
void calculate_memory_utilization() {
    int total_used = 0;
    MemoryBlock* current = memory_head;
    
    while (current != NULL) {
        if (!current->is_free) {
            total_used += current->size;
        }
        current = current->next;
    }
    
    double utilization = (double)total_used / total_memory_size;
    // Update the running average
    if (stats.memory_utilization == 0) {
        stats.memory_utilization = utilization;
    } else {
        stats.memory_utilization = (stats.memory_utilization + utilization) / 2.0;
    }
}

// Advance the simulation by one time unit
void simulate_time_step() {
    current_time++;
    check_process_completion();
    check_waiting_processes();
    calculate_memory_utilization();
}

// Add a process to be allocated
bool add_process(Process* process) {
    // If the process arrival time is in the future, queue it
    if (process->arrival_time > current_time) {
        printf("%sProcess %d will arrive at time %d%s\n", 
               COLOR_YELLOW, process->pid, process->arrival_time, COLOR_RESET);
        return false;
    }
    
    // Try to allocate memory
    if (allocate_memory(process)) {
        printf("%sProcess %d allocated successfully (time: %d, exec time: %d)%s\n", 
               COLOR_GREEN, process->pid, current_time, process->execution_time, COLOR_RESET);
        return true;
    } else {
        // If allocation fails, add to waiting queue
        printf("%sNot enough memory for Process %d. Added to waiting queue.%s\n", 
               COLOR_RED, process->pid, COLOR_RESET);
        waiting_queue[waiting_queue_size++] = process;
        return false;
    }
}

// Create sample processes with varying sizes, arrival times, and execution times
Process* create_sample_processes(int num_processes) {
    Process* processes = (Process*)malloc(num_processes * sizeof(Process));
    if (processes == NULL) {
        fprintf(stderr, "Memory allocation failed\n");
        exit(1);
    }
    
    int i;
    
    srand(time(NULL)); // Initialize random seed
    
    for (i = 0; i < num_processes; i++) {
        processes[i].pid = i + 1;
        // Random size between 10 and 200 MB
        processes[i].size = rand() % 191 + 10;
        // Random arrival time between 0 and 20
        processes[i].arrival_time = rand() % 21;
        // Random execution time between 5 and 30
        processes[i].execution_time = rand() % 26 + 5;
        processes[i].remaining_time = processes[i].execution_time;
        processes[i].allocated = false;
        processes[i].allocation_time = -1;
        processes[i].memory_address = -1;
        processes[i].waiting_time = 0;
        processes[i].completed = false;
    }
    
    // Sort by arrival time using bubble sort (simple enough for this case)
    for (i = 0; i < num_processes - 1; i++) {
        int j;
        for (j = 0; j < num_processes - i - 1; j++) {
            if (processes[j].arrival_time > processes[j + 1].arrival_time) {
                Process temp = processes[j];
                processes[j] = processes[j + 1];
                processes[j + 1] = temp;
            }
        }
    }
    
    return processes;
}

// Read processes from file (improved to handle comments and validate data)
int read_processes_from_file(const char* filename, Process* processes) {
    FILE* file = fopen(filename, "r");
    int count = 0;
    char line[256];
    
    if (file == NULL) {
        printf("%sFile %s not found.%s\n", COLOR_RED, filename, COLOR_RESET);
        return 0;
    }
    
    printf("%sReading processes from %s...%s\n", COLOR_BLUE, filename, COLOR_RESET);
    
    while (count < MAX_PROCESSES && fgets(line, sizeof(line), file)) {
        // Skip comments and empty lines
        if (line[0] == '#' || line[0] == '\n' || (line[0] == '\r' && line[1] == '\n')) {
            continue;
        }
        
        int pid, arrival, size, exec_time;
        
        if (sscanf(line, "%d %d %d %d", &pid, &arrival, &size, &exec_time) == 4) {
            // Validate data
            if (pid <= 0 || arrival < 0 || size <= 0 || exec_time <= 0) {
                printf("%sInvalid data in line: %s (skipping)%s\n", COLOR_RED, line, COLOR_RESET);
                continue;
            }
            
            processes[count].pid = pid;
            processes[count].arrival_time = arrival;
            processes[count].size = size;
            processes[count].execution_time = exec_time;
            processes[count].remaining_time = exec_time;
            processes[count].allocated = false;
            processes[count].allocation_time = -1;
            processes[count].memory_address = -1;
            processes[count].waiting_time = 0;
            processes[count].completed = false;
            count++;
        } else {
            printf("%sInvalid format in line: %s%s\n", COLOR_RED, line, COLOR_RESET);
        }
    }
    
    fclose(file);
    printf("%sSuccessfully read %d processes%s\n", COLOR_GREEN, count, COLOR_RESET);
    return count;
}

// Save generated processes to a file (improved format with comments)
void save_processes_to_file(Process* processes, int count, const char* filename) {
    FILE* file = fopen(filename, "w");
    
    if (file == NULL) {
        printf("%sCould not open file %s for writing.%s\n", COLOR_RED, filename, COLOR_RESET);
        return;
    }
    
    // Write header with format explanation
    fprintf(file, "# Format: PID ArrivalTime Size ExecutionTime\n");
    fprintf(file, "# PID: Process ID (integer)\n");
    fprintf(file, "# ArrivalTime: Time when process arrives (integer)\n");
    fprintf(file, "# Size: Memory size in MB (integer)\n");
    fprintf(file, "# ExecutionTime: Duration the process runs (integer)\n");
    
    int i;
    for (i = 0; i < count; i++) {
        fprintf(file, "%d %d %d %d\n", processes[i].pid, processes[i].arrival_time, 
                processes[i].size, processes[i].execution_time);
    }
    
    fclose(file);
    printf("%sSaved %d processes to %s%s\n", COLOR_GREEN, count, filename, COLOR_RESET);
}

// Display all currently allocated processes
void display_allocated_processes() {
    if (allocated_count == 0) {
        printf("%sNo processes currently allocated in memory.%s\n", COLOR_YELLOW, COLOR_RESET);
        return;
    }
    
    printf("\n%s%sALLOCATED PROCESSES%s\n", BOLD, COLOR_CYAN, COLOR_RESET);
    print_separator('-');
    
    printf("%-6s %-8s %-10s %-10s %-12s %-10s %-10s\n", 
           "PID", "Size", "Address", "Arrival", "Allocation", "Wait", "Remaining");
    print_separator('-');
    
    int i;
    for (i = 0; i < allocated_count; i++) {
        int pid = allocated_processes[i];
        int j;
        
        // Find the process in our process array
        for (j = 0; j < MAX_PROCESSES; j++) {
            if (processes[j].pid == pid && processes[j].allocated) {
                printf("%s%-6d%s %-8d %-10d %-10d %-12d %-10d %s%-10d%s\n",
                       COLOR_RED, processes[j].pid, COLOR_RESET, 
                       processes[j].size, 
                       processes[j].memory_address,
                       processes[j].arrival_time, 
                       processes[j].allocation_time, 
                       processes[j].waiting_time, 
                       (processes[j].remaining_time <= 2) ? COLOR_YELLOW : COLOR_BLUE,
                       processes[j].remaining_time,
                       COLOR_RESET);
                break;
            }
        }
    }
    
    print_separator('-');
}

// Display simulation statistics
void display_simulation_stats() {
    print_separator('=');
    printf("%s%sSIMULATION STATISTICS%s\n", BOLD, COLOR_CYAN, COLOR_RESET);
    print_separator('=');
    
    printf("%sTotal simulation time:%s %d units\n", COLOR_WHITE, COLOR_RESET, current_time);
    
    printf("\n%sPerformance Metrics:%s\n", BOLD, COLOR_RESET);
    printf("  %sSuccessful allocations:%s %d\n", COLOR_GREEN, COLOR_RESET, stats.successful_allocations);
    printf("  %sFailed allocations:%s %d\n", COLOR_RED, COLOR_RESET, stats.failed_allocations);
    printf("  %sCompleted processes:%s %d\n", COLOR_GREEN, COLOR_RESET, stats.completed_processes);
    printf("  %sFragmentation events:%s %d\n", COLOR_YELLOW, COLOR_RESET, stats.total_fragmentation_events);
    
    // Calculate turnaround and waiting time statistics
    int total_waiting_time = 0;
    int total_turnaround_time = 0;
    int total_execution_time = 0;
    int completed_count = 0;
    
    for (int i = 0; i < MAX_PROCESSES; i++) {
        if (processes[i].pid > 0 && processes[i].completed) {
            total_waiting_time += processes[i].waiting_time;
            total_turnaround_time += (processes[i].allocation_time + processes[i].execution_time) - 
                                     processes[i].arrival_time;
            total_execution_time += processes[i].execution_time;
            completed_count++;
        }
    }
    
    printf("\n%sTiming Metrics:%s\n", BOLD, COLOR_RESET);
    if (completed_count > 0) {
        stats.avg_waiting_time = (double)total_waiting_time / completed_count;
        stats.avg_turnaround_time = (double)total_turnaround_time / completed_count;
        stats.avg_execution_time = (double)total_execution_time / completed_count;
        
        printf("  %sAverage waiting time:%s %.2f time units\n", COLOR_BLUE, COLOR_RESET, stats.avg_waiting_time);
        printf("  %sAverage turnaround time:%s %.2f time units\n", COLOR_BLUE, COLOR_RESET, stats.avg_turnaround_time);
        printf("  %sAverage execution time:%s %.2f time units\n", COLOR_BLUE, COLOR_RESET, stats.avg_execution_time);
        printf("  %sMaximum waiting time:%s %d time units\n", COLOR_RED, COLOR_RESET, stats.max_waiting_time);
    }
    
    printf("\n%sMemory Utilization:%s %.2f%%\n", BOLD, COLOR_GREEN, stats.memory_utilization * 100);
    
    double utilization_visuals = stats.memory_utilization;
    print_progress_bar(utilization_visuals, BAR_LENGTH);
    printf("\n");
    
    printf("\n%sSimulation duration:%s %.4f seconds\n", COLOR_MAGENTA, COLOR_RESET, stats.simulation_duration);
    print_separator('=');
}

// Free all allocated memory at the end
void free_memory() {
    MemoryBlock* current = memory_head;
    while (current != NULL) {
        MemoryBlock* next = current->next;
        free(current);
        current = next;
    }
    memory_head = NULL;
}

int main() {
    char input[20];
    char filename[MAX_FILENAME_LENGTH];
    int num_processes = 10;
    int memory_size = 0;
    bool sim_initialized = false;
    display_welcome_screen();

    while (1) {
        print_separator('=');
        printf("%sMain Menu%s\n", COLOR_CYAN, COLOR_RESET);
        printf("1. Generate sample processes\n");
        printf("2. Load processes from file\n");
        printf("3. Save processes to file\n");
        printf("4. Set memory size\n");
        printf("5. Run simulation\n");
        printf("6. Exit\n");
        printf("Enter choice: ");
        scanf("%s", input);

        if (strcmp(input, "6") == 0) {
            printf("Exiting...\n");
            free_memory();
            break;
        }

        switch(atoi(input)) {
            case 1: {
                printf("Number of processes (max %d): ", MAX_PROCESSES);
                scanf("%d", &num_processes);
                Process* sample = create_sample_processes(num_processes);
                memcpy(processes, sample, num_processes * sizeof(Process));
                free(sample);
                printf("%sGenerated %d random processes%s\n", COLOR_GREEN, num_processes, COLOR_RESET);
                break;
            }
            case 2: {
                printf("Enter filename: ");
                scanf("%s", filename);
                num_processes = read_processes_from_file(filename, processes);
                break;
            }
            case 3: {
                printf("Enter filename: ");
                scanf("%s", filename);
                save_processes_to_file(processes, num_processes, filename);
                break;
            }
            case 4: {
                printf("Enter memory size (MB): ");
                scanf("%d", &memory_size);
                if (memory_size <= 0) {
                    printf("%sInvalid memory size%s\n", COLOR_RED, COLOR_RESET);
                    break;
                }
                if (memory_head) free_memory();
                initialize_memory(memory_size);
                sim_initialized = true;
                printf("%sMemory initialized to %d MB%s\n", COLOR_GREEN, memory_size, COLOR_RESET);
                break;
            }
            case 5: {
                if (!sim_initialized) {
                    printf("%sMemory not initialized!%s\n", COLOR_RED, COLOR_RESET);
                    break;
                }
                if (num_processes <= 0) {
                    printf("%sNo processes loaded!%s\n", COLOR_RED, COLOR_RESET);
                    break;
                }

                // Initialize simulation state
                current_time = 0;
                memset(&stats, 0, sizeof(stats));
                waiting_queue_size = 0;
                allocated_count = 0;
                memory_head->next = NULL;
                memory_head->size = memory_size;
                memory_head->is_free = true;

                // Run simulation
                int step_mode = 0;
                printf("Enable step-by-step? (1/0): ");
                scanf("%d", &step_mode);

                clock_t start = clock();
                int current_process = 0;

                while (current_process < num_processes || allocated_count > 0 || waiting_queue_size > 0) {
                    clear_screen();
                    display_simulation_header();

                    // Add arriving processes
                    while (current_process < num_processes && 
                           processes[current_process].arrival_time <= current_time) {
                        add_process(&processes[current_process]);
                        current_process++;
                    }

                    // Update simulation state
                    simulate_time_step();
                    display_memory_state();
                    display_allocated_processes();

                    // Handle display timing
                    if (step_mode) {
                        printf("Press ENTER to continue...");
                        while (getchar() != '\n'); 
                        getchar();
                    } else {
                        usleep(500000); // 0.5 second delay
                    }
                }

                stats.simulation_duration = (double)(clock() - start)/CLOCKS_PER_SEC;
                display_simulation_stats();
                break;
            }
            default:
                printf("%sInvalid choice!%s\n", COLOR_RED, COLOR_RESET);
        }
    }

    return 0;
}