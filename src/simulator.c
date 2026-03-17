#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <pthread.h>
#include <semaphore.h>
#include <stdbool.h>
#include <unistd.h>
#include <time.h>
#include <sys/time.h> 

#define TOTAL_OVERS 20

// all global variables here

int Global_Score = 0;
int pitch_status = 0;
bool ball_in_air = false;
bool match_over = false;

pthread_mutex_t pitch_mutex = PTHREAD_MUTEX_INITIALIZER;
pthread_mutex_t score_mutex = PTHREAD_MUTEX_INITIALIZER;
pthread_cond_t bowler_cond = PTHREAD_COND_INITIALIZER;
pthread_cond_t batsman_cond = PTHREAD_COND_INITIALIZER;
pthread_cond_t fielder_cond = PTHREAD_COND_INITIALIZER;
sem_t *crease_sem;

FILE *log_file;
int wickets_fallen = 0;


// strike and scheduling variables here

int striker_id = 0;
int non_striker_id = 0;
int current_over = 1;
bool delivery_was_extra = false;

int rr_schedule[20] = {1, 2, 3, 4, 5, 6, 1, 2, 3, 4, 5, 6, 1, 2, 3, 4, 5, 6, 1, 2};
int current_bowler_id = 1;

int bowler_balls[7] = {0};
int bowler_runs[7] = {0};
int bowler_wickets[7] = {0};
int total_extras = 0;

pthread_cond_t umpire_cond = PTHREAD_COND_INITIALIZER;
int Allocation[2][2] = {{0,0}, {0,0}};
int Request[2][2] = {{0,0}, {0,0}};
int run_out_victim = 0;

void initialize_primitives() {
    sem_unlink("/crease_sem");
    crease_sem = sem_open("/crease_sem", O_CREAT, 0644, 2); 
    printf("[KERNEL] Synchronization primitives initialized.\n");
}

void cleanup_primitives() {
    sem_close(crease_sem);
    sem_unlink("/crease_sem");
}

typedef struct {
    int id;
    int burst_time; 
    double arrival_time;
    double start_time;
    double wait_time;
} BatsmanData;

BatsmanData team[11];

double get_current_time_ms(){
    struct timeval tv;
    gettimeofday(&tv, NULL);
    return (tv.tv_sec * 1000.0) + (tv.tv_usec / 1000.0);
}

void apply_sjf_scheduling(BatsmanData arr[], int n) {
    for(int i=0; i < n-1; i++){
        for (int j=0; j < n-i-1; j++) {
            if(arr[j].burst_time > arr[j+1].burst_time){
                BatsmanData temp = arr[j];
                arr[j] = arr[j+1];
                arr[j+1] = temp;
            }
        }
    }
    printf("[Scheduler] Applied SJF. Tail-enders will bat first.\n");
}

int simulate_delivery() {
    int r = rand() % 100;
    if (r < 3) return -3;       // 3%  wide
    if (r < 6) return -4;       // 3%  no Ball
    if (r < 31) return 0;       // 25% dot ball
    if (r < 66) return 1;       // 35% 1 run
    if (r < 81) return 2;       // 15% 2 runs
    if (r < 84) return 3;       // 3%  3 runs
    if (r < 91) return 4;       // 7%  4 runs
    if (r < 94) return 6;       // 3%  6 runs
    if (r < 97) return -1;      // 3%  bowled
    return -2;                  // 3%  catch out

    // below we added additional 5% prob of run out if 1,2,3 runs taken
    // so run out prob = 5% of 53% = 2.65% approx
}


// 3rd umpire for deadlock detector

