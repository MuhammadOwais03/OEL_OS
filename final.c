#include <pthread.h>
#include <stdio.h>
#include <stdlib.h>
#include <unistd.h> // for sleep()

#define MEMORY_SIZE 1024
#define MAX_PROCESSES 20

typedef struct {
    int id;
    int size;
    int allocated;
    int start_address;
    int arrival_time;
    int execution_time;
    int remaining_time;
} Process;

typedef struct FreeBlock {
    int start;
    int size;
    struct FreeBlock *next;
} FreeBlock;

// Global Variables
Process processes[MAX_PROCESSES];
Process waiting_queue[MAX_PROCESSES];
int waiting_count = 0;
FreeBlock *freeList = NULL;
int total_used_memory = 0;

void initialize_memory();
void allocate_memory(Process *p, int num_processes);
void deallocate_memory(int process_id, int num_processes);
void display_memory_state();
void display_process_table(int num_processes);
void save_memory_state();
void merge_free_blocks();
void calculate_process_stats(int num_processes);
void tick(int *num_processes);
void display_waiting_queue();
void* clock_tick_thread();
int process_entry_number = 1;



int main() {
    int num_processes = 0;
    initialize_memory();
    int choice;
    // int clock_tick_counter = 0;
    pthread_t tid;
    pthread_create(&tid, NULL, clock_tick_thread, (void *)&num_processes);

    printf("\n--- Welcome to Dynamic Partitioning Memory Manager ---\n");

    while (1) {
        // printf("\n==============================\n");
        // printf("🕰️  Clock Tick: %d\n", clock_tick_counter);
        // printf("==============================\n");
        printf("\nChoose an option:\n");
        printf("1. Add New Process\n");
        printf("2. Show Process Table\n");
        printf("3. Show Waiting Queue\n");
        printf("4. Show Memory Statistics\n");
        printf("5. Exit\n");
        printf("Enter your choice: ");
        scanf("%d", &choice);

        // sleep(1);  // simulate time passing
        // tick(&num_processes);
        // clock_tick_counter++;

        switch (choice) {
            case 1: {
                if (total_used_memory < MEMORY_SIZE) {
                    printf("\nEnter process ID, memory size (KB), arrival time, and execution time for process %d (or enter -1 to cancel): ", process_entry_number);
                    int id, size, arrival_time, execution_time;
                    scanf("%d", &id);
                    if (id == -1) break;
                    scanf("%d %d %d", &size, &arrival_time, &execution_time);

                    if (size > (MEMORY_SIZE - total_used_memory)) {
                        printf("Memory full! Process %d is added to waiting queue.\n", id);
                        waiting_queue[waiting_count].id = id;
                        waiting_queue[waiting_count].size = size;
                        waiting_queue[waiting_count].arrival_time = arrival_time;
                        waiting_queue[waiting_count].execution_time = execution_time;
                        waiting_queue[waiting_count].remaining_time = execution_time;
                        waiting_queue[waiting_count].allocated = 0;
                        waiting_queue[waiting_count].start_address = -1;
                        waiting_count++;
                        process_entry_number++;
                        break;
                    }

                    processes[num_processes].id = id;
                    processes[num_processes].size = size;
                    processes[num_processes].arrival_time = arrival_time;
                    processes[num_processes].execution_time = execution_time;
                    processes[num_processes].remaining_time = execution_time;
                    processes[num_processes].allocated = 0;
                    processes[num_processes].start_address = -1;

                    allocate_memory(&processes[num_processes], num_processes);
                    total_used_memory += size;
                    calculate_process_stats(num_processes + 1);
                    num_processes++;
                    process_entry_number++;
                } else {
                    printf("Memory Full! Cannot add process right now.\n");
                }
                break;
            }
            case 2:
                display_process_table(num_processes);
                break;
            case 3:
                display_waiting_queue();
                break;
            case 4:
                calculate_process_stats(num_processes);
                break;
            case 5:
                printf("\nExiting Memory Manager. Final memory state:\n");
                display_memory_state();
                display_process_table(num_processes);
                exit(0);
            default:
                printf("Invalid choice. Please try again!\n");
        }
    }
}


