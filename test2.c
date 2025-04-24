#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>
#include <stdbool.h>
#include <unistd.h>

// Constants
#define MAX_PROCESSES 100
#define MAX_FILENAME_LENGTH 100
#define PAGE_SIZE 4       // Using 4MB pages for simplicity (would be 4KB in real systems)
#define MAX_FRAMES 1024   // Maximum number of frames in physical memory
#define MAX_PARTITION_SIZE 256  // Maximum size for dynamic partitioning (in MB)

// Structure for a memory block (for dynamic partitioning)
typedef struct MemoryBlock {
    int start_address;
    int size;
    bool is_free;
    int process_id;  // -1 for free blocks
    int arrival_time;
    int allocation_time;
    struct MemoryBlock* next;
} MemoryBlock;

// Structure for a page table entry
typedef struct {
    int page_number;
    int frame_number;
    bool is_loaded;
} PageTableEntry;

// Structure for a process's page table
typedef struct {
    int process_id;
    int total_pages;
    PageTableEntry* entries;
} ProcessPageTable;

// Structure for a process
typedef struct Process {
    int pid;
    int size;
    int arrival_time;
    bool allocated;
    int allocation_time;
    int memory_address;    // For dynamic partitioning
    bool uses_paging;      // Whether this process uses paging
    ProcessPageTable* page_table;  // For paged processes
} Process;

// Global variables
MemoryBlock* memory_head = NULL;
Process processes[MAX_PROCESSES];
Process* waiting_queue[MAX_PROCESSES];
int waiting_queue_size = 0;
int allocated_processes[MAX_PROCESSES];
int allocated_count = 0;
int current_time = 0;
int total_memory_size = 0;

// Frame table for paging
bool frame_table[MAX_FRAMES];  // true if frame is allocated
int frame_owner[MAX_FRAMES];   // process ID owning the frame

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
int read_processes_from_file(const char* filename, Process* processes, int* memory_size);
void save_processes_to_file(Process* processes, int count, int memory_size, const char* filename);
void display_allocated_processes();
void free_memory();
bool handle_process(Process* process);
bool allocate_with_paging(Process* process);
ProcessPageTable* create_page_table(int pid, int pages_needed);
int allocate_frames(ProcessPageTable* page_table, int pages_needed);
void deallocate_paged_process(int pid);
void display_frame_usage();