void* umpire_thread_func(void* arg) {
    while (!match_over) {
        pthread_mutex_lock(&pitch_mutex);
        
        while (Allocation[0][0] == 0 && !match_over) {
            pthread_cond_wait(&umpire_cond, &pitch_mutex);
        }
        if (match_over) {
            pthread_mutex_unlock(&pitch_mutex);
            break;
        }
        
        if (Request[0][1] == 1 && Allocation[1][1] == 1 && Request[1][0] == 1 && Allocation[0][0] == 1) {
            fprintf(log_file, "\n[Third Umpire] DEADLOCK DETECTED! Circular Wait between Batsmen %d and %d.\n", striker_id, non_striker_id);
            printf("\n[Third Umpire] DEADLOCK DETECTED! Circular Wait between Batsmen %d and %d.\n", striker_id, non_striker_id);
            
            int victim = (rand() % 2 == 0) ? striker_id : non_striker_id;
            run_out_victim = victim;
            
            Allocation[0][0] = 0; Request[0][1] = 0;
            Allocation[1][1] = 0; Request[1][0] = 0;
            
            pitch_status = 0; 
            pthread_cond_broadcast(&batsman_cond); 
            pthread_cond_broadcast(&bowler_cond); 
        }
        pthread_mutex_unlock(&pitch_mutex);
    }
    return NULL;
}


// thread funcitons

void* bowler(void* arg) {
    int bowler_id = *((int*)arg);
    
    while (!match_over) {
        pthread_mutex_lock(&pitch_mutex);
        
        while ((current_bowler_id != bowler_id || striker_id == 0 || non_striker_id == 0) && !match_over) {
            pthread_cond_wait(&bowler_cond, &pitch_mutex);
        }
        
        if (match_over || wickets_fallen >= 10 || current_over > TOTAL_OVERS) {
            pthread_mutex_unlock(&pitch_mutex);
            break;
        }
        
        int legal_deliveries = 0;
        
        while (legal_deliveries < 6 && !match_over && wickets_fallen < 10) {
            while((pitch_status == 1 || striker_id == 0 || non_striker_id == 0) && !match_over) {
                pthread_cond_wait(&bowler_cond, &pitch_mutex);
            }
            if (match_over || wickets_fallen >= 10) break;
            
            delivery_was_extra = false;
            pitch_status = 1;
            
            fprintf(log_file, "[Bowler %d] delivering Over %d, Ball %d...\n", bowler_id, current_over, legal_deliveries + 1);
            printf("[Bowler %d] delivering Over %d, Ball %d...\n", bowler_id, current_over, legal_deliveries + 1);
            
            pthread_cond_broadcast(&batsman_cond); 
            
            while (pitch_status == 1 && !match_over) {
                pthread_cond_wait(&bowler_cond, &pitch_mutex);
            }
            
            if (match_over || wickets_fallen >= 10) {
                if (!delivery_was_extra) {
                    legal_deliveries++;
                    bowler_balls[current_bowler_id]++;
                }
                break;
            }
            
            if (!delivery_was_extra) {
                legal_deliveries++;
                bowler_balls[current_bowler_id]++;
            }
        }

        if (match_over || wickets_fallen >= 10) {
            pthread_mutex_unlock(&pitch_mutex);
            break;
        }
        
        int temp = striker_id;
        striker_id = non_striker_id;
        non_striker_id = temp;
        
        if(current_over != TOTAL_OVERS){
            fprintf(log_file, "[Umpire] Over %d complete! Strike rotated.\n", current_over);
            printf("[Umpire] Over %d complete! Strike rotated.\n", current_over);
        }
        else{
            fprintf(log_file, "[Umpire] Over %d complete! Match ends.\n", current_over);
            printf("[Umpire] Over %d complete! Match ends.\n", current_over);
        }
        current_over++;
        if(current_over > TOTAL_OVERS){
            match_over = true;
            pthread_cond_broadcast(&batsman_cond);
            pthread_cond_broadcast(&fielder_cond);
            pthread_cond_broadcast(&bowler_cond);
            pthread_cond_broadcast(&umpire_cond);
        } 
        else{
            current_bowler_id = rr_schedule[current_over - 1]; 
            fprintf(log_file, "[Umpire] Context switch! Bowler %d loading...\n", current_bowler_id);
            printf("[Umpire] Context switch! Bowler %d loading...\n", current_bowler_id);
            pthread_cond_broadcast(&bowler_cond); 
            pthread_cond_broadcast(&batsman_cond);
        }
        pthread_mutex_unlock(&pitch_mutex);
        usleep(10000); 
    }
    return NULL;
}