// Functions

void initialize_memory() {
    freeList = (FreeBlock *)malloc(sizeof(FreeBlock));
    freeList->start = 0;
    freeList->size = MEMORY_SIZE;
    freeList->next = NULL;
}

void allocate_memory(Process *p, int num_processes) {
    FreeBlock *best_fit = NULL, *prev = NULL, *current = freeList, *best_prev = NULL;

    while (current) {
        if (current->size >= p->size) {
            if (!best_fit || current->size < best_fit->size) {
                best_fit = current;
                best_prev = prev;
            }
        }
        prev = current;
        current = current->next;
    }

    if (!best_fit) {
        printf("Process %d (Size: %d KB) cannot be allocated! Not enough memory.\n", p->id, p->size);
        return;
    }

    p->start_address = best_fit->start;
    p->allocated = 1;
    best_fit->start += p->size;
    best_fit->size -= p->size;

    if (best_fit->size == 0) {
        if (best_prev)
            best_prev->next = best_fit->next;
        else
            freeList = best_fit->next;
        free(best_fit);
    }

    printf("Process %d allocated at Address: %d KB\n", p->id, p->start_address);
    display_memory_state();
    save_memory_state();
    display_process_table(num_processes + 1);
}

void deallocate_memory(int process_id, int num_processes) {
    int found = 0, freed_size = 0, start_address = -1;

    for (int i = 0; i < num_processes; i++) {
        if (processes[i].id == process_id && processes[i].allocated) {
            start_address = processes[i].start_address;
            freed_size = processes[i].size;
            processes[i].allocated = 0;
            processes[i].start_address = -1;
            found = 1;
            break;
        }
    }

    if (!found) {
        printf("Process %d not found in memory.\n", process_id);
        return;
    }

    FreeBlock *new_block = (FreeBlock *)malloc(sizeof(FreeBlock));
    new_block->start = start_address;
    new_block->size = freed_size;
    new_block->next = freeList;
    freeList = new_block;

    printf("Process %d deallocated, Freed %d KB\n", process_id, freed_size);
    merge_free_blocks();
    display_memory_state();
    save_memory_state();
}

void merge_free_blocks() {
    FreeBlock *current = freeList, *next;
    while (current && current->next) {
        next = current->next;
        if (current->start + current->size == next->start) {
            current->size += next->size;
            current->next = next->next;
            free(next);
        } else {
            current = current->next;
        }
    }
}

void display_memory_state() {
    printf("\nCurrent Memory State:\n");
    FreeBlock *current = freeList;
    while (current) {
        printf("[ Free: %d KB at %d KB ] ", current->size, current->start);
        current = current->next;
    }
    printf("\n");
}

void display_process_table(int num_processes) {
    printf("\nProcess Table:\n");
    printf("+------------+----------+--------------+--------------+--------------+--------------+------------+\n");
    printf("| Process ID |  Size KB | Start Address | Arrival Time | Exec Time(s) | Remaining(s) | Allocated  |\n");
    printf("+------------+----------+--------------+--------------+--------------+--------------+------------+\n");

    for (int i = 0; i < num_processes; i++) {
        printf("| %10d | %8d | %12d | %12d | %12d | %12d | %10s |\n",
               processes[i].id,
               processes[i].size,
               processes[i].start_address,
               processes[i].arrival_time,
               processes[i].execution_time,
               processes[i].remaining_time,
               processes[i].allocated ? "YES" : "NO");
    }

    printf("+------------+----------+--------------+--------------+--------------+--------------+------------+\n");
}


