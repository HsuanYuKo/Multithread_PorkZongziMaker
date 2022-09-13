#include <iostream>
#include <pthread.h>
#include <semaphore.h>
#include <unistd.h>
#include <queue>
#include <chrono>
#include <string> 
#include <vector>

using namespace std;
using namespace std::chrono;

#define ORIGIN  0
#define CUTTED  1
#define PACKED  2

struct Pork {
    int id;
    int status;
    steady_clock::time_point release_time;
};

int cutted_pork_cnt = 0;
int packed_pork_cnt = 0;
int PORK_CNT = 10; // 10;
int SLOT_MAX_CNT = 5; // 5;

const steady_clock::time_point START = steady_clock::now();
pthread_mutex_t print_mutex;
pthread_mutex_t slot_mutex;
pthread_mutex_t fridge_mutex;
pthread_mutex_t working_mutex;
queue<Pork> origin_slot;
queue<Pork> cutted_slot;
vector<Pork> fridge;
bool cutter_working = false;
bool packer_working = false;

void wait(int);
void print(string);
void* porkGenerator(void*);
void* cutter(void*);
void* packer(void*);
void* freezer(void*);

int main(int argc, char* argv[]) {
    PORK_CNT = atoi(argv[1]); // set pork count
    SLOT_MAX_CNT = atoi(argv[2]); //set slot count

    srand(0);
    pthread_t porkGenerate_thd; //generate pork
    pthread_t cutter_thd; //do the cutting
    pthread_t packer_thd; //do the packing
    pthread_t freezer_thd; //do the freezing
    pthread_attr_t attr; // set of attributes for the thread

    pthread_attr_init(&attr); // get the default attributes
    pthread_mutex_init(&print_mutex, 0);
    pthread_mutex_init(&slot_mutex, 0);
    pthread_mutex_init(&fridge_mutex, 0);
    pthread_mutex_init(&working_mutex, 0);

    pthread_create(&porkGenerate_thd, &attr, &porkGenerator, NULL);
    pthread_create(&cutter_thd, &attr, &cutter, NULL);
    pthread_create(&packer_thd, &attr, &packer, NULL);
    pthread_create(&freezer_thd, &attr, &freezer, NULL);

    pthread_join(porkGenerate_thd, NULL);
    pthread_join(cutter_thd, NULL);
    pthread_join(packer_thd, NULL);
    pthread_join(freezer_thd, NULL);

    pthread_mutex_destroy(&working_mutex);
    pthread_mutex_destroy(&fridge_mutex);
    pthread_mutex_destroy(&slot_mutex);
    pthread_mutex_destroy(&print_mutex);
    return 0;
}

void wait(int millisecond) {
    usleep(millisecond * 1000);
}

void print(string output) {
    pthread_mutex_lock(&print_mutex);
    cout << output << endl;
    pthread_mutex_unlock(&print_mutex);
}

void* porkGenerator(void* param) {
    string output;
    long duration_ms;

    for (int i = 1; i <= PORK_CNT; i++) {
        int generate_ms = ((rand() % 6) + 5) * 10; //50~100ms
        wait(generate_ms);

        pthread_mutex_lock(&slot_mutex); //slot mutex lock    
        Pork p = { i, ORIGIN };
        if (origin_slot.size() + cutted_slot.size() < SLOT_MAX_CNT) { //if slot is not filled
            duration_ms = duration_cast<milliseconds>(steady_clock::now() - START).count();
            output = to_string(duration_ms) + "ms -- Pork#" + to_string(p.id) + ": waiting in the slot";
            print(output);
            origin_slot.push(p); //put into slot
        }
        else {
            int freeze_ms = ((rand() % 21) + 30) * 10; //slot filled, freeze 300~500ms
            duration_ms = duration_cast<milliseconds>(steady_clock::now() - START).count();
            output = to_string(duration_ms) + "ms -- Pork#" + to_string(p.id) + \
                ": has been sent to the Freezer - " + to_string(freeze_ms) + "ms";
            print(output);

            pthread_mutex_lock(&fridge_mutex); //fridge mutex lock
            p.release_time = steady_clock::now() + milliseconds(freeze_ms); //when to release
            fridge.push_back(p); //freeze the pork
            pthread_mutex_unlock(&fridge_mutex); //fridge mutex unlock
        }
        pthread_mutex_unlock(&slot_mutex); //slot mutex unlock
    }

    return NULL;
}