void* batsman(void* arg) {
    BatsmanData *data = (BatsmanData*)arg;
    
    sem_wait(crease_sem);
    if(match_over){
        sem_post(crease_sem);
        return NULL;
    }
    
    data->start_time = get_current_time_ms();
    data->wait_time = data->start_time - data->arrival_time;
    
    pthread_mutex_lock(&pitch_mutex);
    if (striker_id == 0) striker_id = data->id;
    else non_striker_id = data->id;
    
    fprintf(log_file, "[Batsman %d] entered the crease. Wait Time: %.2f ms\n", data->id, data->wait_time);
    printf("[Batsman %d] entered the crease. Wait Time: %.2f ms\n", data->id, data->wait_time);
    
    if(striker_id != 0 && non_striker_id != 0){
        pthread_cond_broadcast(&bowler_cond); 
    }
    pthread_mutex_unlock(&pitch_mutex);
    
    bool is_out = false;

    while(!match_over && !is_out){
        pthread_mutex_lock(&pitch_mutex);
        
        while((pitch_status == 0 || striker_id != data->id) && !match_over && !is_out && run_out_victim != data->id) {
            pthread_cond_wait(&batsman_cond, &pitch_mutex);
        }
        
        if(run_out_victim == data->id){
            wickets_fallen++;
            is_out = true;
            
            pthread_mutex_lock(&score_mutex);
            bowler_wickets[current_bowler_id]++;
            pthread_mutex_unlock(&score_mutex);
            
            fprintf(log_file, "!! [Batsman %d] is RUN OUT! (Total: %d/%d) !!\n", data->id, Global_Score, wickets_fallen);
            printf("!! [Batsman %d] is RUN OUT! (Total: %d/%d) !!\n", data->id, Global_Score, wickets_fallen);
            
            if(striker_id == data->id) striker_id = 0;
            else non_striker_id = 0;
            
            run_out_victim = 0; 
            
            if(wickets_fallen >= 10){
                match_over = true;
                pthread_cond_broadcast(&bowler_cond);
                pthread_cond_broadcast(&fielder_cond);
                pthread_cond_broadcast(&batsman_cond);
                pthread_cond_broadcast(&umpire_cond);
            } 
            else pthread_cond_broadcast(&bowler_cond); 

            pthread_mutex_unlock(&pitch_mutex);
            break;
        }

        if(match_over || is_out){
            pthread_mutex_unlock(&pitch_mutex);
            break;
        }
        
        int outcome = simulate_delivery(); 
        
        pthread_mutex_lock(&score_mutex);
        if(outcome == -3 || outcome == -4){
            Global_Score++;
            total_extras++;
            bowler_runs[current_bowler_id]++;
            delivery_was_extra = true; 
            
            char extra_type[10];
            strcpy(extra_type, (outcome == -3) ? "WIDE" : "NO-BALL");
            
            fprintf(log_file, "--- %s! Extra run added. Ball must be re-bowled. (Total: %d/%d)\n", extra_type, Global_Score, wickets_fallen);
            printf("--- %s! Extra run added. Ball must be re-bowled. (Total: %d/%d)\n", extra_type, Global_Score, wickets_fallen);
            
            pitch_status = 0;
            pthread_cond_broadcast(&batsman_cond); 
            pthread_cond_broadcast(&bowler_cond); 
            pthread_mutex_unlock(&score_mutex);
            pthread_mutex_unlock(&pitch_mutex);
            continue;
        }
        
        delivery_was_extra = false;
        
        if(outcome >= 0){
            Global_Score += outcome;
            bowler_runs[current_bowler_id] += outcome;
            
            // ball by ball logs

            if(outcome == 0){
                fprintf(log_file, "[Batsman %d] played a DOT ball. (Total: %d/%d)\n", data->id, Global_Score, wickets_fallen);
                printf("[Batsman %d] played a DOT ball. (Total: %d/%d)\n", data->id, Global_Score, wickets_fallen);
            } 
            else{
                fprintf(log_file, "[Batsman %d] hit for %d runs! (Total: %d/%d)\n", data->id, outcome, Global_Score, wickets_fallen);
                printf("[Batsman %d] hit for %d runs! (Total: %d/%d)\n", data->id, outcome, Global_Score, wickets_fallen);
            }
            
            if(outcome == 1 || outcome == 3){
                int temp = striker_id;
                striker_id = non_striker_id;
                non_striker_id = temp;
            }
            pthread_mutex_unlock(&score_mutex);

            if((outcome == 1 || outcome == 2 || outcome == 3) && (rand() % 100 < 5)) {
                Allocation[0][0] = 1; Request[0][1] = 1; 
                Allocation[1][1] = 1; Request[1][0] = 1; 
                
                fprintf(log_file, "--- MIX UP! Batsmen %d and %d running to the same end! ---\n", striker_id, non_striker_id);
                printf("--- MIX UP! Batsmen %d and %d running to the same end! ---\n", striker_id, non_striker_id);
                
                pthread_cond_signal(&umpire_cond);
                
                while(Allocation[0][0] != 0 && !match_over) {
                    pthread_cond_wait(&batsman_cond, &pitch_mutex);
                }
                pthread_mutex_unlock(&pitch_mutex);
                continue; 
            }
            
            pitch_status = 0; 
            pthread_cond_broadcast(&batsman_cond); 
            pthread_cond_broadcast(&bowler_cond);     
            
        } 
        else{
            wickets_fallen++;
            is_out = true;
            bowler_wickets[current_bowler_id]++;
            pthread_mutex_unlock(&score_mutex);
            
            // wicket commentary here
            if(outcome == -1){
                fprintf(log_file, "!! [Batsman %d] is BOWLED! !!\n", data->id);
                printf("!! [Batsman %d] is BOWLED! !!\n", data->id);
                pitch_status = 0;
                striker_id = 0;
                pthread_cond_broadcast(&bowler_cond);
            } 
            else if(outcome == -2){
                fprintf(log_file, "[Batsman %d] hit the ball high in the air...\n", data->id);
                printf("[Batsman %d] hit the ball high in the air...\n", data->id);
                ball_in_air = true;
                striker_id = 0;
                pthread_cond_broadcast(&fielder_cond);
            }

            if(wickets_fallen >= 10){
                match_over = true;
                pthread_cond_broadcast(&bowler_cond);
                pthread_cond_broadcast(&fielder_cond);
                pthread_cond_broadcast(&batsman_cond);
                pthread_cond_broadcast(&umpire_cond);
            }
        }
        pthread_mutex_unlock(&pitch_mutex);
        usleep(50000); 
    }
    
    if (is_out || (match_over && striker_id != 0 && non_striker_id != 0)) {
        fprintf(log_file, "[Batsman %d] walks back to the pavilion.\n", data->id);
        printf("[Batsman %d] walks back to the pavilion.\n", data->id);
    }
    
    sem_post(crease_sem);
    return NULL;
}

