#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>
#include <stdbool.h>
#include <unistd.h>

#define MAX_PROCESSES 1000
#define MAX_FILENAME_LENGTH 256

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

// Display the current state of memory
void display_memory_state() {
    printf("\n==================================================\n");
    printf("CURRENT MEMORY STATE (Time: %d)\n", current_time);
    printf("==================================================\n");
    
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
    
    printf("Total Memory: %d MB\n", total_memory_size);
    printf("Used Memory: %d MB (%.2f%%)\n", total_used, (float)total_used / total_memory_size * 100);
    printf("Free Memory: %d MB (%.2f%%)\n", total_free, (float)total_free / total_memory_size * 100);
    printf("--------------------------------------------------\n");
    
    // Display each memory block
    current = memory_head;
    while (current != NULL) {
        printf("[%d - %d] (%d MB) ", current->start_address, 
               current->start_address + current->size - 1, current->size);
        
        if (current->is_free) {
            printf("FREE\n");
        } else {
            // printf("USED by P%d (remaining: %d)\n", current->process_id, 
            //       get_process_by_pid(current->process_id)->remaining_time);
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
        printf("\nExternal Fragmentation: %d free blocks\n", free_block_count);
        printf("Largest free block: %d MB\n", largest_free_block);
    }
    
    printf("==================================================\n");
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
            printf("Process %d completed execution and deallocated at time %d\n", 
                   pid, current_time);
        }
        
        // Merge adjacent free blocks
        merge_free_blocks();
    } else {
        printf("Process %d not found in allocated processes.\n", pid);
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
            printf("Process %d allocated from waiting queue (time: %d)\n", 
                   waiting_queue[i]->pid, current_time);
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
        printf("Allocated %d processes from waiting queue\n", allocated_from_queue);
        if (waiting_queue_size > 0) {
            printf("%d processes still waiting\n", waiting_queue_size);
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
                printf("Process %d has finished execution at time %d\n", pid, current_time);
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
        printf("Process %d will arrive at time %d\n", process->pid, process->arrival_time);
        return false;
    }
    
    // Try to allocate memory
    if (allocate_memory(process)) {
        printf("Process %d allocated successfully (time: %d, exec time: %d)\n", 
               process->pid, current_time, process->execution_time);
        return true;
    } else {
        // If allocation fails, add to waiting queue
        printf("Not enough memory for Process %d. Added to waiting queue.\n", process->pid);
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

// Read processes from file (new format with execution time)
int read_processes_from_file(const char* filename, Process* processes) {
    FILE* file = fopen(filename, "r");
    int count = 0;
    
    if (file == NULL) {
        printf("File %s not found.\n", filename);
        return 0;
    }
    
    while (count < MAX_PROCESSES && 
           fscanf(file, "%d %d %d %d", &processes[count].pid, 
                  &processes[count].arrival_time, &processes[count].size,
                  &processes[count].execution_time) == 4) {
        processes[count].remaining_time = processes[count].execution_time;
        processes[count].allocated = false;
        processes[count].allocation_time = -1;
        processes[count].memory_address = -1;
        processes[count].waiting_time = 0;
        processes[count].completed = false;
        count++;
    }
    
    fclose(file);
    return count;
}

// Save generated processes to a file (new format with execution time)
void save_processes_to_file(Process* processes, int count, const char* filename) {
    FILE* file = fopen(filename, "w");
    
    if (file == NULL) {
        printf("Could not open file %s for writing.\n", filename);
        return;
    }
    
    int i;
    for (i = 0; i < count; i++) {
        fprintf(file, "%d %d %d %d\n", processes[i].pid, processes[i].arrival_time, 
                processes[i].size, processes[i].execution_time);
    }
    
    fclose(file);
    printf("Saved %d processes to %s\n", count, filename);
}

// Display all currently allocated processes
void display_allocated_processes() {
    if (allocated_count == 0) {
        printf("No processes currently allocated in memory.\n");
        return;
    }
    
    printf("\nAllocated Processes:\n");
    printf("--------------------------------------------------\n");
    
    int i;
    for (i = 0; i < allocated_count; i++) {
        int pid = allocated_processes[i];
        int j;
        
        // Find the process in our process array
        for (j = 0; j < MAX_PROCESSES; j++) {
            if (processes[j].pid == pid && processes[j].allocated) {
                printf("Process %d: Size=%dMB, Address=%d, Arrival=%d, Allocated at=%d, "
                       "Wait time=%d, Exec time=%d, Remaining=%d\n",
                       processes[j].pid, processes[j].size, processes[j].memory_address,
                       processes[j].arrival_time, processes[j].allocation_time, 
                       processes[j].waiting_time, processes[j].execution_time,
                       processes[j].remaining_time);
                break;
            }
        }
    }
}

// Display simulation statistics
void display_simulation_stats() {
    printf("\n==================================================\n");
    printf("SIMULATION STATISTICS\n");
    printf("==================================================\n");
    printf("Total simulation time: %d units\n", current_time);
    printf("Successful allocations: %d\n", stats.successful_allocations);
    printf("Failed allocations: %d\n", stats.failed_allocations);
    printf("Completed processes: %d\n", stats.completed_processes);
    printf("Fragmentation events: %d\n", stats.total_fragmentation_events);
    printf("Maximum waiting time: %d time units\n", stats.max_waiting_time);
    
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
    
    if (completed_count > 0) {
        stats.avg_waiting_time = (double)total_waiting_time / completed_count;
        stats.avg_turnaround_time = (double)total_turnaround_time / completed_count;
        stats.avg_execution_time = (double)total_execution_time / completed_count;
        
        printf("Average waiting time: %.2f time units\n", stats.avg_waiting_time);
        printf("Average turnaround time: %.2f time units\n", stats.avg_turnaround_time);
        printf("Average execution time: %.2f time units\n", stats.avg_execution_time);
    }
    
    printf("Average memory utilization: %.2f%%\n", stats.memory_utilization * 100);
    printf("Simulation duration: %.4f seconds\n", stats.simulation_duration);
    printf("==================================================\n");
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
    char input[10];
    char filename[MAX_FILENAME_LENGTH];
    int num_processes = 10;
    int process_count = 0;
    int i;
    
    // Start timing the execution
    clock_t start_time = clock();
    
    // Initialize memory
    int memory_size = 1024;  // 1 GB in MB
    initialize_memory(memory_size);
    
    // Option to read processes from file or generate them
    printf("Read processes from file? (y/n): ");
    scanf("%s", input);
    
    if (input[0] == 'y' || input[0] == 'Y') {
        printf("Enter filename: ");
        scanf("%s", filename);
        process_count = read_processes_from_file(filename, processes);
        
        if (process_count == 0) {
            printf("No valid processes found in file. Generating sample processes.\n");
            Process* generated = create_sample_processes(num_processes);
            memcpy(processes, generated, num_processes * sizeof(Process));
            process_count = num_processes;
            free(generated);
            
            printf("Save generated processes to file? (y/n): ");
            scanf("%s", input);
            if (input[0] == 'y' || input[0] == 'Y') {
                save_processes_to_file(processes, process_count, "processes.txt");
            }
        }
    } else {
        printf("Enter number of processes (default 10): ");
        scanf("%s", input);
        if (strlen(input) > 0 && atoi(input) > 0) {
            num_processes = atoi(input);
        }
        
        Process* generated = create_sample_processes(num_processes);
        memcpy(processes, generated, num_processes * sizeof(Process));
        process_count = num_processes;
        free(generated);
        
        printf("Save generated processes to file? (y/n): ");
        scanf("%s", input);
        if (input[0] == 'y' || input[0] == 'Y') {
            save_processes_to_file(processes, process_count, "processes.txt");
        }
    }
    
    // Display process information
    printf("\nProcess Information:\n");
    for (i = 0; i < process_count; i++) {
        printf("P%d: Size=%dMB, Arrival=%d, Execution time=%d\n", 
               processes[i].pid, processes[i].size, processes[i].arrival_time,
               processes[i].execution_time);
    }
    
    // Calculate process statistics
    int min_size = processes[0].size;
    int max_size = processes[0].size;
    int total_size = 0;
    int min_exec = processes[0].execution_time;
    int max_exec = processes[0].execution_time;
    int total_exec = 0;
    
    for (i = 0; i < process_count; i++) {
        if (processes[i].size < min_size) min_size = processes[i].size;
        if (processes[i].size > max_size) max_size = processes[i].size;
        total_size += processes[i].size;
        
        if (processes[i].execution_time < min_exec) min_exec = processes[i].execution_time;
        if (processes[i].execution_time > max_exec) max_exec = processes[i].execution_time;
        total_exec += processes[i].execution_time;
    }
    
    printf("\nProcess Statistics:\n");
    printf("Total Processes: %d\n", process_count);
    printf("Memory Size: Min=%d MB, Max=%d MB, Avg=%.2f MB\n", 
           min_size, max_size, (float)total_size / process_count);
    printf("Execution Time: Min=%d, Max=%d, Avg=%.2f\n", 
           min_exec, max_exec, (float)total_exec / process_count);
    printf("Total Process Size: %d MB\n", total_size);
    printf("Memory Size: %d MB\n", memory_size);
    
    // Run simulation
    printf("\nStarting Memory Allocation Simulation...\n");
    
    // Set the maximum simulation time
    int max_time = 0;
    for (i = 0; i < process_count; i++) {
        if (processes[i].arrival_time > max_time) {
            max_time = processes[i].arrival_time;
        }
    }
    max_time += 50; // Add extra time for simulation to account for execution times
    
    // Display initial memory state
    display_memory_state();
    
    // Simulation loop
    int next_process_index = 0;
    
    while (current_time <= max_time) {
        // Check for arriving processes
        while (next_process_index < process_count && 
               processes[next_process_index].arrival_time <= current_time) {
            add_process(&processes[next_process_index]);
            display_memory_state();
            next_process_index++;
        }
        
        // If all processes have been processed, completed, and none are waiting
        if (next_process_index >= process_count && waiting_queue_size == 0 && allocated_count == 0) {
            printf("\nAll processes have completed execution. Ending simulation.\n");
            break;
        }
        
        // Advance time
        simulate_time_step();
        
        // Occasionally display memory state (every 5 time units)
        if (current_time % 5 == 0) {
            display_memory_state();
        }
    }
    
    // Final memory state
    printf("\nFinal Memory State:\n");
    display_memory_state();
    display_allocated_processes();
    
    printf("\nSimulation ended at time %d\n", current_time);
    printf("Processed %d processes, completed %d\n", process_count, stats.completed_processes);
    
    // Calculate execution time
    clock_t end_time = clock();
    stats.simulation_duration = (double)(end_time - start_time) / CLOCKS_PER_SEC;
    
    // Display simulation statistics
    display_simulation_stats();
    
    // Clean up memory
    free_memory();
    
    return 0;
}