void* cutter(void* param) {
    bool finised = false;
    Pork pork_template = { -1, -1 };
    Pork pork_being_cut = pork_template;
    string output;
    long duration_ms;
    while (!finised) {
        int sleep_ms = ((rand() % 10) + 1) * 10;  //10~100ms
        int cut_ms = ((rand() % 21) + 10) * 10; //in cutter wait 100~300ms

        if (pork_being_cut.id == -1) {
            pthread_mutex_lock(&slot_mutex); //slot mutex lock
            if (!origin_slot.size()) {
                pthread_mutex_unlock(&slot_mutex); //slot mutex unlock
                pthread_mutex_lock(&working_mutex); //working mutex lock
                cutter_working = false;
                duration_ms = duration_cast<milliseconds>(steady_clock::now() - START).count();
                if (packer_working) { //only cutter no work, so cutter under maintenance
                    output = to_string(duration_ms) + "ms -- CUTTER: under maintenance";
                }
                else { //cutter and packer both no work, both under reviewing
                    output = to_string(duration_ms) + "ms -- CUTTER: under reviewing together...";
                }
                print(output);
                wait(sleep_ms);
                pthread_mutex_unlock(&working_mutex); //slot mutex unlock
                continue;
            }
            pork_being_cut = origin_slot.front();
            origin_slot.pop(); //move away from slot, ready go to cutter
            pthread_mutex_unlock(&slot_mutex); //slot mutex unlock

            pthread_mutex_lock(&working_mutex); //working mutex lock
            cutter_working = true;
            pthread_mutex_unlock(&working_mutex); //working mutex unlock
            duration_ms = duration_cast<milliseconds>(steady_clock::now() - START).count();
            output = to_string(duration_ms) + "ms -- Pork#" + to_string(pork_being_cut.id) + ": enters the CUTTER"; //pork into cutter
            print(output);
        }
        duration_ms = duration_cast<milliseconds>(steady_clock::now() - START).count();
        output = to_string(duration_ms) + "ms -- CUTTER: cutting... cutting... Pork#" + to_string(pork_being_cut.id) + " -- " + to_string(cut_ms) + "ms"; //cutting pork
        print(output);
        wait(cut_ms);

        pork_being_cut.status = CUTTED; //pork cutted
        while (true) {
            pthread_mutex_lock(&slot_mutex); //slot mutex lock
            if (origin_slot.size() + cutted_slot.size() < SLOT_MAX_CNT) { //if slot has space
                cutted_slot.push(pork_being_cut); //pork being cutted and put back to slot
                duration_ms = duration_cast<milliseconds>(steady_clock::now() - START).count();
                output = to_string(duration_ms) + "ms -- Pork#" + to_string(pork_being_cut.id) + ": leaves CUTTER (complete 1st stage)";
                print(output);
                output = to_string(duration_ms) + "ms -- Pork#" + to_string(pork_being_cut.id) + ": waiting in the slot (cutted)";
                print(output);
                cutted_pork_cnt++;
                pork_being_cut = pork_template;
                pthread_mutex_unlock(&slot_mutex); //slot mutex unlock
                break;
            }
            else if (origin_slot.size() > 0) { //if there are still some pork in origin slot
                Pork temp = origin_slot.front();
                origin_slot.pop(); //take out one pork and go to cutter
                duration_ms = duration_cast<milliseconds>(steady_clock::now() - START).count();
                output = to_string(duration_ms) + "ms -- Pork#" + to_string(temp.id) + ": enters the CUTTER"; //cutting pork
                print(output);
                cutted_slot.push(pork_being_cut); //pork being cutted and put back to slot
                output = to_string(duration_ms) + "ms -- Pork#" + to_string(pork_being_cut.id) + ": leaves CUTTER (complete 1st stage)";
                print(output);
                output = to_string(duration_ms) + "ms -- Pork#" + to_string(pork_being_cut.id) + ": waiting in the slot (cutted)";
                print(output);
                cutted_pork_cnt++;
                pork_being_cut = temp;
                pthread_mutex_unlock(&slot_mutex); //slot mutex unlock
                break;
            }
            else {
                pthread_mutex_unlock(&slot_mutex); //slot mutex unlock
                wait(sleep_ms);
                continue;
            }
        }

        if (cutted_pork_cnt == PORK_CNT) {
            finised = true;
        }

        pthread_mutex_lock(&working_mutex); //working mutex lock
        cutter_working = false;
        pthread_mutex_unlock(&working_mutex); //working mutex unlock
        wait(sleep_ms);
    }
    pthread_mutex_lock(&working_mutex); //working mutex lock
    cutter_working = true;
    pthread_mutex_unlock(&working_mutex); //working mutex unlock

    return NULL;
}