void* fielder(void* arg) {
    int fielder_id = *((int*)arg);
    
    while (1) {
        pthread_mutex_lock(&pitch_mutex);
        
        // sleeping if no ball in air and match ongoing
        while(!ball_in_air && !match_over){
            pthread_cond_wait(&fielder_cond, &pitch_mutex);
        }
        
        if(match_over && !ball_in_air){
            pthread_mutex_unlock(&pitch_mutex);
            break;
        }

        if(ball_in_air){
            fprintf(log_file, "!!! [Fielder %d] takes a spectacular CATCH! !!!\n", fielder_id);
            printf("!!! [Fielder %d] takes a spectacular CATCH! !!!\n", fielder_id);
            
            ball_in_air = false;
            pitch_status = 0;
            
            if(wickets_fallen >= 10){
                match_over = true;
                pthread_cond_broadcast(&bowler_cond);
                pthread_cond_broadcast(&batsman_cond);
                pthread_cond_broadcast(&umpire_cond);
            }
            else pthread_cond_broadcast(&bowler_cond); 
        }
        pthread_mutex_unlock(&pitch_mutex);
    }
    return NULL;
}


// main func

int main() {
    log_file = fopen("../logs/match_log.txt", "w");

    srand(time(NULL));
    initialize_primitives();
    
    pthread_t bowler_threads[6];
    pthread_t batsman_threads[11];
    pthread_t fielder_threads[10];
    pthread_t umpire_thread;
    
    int bowler_ids[6] = {1, 2, 3, 4, 5, 6};
    int fielder_ids[10] = {1, 2, 3, 4, 5, 6, 7, 8, 9, 10}; 

    int burst_estimates[11] = {50, 45, 60, 40, 30, 25, 20, 10, 8, 5, 2};
    for(int i=0; i<11; i++){
        team[i].id = i+1;
        team[i].burst_time = burst_estimates[i];
    }

    apply_sjf_scheduling(team, 11);

    pthread_create(&umpire_thread, NULL, umpire_thread_func, NULL);
    for(int i=0; i<10; i++) pthread_create(&fielder_threads[i], NULL, fielder, &fielder_ids[i]);
    
    double simulation_start = get_current_time_ms();
    for(int i=0; i<11; i++){
        team[i].arrival_time = simulation_start; 
        pthread_create(&batsman_threads[i], NULL, batsman, &team[i]);
        usleep(2000);
    }
    
    usleep(50000); 
    for(int i=0; i<6; i++){
        pthread_create(&bowler_threads[i], NULL, bowler, &bowler_ids[i]);
    }

    for(int i=0; i<6; i++) pthread_join(bowler_threads[i], NULL);
    pthread_join(umpire_thread, NULL);
    for(int i=0; i<11; i++) pthread_join(batsman_threads[i], NULL);
    for(int i=0; i<10; i++) pthread_join(fielder_threads[i], NULL);

    cleanup_primitives();
    pthread_cond_destroy(&umpire_cond);

    // final output and summary

    fprintf(log_file, "\n--- Innings Concluded. Final Score: %d/%d ---\n", Global_Score, wickets_fallen);
    printf("\n--- Innings Concluded. Final Score: %d/%d ---\n", Global_Score, wickets_fallen);
    
    fprintf(log_file, "\n--- Bowler Summary ---\n");
    printf("\n--- Bowler Summary ---\n");
    for(int i=1; i<=6; i++){
        int overs = (bowler_balls[i] + 5)/6;
        int rem = bowler_balls[i]%6;
        if(rem%6 == 0){
            fprintf(log_file, "Bowler %d = %d overs , %d/%d\n", i, overs, bowler_runs[i], bowler_wickets[i]);
            printf("Bowler %d = %d overs , %d/%d\n", i, overs, bowler_runs[i], bowler_wickets[i]);
        }
        else{
            fprintf(log_file, "Bowler %d = %d.%d overs , %d/%d\n", i, overs, rem, bowler_runs[i], bowler_wickets[i]);
            printf("Bowler %d = %d overs , %d/%d\n", i, overs, bowler_runs[i], bowler_wickets[i]);
        }
    }
    
    fprintf(log_file, "Extras = %d\n", total_extras);
    printf("Extras = %d\n", total_extras);
    
    fprintf(log_file, "\n--- Wait Time Analysis ---\n");
    printf("\n--- Wait Time Analysis ---\n");
    for(int i=0; i<11; i++){
        if(team[i].id == 6 || team[i].id == 7) {
            fprintf(log_file, "Middle Order [Batsman %d] Wait Time: %.2f ms\n", team[i].id, team[i].wait_time);
            printf("Middle Order [Batsman %d] Wait Time: %.2f ms\n", team[i].id, team[i].wait_time);
        }
    }

    fclose(log_file);
    return EXIT_SUCCESS;
}