void save_memory_state() {
    FILE *file = fopen("memory_state.txt", "w");
    if (!file) {
        printf("Error opening file!\n");
        return;
    }

    fprintf(file, "Memory State:\n");
    if (freeList == NULL) {
        fprintf(file, "No free memory available. All memory is allocated.\n");
    } else {
        FreeBlock *current = freeList;
        while (current) {
            fprintf(file, "[ Free: %d KB at %d KB ]\n", current->size, current->start);
            current = current->next;
        }
    }

    fclose(file);
    printf("Memory state saved to 'memory_state.txt'\n");
}

void calculate_process_stats(int num_processes) {
    if (num_processes == 0) return;

    int total_size = 0, min_size = 999999, max_size = 0;
    int used_memory = 0;
    int allocated_processes = 0;

    for (int i = 0; i < num_processes; i++) {
        if (processes[i].allocated) {
            total_size += processes[i].size;
            if (processes[i].size < min_size) min_size = processes[i].size;
            if (processes[i].size > max_size) max_size = processes[i].size;
            used_memory += processes[i].size;
            allocated_processes++;
        }
    }

    if (allocated_processes == 0) {
        min_size = 0;
        max_size = 0;
    }

    float avg_size = allocated_processes > 0 ? (float)total_size / allocated_processes : 0;

    printf("\nMemory Statistics:\n");
    printf("-- Average Process Size: %.2f KB\n", avg_size);
    printf("-- Min Process Size: %d KB\n", min_size);
    printf("-- Max Process Size: %d KB\n", max_size);
    printf("-- Total RAM Available: %d KB\n", MEMORY_SIZE);
    printf("-- Used Memory: %d KB\n", used_memory);
    printf("-- Free Memory: %d KB\n", MEMORY_SIZE - used_memory);
}

void tick(int *num_processes) {
    // Decrease execution time for processes inside memory
    for (int i = 0; i < *num_processes; i++) {
        if (processes[i].allocated && processes[i].remaining_time > 0) {
            processes[i].remaining_time--;
            if (processes[i].remaining_time == 0) {
                printf("⚡ Process %d finished execution!\n", processes[i].id);
                deallocate_memory(processes[i].id, *num_processes);
                total_used_memory -= processes[i].size;
                display_process_table(*num_processes);
            }
        }
    }

    // Try to allocate processes from waiting queue
    for (int i = 0; i < waiting_count; i++) {
        if (waiting_queue[i].size <= (MEMORY_SIZE - total_used_memory)) {
            printf("Moving Process %d from waiting queue into memory!\n", waiting_queue[i].id);

            // Move from waiting queue to processes array
            processes[*num_processes] = waiting_queue[i];
            allocate_memory(&processes[*num_processes], *num_processes);
            total_used_memory += waiting_queue[i].size;
            (*num_processes)++;

            // Shift waiting queue left
            for (int j = i; j < waiting_count - 1; j++) {
                waiting_queue[j] = waiting_queue[j + 1];
            }
            waiting_count--;
            i--; // adjust index
        }
    }
}


void display_waiting_queue() {
    printf("\nWaiting Queue:\n");
    printf("+------------+----------+--------------+--------------+--------------+\n");
    printf("| Process ID |  Size KB | Arrival Time  | Exec Time(s) | Remaining(s)  |\n");
    printf("+------------+----------+--------------+--------------+--------------+\n");

    for (int i = 0; i < waiting_count; i++) {
        printf("| %10d | %8d | %12d | %12d | %12d |\n",
               waiting_queue[i].id,
               waiting_queue[i].size,
               waiting_queue[i].arrival_time,
               waiting_queue[i].execution_time,
               waiting_queue[i].remaining_time);
    }

    printf("+------------+----------+--------------+--------------+--------------+\n");
}
void* clock_tick_thread(void* arg) {
    int *num_processes = (int *)arg;
    int clock_counter = 0;

    while (1) {
        sleep(1); // wait 1 second
        tick(num_processes);
        clock_counter++;
        // printf("\n🕰️  [Clock Tick %d Completed]\n", clock_counter);
    }
    return NULL;
}