void* packer(void* param) {
    bool finised = false;
    string output;
    long duration_ms;

    while (!finised) {
        int sleep_ms = ((rand() % 10) + 1) * 10; //10~100ms
        int pack_ms = ((rand() % 51) + 50) * 10; //500~1000ms

        pthread_mutex_lock(&slot_mutex);  //slot mutex lock
        if (!cutted_slot.size()) { //if no slot is cutted
            pthread_mutex_unlock(&slot_mutex);//slot mutex unlock
            pthread_mutex_lock(&working_mutex); //working mutex lock
            duration_ms = duration_cast<milliseconds>(steady_clock::now() - START).count();
            if (cutter_working) { //only packer no work, so packer under maintenance
                output = to_string(duration_ms) + "ms -- PACKER: under maintenance";
            }
            else { //cutter and packer both no work, both under reviewing
                output = to_string(duration_ms) + "ms -- PACKER: under reviewing together...";
            }
            print(output);
            wait(sleep_ms);
            pthread_mutex_unlock(&working_mutex); //working mutex unlock
            continue;
        }
        Pork p = cutted_slot.front();
        cutted_slot.pop(); //take out one cutted pork and go to packer
        pthread_mutex_unlock(&slot_mutex); //slot mutex unlock

        pthread_mutex_lock(&working_mutex); //working mutex lock
        packer_working = true;
        pthread_mutex_unlock(&working_mutex); //working mutex unlock

        duration_ms = duration_cast<milliseconds>(steady_clock::now() - START).count();
        output = to_string(duration_ms) + "ms -- Pork#" + to_string(p.id) + ": enters the PACKER";
        print(output);
        output = to_string(duration_ms) + "ms -- PACKER: processing & packing Pork#" + to_string(p.id) + \
            " -- " + to_string(pack_ms) + "ms";
        print(output);
        wait(pack_ms);

        p.status = PACKED;
        packed_pork_cnt++;

        duration_ms = duration_cast<milliseconds>(steady_clock::now() - START).count();
        output = to_string(duration_ms) + "ms -- Pork#" + to_string(p.id) + ": leaves PACKER (Complete)";
        print(output);

        if (packed_pork_cnt == PORK_CNT) { //if all pork was packed, then finish
            finised = true;
        }
        pthread_mutex_lock(&working_mutex); //working mutex lock
        packer_working = false;
        pthread_mutex_unlock(&working_mutex); //working mutex unlock
        wait(sleep_ms);
    }
    pthread_mutex_lock(&working_mutex); //working mutex lock
    packer_working = true;
    pthread_mutex_unlock(&working_mutex); //working mutex unlock
    return NULL;
}

void* freezer(void* param) {
    bool finised = false;
    int sleep_ms = ((rand() % 10) + 1) * 10; //10~100ms
    string output;
    long duration_ms;

    while (!finised) {
        vector<int> index;
        pthread_mutex_lock(&fridge_mutex); //fridge mutex lock
        for (int i = 0; i < fridge.size(); i++) {
            if (fridge[i].release_time <= steady_clock::now()) { //if freeze time over
                pthread_mutex_lock(&slot_mutex); //slot mutex lock
                if (origin_slot.size() + cutted_slot.size() < SLOT_MAX_CNT) { //slot has space
                    duration_ms = duration_cast<milliseconds>(steady_clock::now() - START).count();
                    output = to_string(duration_ms) + "ms -- Pork#" + to_string(fridge[i].id) + ": waiting in the slot";
                    print(output);
                    origin_slot.push(fridge[i]); //put back to slot
                    pthread_mutex_unlock(&slot_mutex); //slot mutex unlock
                    index.push_back(i);
                }
                else { //slot has no space
                    pthread_mutex_unlock(&slot_mutex); //slot mutex unlock
                    int freeze_ms = ((rand() % 21) + 30) * 10; //300~500ms
                    fridge[i].release_time += milliseconds(freeze_ms); //freeze again
                    duration_ms = duration_cast<milliseconds>(steady_clock::now() - START).count();
                    output = to_string(duration_ms) + "ms -- Pork#" + to_string(fridge[i].id) + \
                        " has been sent to the Freezer - " + to_string(freeze_ms) + "ms";
                    print(output);
                }
            }
        }
        if (cutted_pork_cnt == PORK_CNT) {
            finised = true;
        }
        for (int i = 0; i < index.size(); i++) {
            fridge.erase(fridge.begin() + index[i]); //move away the pork which is not in fridge
        }
        pthread_mutex_unlock(&fridge_mutex); //fridge mutex unlock
        wait(sleep_ms);
    }
    return NULL;
}