// Initialize memory with a single free block
void initialize_memory(int size) {
    total_memory_size = size;
    
    // Initialize frame table for paging
    int i;
    for (i = 0; i < MAX_FRAMES && i < (size / PAGE_SIZE); i++) {
        frame_table[i] = false;    // All frames are initially free
        frame_owner[i] = -1;       // No owner
    }
    
    // Create the initial free block for dynamic partitioning
    memory_head = (MemoryBlock*)malloc(sizeof(MemoryBlock));
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
    
    // Calculate total free and used memory for dynamic partitioning
    int total_free_dp = 0;
    int total_used_dp = 0;
    MemoryBlock* current = memory_head;
    
    while (current != NULL) {
        if (current->is_free) {
            total_free_dp += current->size;
        } else {
            total_used_dp += current->size;
        }
        current = current->next;
    }
    
    // Calculate paging usage
    int total_frames = total_memory_size / PAGE_SIZE;
    int used_frames = 0;
    int i;
    
    for (i = 0; i < total_frames; i++) {
        if (frame_table[i]) {
            used_frames++;
        }
    }
    
    int total_used_paging = used_frames * PAGE_SIZE;
    
    printf("Total Memory: %d MB\n", total_memory_size);
    printf("Dynamic Partitioning: Used %d MB, Free %d MB\n", total_used_dp, total_free_dp);
    printf("Paging: Used %d MB (%d frames), Free %d MB (%d frames)\n", 
           total_used_paging, used_frames, 
           (total_frames * PAGE_SIZE) - total_used_paging, total_frames - used_frames);
    
    printf("--------------------------------------------------\n");
    printf("DYNAMIC PARTITIONING BLOCKS:\n");
    
    // Display each memory block
    current = memory_head;
    while (current != NULL) {
        printf("[%d - %d] (%d MB) ", current->start_address, 
               current->start_address + current->size - 1, current->size);
        
        if (current->is_free) {
            printf("FREE\n");
        } else {
            printf("USED by P%d\n", current->process_id);
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
        printf("\nExternal Fragmentation: %d free blocks\n", free_block_count);
        printf("Largest free block: %d MB\n", largest_free_block);
    }
    
    // Display frame usage
    display_frame_usage();
    
    printf("==================================================\n");
}

// Display frame usage for paging
void display_frame_usage() {
    int total_frames = total_memory_size / PAGE_SIZE;
    int i;
    
    printf("\nPAGING FRAMES:\n");
    
    for (i = 0; i < total_frames; i++) {
        if (i % 16 == 0) {
            printf("\nFrames %3d-%3d: ", i, i + 15 < total_frames ? i + 15 : total_frames - 1);
        }
        
        if (frame_table[i]) {
            printf("P%d ", frame_owner[i]);
        } else {
            printf("□  ");  // Empty frame
        }
    }
    printf("\n");
}

// Best-fit algorithm for memory allocation (dynamic partitioning)
bool allocate_memory(Process* process) {
    // Only use dynamic partitioning for processes <= MAX_PARTITION_SIZE
    if (process->size > MAX_PARTITION_SIZE) {
        return false;
    }
    
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
    process->uses_paging = false;
    
    // Add to allocated processes
    allocated_processes[allocated_count++] = process->pid;
    
    return true;
}

// Create page table for a process
ProcessPageTable* create_page_table(int pid, int pages_needed) {
    ProcessPageTable* page_table = (ProcessPageTable*)malloc(sizeof(ProcessPageTable));
    page_table->process_id = pid;
    page_table->total_pages = pages_needed;
    page_table->entries = (PageTableEntry*)malloc(pages_needed * sizeof(PageTableEntry));
    
    int i;
    for (i = 0; i < pages_needed; i++) {
        page_table->entries[i].page_number = i;
        page_table->entries[i].frame_number = -1;
        page_table->entries[i].is_loaded = false;
    }
    
    return page_table;
}

// Allocate frames for a paged process
int allocate_frames(ProcessPageTable* page_table, int pages_needed) {
    int total_frames = total_memory_size / PAGE_SIZE;
    int allocated_frames = 0;
    int i, j;
    
    // Try to allocate frames for as many pages as possible
    for (i = 0; i < page_table->total_pages; i++) {
        // Find a free frame
        for (j = 0; j < total_frames; j++) {
            if (!frame_table[j]) {
                // Allocate this frame
                frame_table[j] = true;
                frame_owner[j] = page_table->process_id;
                
                // Update page table entry
                page_table->entries[i].frame_number = j;
                page_table->entries[i].is_loaded = true;
                
                allocated_frames++;
                break;
            }
        }
        
        // If no free frame found, stop allocation (could implement swapping here)
        if (j == total_frames) {
            break;
        }
    }
    
    return allocated_frames;
}

// Allocate process using paging
bool allocate_with_paging(Process* process) {
    int pages_needed = (process->size + PAGE_SIZE - 1) / PAGE_SIZE;
    
    // Create page table for this process
    ProcessPageTable* page_table = create_page_table(process->pid, pages_needed);
    
    // Allocate frames for some or all pages
    int pages_loaded = allocate_frames(page_table, pages_needed);
    
    if (pages_loaded > 0) {
        // Process is at least partially loaded
        process->allocated = true;
        process->allocation_time = current_time;
        process->uses_paging = true;
        process->page_table = page_table;
        
        // Add to allocated processes
        allocated_processes[allocated_count++] = process->pid;
        
        printf("Process %d partially loaded with paging (%d/%d pages)\n", 
               process->pid, pages_loaded, pages_needed);
        return true;
    } else {
        // Free the page table if we couldn't allocate any frames
        free(page_table->entries);
        free(page_table);
        return false;
    }
}

// Try to allocate a process using either dynamic partitioning or paging
bool handle_process(Process* process) {
    // Try dynamic partitioning first for smaller processes
    if (process->size <= MAX_PARTITION_SIZE && allocate_memory(process)) {
        printf("Process %d allocated with dynamic partitioning (time: %d)\n", 
               process->pid, current_time);
        return true;
    }
    
    // If dynamic partitioning fails or process is too large, try paging
    if (allocate_with_paging(process)) {
        printf("Process %d allocated with paging (time: %d)\n", 
               process->pid, current_time);
        return true;
    }
    
    // If both methods fail
    return false;
}

// Deallocate paged process
void deallocate_paged_process(int pid) {
    int i;
    int total_frames = total_memory_size / PAGE_SIZE;
    
    // Free all frames owned by this process
    for (i = 0; i < total_frames; i++) {
        if (frame_table[i] && frame_owner[i] == pid) {
            frame_table[i] = false;
            frame_owner[i] = -1;
        }
    }
    
    // Find the process and free its page table
    for (i = 0; i < MAX_PROCESSES; i++) {
        if (processes[i].pid == pid && processes[i].allocated && processes[i].uses_paging) {
            free(processes[i].page_table->entries);
            free(processes[i].page_table);
            processes[i].page_table = NULL;
            processes[i].allocated = false;
            break;
        }
    }
}

// Deallocate memory for a process (dynamic partitioning or paging)
void deallocate_memory(int pid) {
    int i;
    bool found = false;
    
    // Find the process
    for (i = 0; i < MAX_PROCESSES; i++) {
        if (processes[i].pid == pid && processes[i].allocated) {
            if (processes[i].uses_paging) {
                // Deallocate paged process
                deallocate_paged_process(pid);
            } else {
                // Deallocate dynamic partition
                MemoryBlock* current = memory_head;
                
                // Find the block allocated to this process
                while (current != NULL) {
                    if (!current->is_free && current->process_id == pid) {
                        // Free this block
                        current->is_free = true;
                        current->process_id = -1;
                        current->allocation_time = -1;
                        found = true;
                        break;
                    }
                    current = current->next;
                }
                
                if (found) {
                    // Merge adjacent free blocks
                    merge_free_blocks();
                }
            }
            
            processes[i].allocated = false;
            
            // Remove from allocated processes
            for (i = 0; i < allocated_count; i++) {
                if (allocated_processes[i] == pid) {
                    // Remove by shifting remaining elements
                    memmove(&allocated_processes[i], &allocated_processes[i + 1], 
                            (allocated_count - i - 1) * sizeof(int));
                    allocated_count--;
                    break;
                }
            }
            
            printf("Process %d deallocated successfully\n", pid);
            return;
        }
    }
    
    printf("Process %d not found in allocated processes.\n", pid);
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
        if (handle_process(waiting_queue[i])) {
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

// Advance the simulation by one time unit
void simulate_time_step() {
    current_time++;
    check_waiting_processes();
}

// Add a process to be allocated
bool add_process(Process* process) {
    // If the process arrival time is in the future, queue it
    if (process->arrival_time > current_time) {
        printf("Process %d will arrive at time %d\n", process->pid, process->arrival_time);
        return false;
    }
    
    // Try to allocate memory
    if (handle_process(process)) {
        return true;
    } else {
        // If allocation fails, add to waiting queue
        printf("Not enough memory for Process %d. Added to waiting queue.\n", process->pid);
        waiting_queue[waiting_queue_size++] = process;
        return false;
    }
}

// Create sample processes with varying sizes and arrival times
Process* create_sample_processes(int num_processes) {
    Process* processes = (Process*)malloc(num_processes * sizeof(Process));
    int i;
    
    srand(time(NULL)); // Initialize random seed
    
    for (i = 0; i < num_processes; i++) {
        processes[i].pid = i + 1;
        // Random size between 10 and 300 MB (some larger than MAX_PARTITION_SIZE)
        processes[i].size = rand() % 291 + 10;
        // Random arrival time between 0 and 20
        processes[i].arrival_time = rand() % 21;
        processes[i].allocated = false;
        processes[i].allocation_time = -1;
        processes[i].memory_address = -1;
        processes[i].uses_paging = false;
        processes[i].page_table = NULL;
    }
    
    // Ensure at least one process is larger than MAX_PARTITION_SIZE to demonstrate paging
    if (num_processes >= 3) {
        processes[num_processes - 1].size = MAX_PARTITION_SIZE + 50 + (rand() % 200);
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

// Read processes from file (new format with memory size on first line)
int read_processes_from_file(const char* filename, Process* processes, int* memory_size) {
    FILE* file = fopen(filename, "r");
    int count = 0;
    
    if (file == NULL) {
        printf("File %s not found.\n", filename);
        return 0;
    }
    
    // Read memory size from first line
    if (fscanf(file, "%d", memory_size) != 1) {
        printf("Error reading memory size from file.\n");
        fclose(file);
        return 0;
    }
    
    // Read process information
    while (count < MAX_PROCESSES && 
           fscanf(file, "%d %d %d", &processes[count].pid, 
                  &processes[count].arrival_time, &processes[count].size) == 3) {
        processes[count].allocated = false;
        processes[count].allocation_time = -1;
        processes[count].memory_address = -1;
        processes[count].uses_paging = false;
        processes[count].page_table = NULL;
        count++;
    }
    
    fclose(file);
    return count;
}

// Save generated processes to a file (with memory size on first line)
void save_processes_to_file(Process* processes, int count, int memory_size, const char* filename) {
    FILE* file = fopen(filename, "w");
    
    if (file == NULL) {
        printf("Could not open file %s for writing.\n", filename);
        return;
    }
    
    // Write memory size as first line
    fprintf(file, "%d\n", memory_size);
    
    // Write process information
    int i;
    for (i = 0; i < count; i++) {
        fprintf(file, "%d %d %d\n", processes[i].pid, processes[i].arrival_time, processes[i].size);
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
                printf("Process %d: Size=%dMB, ", processes[j].pid, processes[j].size);
                
                if (processes[j].uses_paging) {
                    // Count loaded pages
                    int loaded_pages = 0;
                    int total_pages = processes[j].page_table->total_pages;
                    int k;
                    
                    for (k = 0; k < total_pages; k++) {
                        if (processes[j].page_table->entries[k].is_loaded) {
                            loaded_pages++;
                        }
                    }
                    
                    printf("Paging (%d/%d pages loaded)", loaded_pages, total_pages);
                } else {
                    printf("Address=%d (Dynamic Partition)", processes[j].memory_address);
                }
                
                printf(", Arrival=%d, Allocated at=%d\n",
                       processes[j].arrival_time, processes[j].allocation_time);
                break;
            }
        }
    }
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
    
    // Free page tables for any paged processes
    int i;
    for (i = 0; i < MAX_PROCESSES; i++) {
        if (processes[i].allocated && processes[i].uses_paging && processes[i].page_table != NULL) {
            free(processes[i].page_table->entries);
            free(processes[i].page_table);
        }
    }
}

int main() {
    char input[10];
    char filename[MAX_FILENAME_LENGTH];
    int num_processes = 10;
    int process_count = 0;
    int i;
    int memory_size = 0;
    
    // Get memory size from user if not loading from file
    printf("Enter memory size in MB (default 1024): ");
    fgets(input, sizeof(input), stdin);
    if (strlen(input) > 1) {
        memory_size = atoi(input);
    }
    if (memory_size <= 0) {
        memory_size = 1024;  // Default if invalid input
    }
    
    // Initialize memory
    initialize_memory(memory_size);
    
    // Option to read processes from file or generate them
    printf("Read processes from file? (y/n): ");
    fgets(input, sizeof(input), stdin);
    
    if (input[0] == 'y' || input[0] == 'Y') {
        printf("Enter filename: ");
        fgets(filename, sizeof(filename), stdin);
        // Remove newline if present
        int len = strlen(filename);
        if (len > 0 && filename[len-1] == '\n') {
            filename[len-1] = '\0';
        }
        
        process_count = read_processes_from_file(filename, processes, &memory_size);
        
        // If memory size was read from file, reinitialize
        if (process_count > 0 && memory_size > 0) {
            free_memory();
            initialize_memory(memory_size);
        }
        
        if (process_count == 0) {
            printf("No valid processes found in file. Generating sample processes.\n");
            Process* generated = create_sample_processes(num_processes);
            memcpy(processes, generated, num_processes * sizeof(Process));
            process_count = num_processes;
            free(generated);
            
            printf("Save generated processes to file? (y/n): ");
            fgets(input, sizeof(input), stdin);
            if (input[0] == 'y' || input[0] == 'Y') {
                save_processes_to_file(processes, process_count, memory_size, "processes.txt");
            }
        }
    } else {
        printf("Enter number of processes (default 10): ");
        fgets(input, sizeof(input), stdin);
        if (strlen(input) > 1) {
            int temp = atoi(input);
            if (temp > 0) num_processes = temp;
        }
        
        Process* generated = create_sample_processes(num_processes);
        memcpy(processes, generated, num_processes * sizeof(Process));
        process_count = num_processes;
        free(generated);
        
        printf("Save generated processes to file? (y/n): ");
        fgets(input, sizeof(input), stdin);
        if (input[0] == 'y' || input[0] == 'Y') {
            save_processes_to_file(processes, process_count, memory_size, "processes.txt");
        }
    }
    
    // Display process information
    printf("\nProcess Information:\n");
    for (i = 0; i < process_count; i++) {
        printf("P%d: Size=%dMB, Arrival=%d\n", 
               processes[i].pid, processes[i].size, processes[i].arrival_time);
    }
    
    // Calculate process statistics
    int min_size = processes[0].size;
    int max_size = processes[0].size;
    int total_size = 0;
    
    for (i = 0; i < process_count; i++) {
        if (processes[i].size < min_size) min_size = processes[i].size;
        if (processes[i].size > max_size) max_size = processes[i].size;
        total_size += processes[i].size;
    }
    
    printf("\nProcess Statistics:\n");
    printf("Total Processes: %d\n", process_count);
    printf("Minimum Size: %d MB\n", min_size);
    printf("Maximum Size: %d MB\n", max_size);
    printf("Average Size: %.2f MB\n", (float)total_size / process_count);
    printf("Total Process Size: %d MB\n", total_size);
    printf("Memory Size: %d MB\n", memory_size);
    printf("Page Size: %d MB\n", PAGE_SIZE);
    printf("Max Size for Dynamic Partitioning: %d MB\n", MAX_PARTITION_SIZE);
    
    // Run simulation
    printf("\nStarting Memory Allocation Simulation...\n");
    
    // Set the maximum simulation time
    int max_time = 0;
    for (i = 0; i < process_count; i++) {
        if (processes[i].arrival_time > max_time) {
            max_time = processes[i].arrival_time;
        }
    }
    max_time += 30; // Add extra time for simulation
    
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
        
        // If all processes have been processed and none are waiting
        if (next_process_index >= process_count && waiting_queue_size == 0) {
            // Check if any processes are still in memory
            if (allocated_count > 0) {
                // Allow simulation to continue for demonstration
            } else {
                // End simulation if nothing left to do
                break;
            }
        }
        
        // Randomly deallocate a process occasionally to demonstrate memory management
        if (allocated_count > 0 && current_time % 10 == 0) {
            int pid_to_deallocate = allocated_processes[0];
            printf("\nDeallocating Process %d for demonstration\n", pid_to_deallocate);
            deallocate_memory(pid_to_deallocate);
            display_memory_state();
        }
        
        // Interactive mode - wait for user input between time steps
        printf("\nTime: %d - Press Enter to advance or 'q' to quit, 'd' to deallocate a process: ", current_time);
        fgets(input, sizeof(input), stdin);
        
        if (input[0] == 'q' || input[0] == 'Q') {
            break;
        } else if (input[0] == 'd' || input[0] == 'D') {
            int pid;
            printf("Enter process ID to deallocate: ");
            fgets(input, sizeof(input), stdin);
            pid = atoi(input);
            deallocate_memory(pid);
            display_memory_state();
        } else {
            simulate_time_step();
            display_allocated_processes();
        }
    }
    
    printf("\nSimulation complete.\n");
    
    // Final memory state
    display_memory_state();
    
    // Free all allocated memory
    free_memory();
    
    return 0;
}