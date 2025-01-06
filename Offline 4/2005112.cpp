#include <iostream>
#include <thread>
#include <vector>
#include <mutex>
#include <semaphore.h>
#include <random>
#include <queue>
#include <condition_variable>
#include <unistd.h>
#include <chrono>

using namespace std;
using namespace chrono;

// Define ANSI color codes for log messages
#define COLOR_RESET "\033[0m"
#define COLOR_BLUE "\033[34m"
#define COLOR_GREEN "\033[32m"
#define COLOR_YELLOW "\033[33m"
#define COLOR_CYAN "\033[36m"
#define COLOR_MAGENTA "\033[35m"
#define COLOR_RED "\033[31m"
#define COLOR_BRIGHT_BLACK "\033[90m"   // Bright Black (Gray)
#define COLOR_BRIGHT_RED "\033[91m"     // Bright Red
#define COLOR_BRIGHT_GREEN "\033[92m"   // Bright Green
#define COLOR_BRIGHT_YELLOW "\033[93m"  // Bright Yellow
#define COLOR_BRIGHT_BLUE "\033[94m"    // Bright Blue
#define COLOR_BRIGHT_MAGENTA "\033[95m" // Bright Magenta
#define COLOR_BRIGHT_CYAN "\033[96m"    // Bright Cyan
#define COLOR_BRIGHT_WHITE "\033[97m"   // Bright White

// Global variables
int N, M;
int w, x, y, z;
sem_t gallery1, steps[3], corridorDE;
mutex printLock, stepLock1, stepLock2, stepLock3, timeLock, boothLock, rwLock, hallwayLock; // rwLock added for Task 3
condition_variable photoBoothCVforStandard, photoBoothCVforPremium;                         // Separate CVs for standard and premium visitors
queue<int> standardQueue, premiumQueue;
bool photoBoothOccupied = false;
int readersCount = 0; // Added for Task 3
vector<thread> visitors;
time_point<steady_clock> startTime;

// Function to generate a timestamp
int getTimestamp()
{
    timeLock.lock();
    auto now = steady_clock::now();
    int timestamp = duration_cast<seconds>(now - startTime).count();
    timeLock.unlock();
    return timestamp;
}

// Function to generate a random delay
int randomDelay(double lambda)
{
    random_device rd;
    mt19937 gen(rd());
    exponential_distribution<> d(lambda);
    return static_cast<int>(d(gen));
}

// Photo booth access logic
void accessPhotoBooth(int id, bool isPremium)
{
    unique_lock<mutex> lock(boothLock);

    // Log the visitor waiting for the photo booth
    printLock.lock();
    cout << (isPremium ? COLOR_BRIGHT_MAGENTA : COLOR_BRIGHT_YELLOW)
         << "Visitor " << id << " is about to enter the photo booth at timestamp "
         << getTimestamp() << COLOR_RESET << endl;
    printLock.unlock();

    // Wait for the photo booth to be available
    photoBoothCVforPremium.wait(lock, [&]()
                                {
        if (isPremium) return !photoBoothOccupied && readersCount == 0; // Premium visitor waits for full access
        return !photoBoothOccupied && premiumQueue.empty() && !standardQueue.empty() && standardQueue.front() == id; });

    // Occupy the photo booth
    photoBoothOccupied = true;

    // Remove the visitor from the appropriate queue
    if (isPremium)
    {
        if (!premiumQueue.empty())
        {
            premiumQueue.pop();
        }
    }
    else
    {
        if (!standardQueue.empty())
        {
            standardQueue.pop();
        }
    }

    // Log photo booth entry
    printLock.lock();
    cout << (isPremium ? COLOR_BRIGHT_MAGENTA : COLOR_BRIGHT_YELLOW) << "Visitor " << id
         << (isPremium ? " (Premium)" : " (Standard)") << " is inside the photo booth at timestamp "
         << getTimestamp() << COLOR_RESET << endl;
    printLock.unlock();

    // Simulate time in the photo booth
    this_thread::sleep_for(chrono::seconds(z));

    // Exit the photo booth
    photoBoothOccupied = false;
    if (premiumQueue.empty())
        photoBoothCVforStandard.notify_all();
    else
        photoBoothCVforPremium.notify_all();

    printLock.lock();
    cout << (isPremium ? COLOR_BRIGHT_MAGENTA : COLOR_BRIGHT_YELLOW) << "Visitor " << id
         << (isPremium ? " (Premium)" : " (Standard)") << " has exited the photo booth at timestamp "
         << getTimestamp() << COLOR_RESET << endl;
    printLock.unlock();
}

// Task 3 Reader-Writer Logic: Shared Access for Standard Visitors
void sharedPhotoBoothAccess(int id, bool isPremium)
{
    { // Reader entry
        unique_lock<mutex> lock(rwLock);

        printLock.lock();
        cout << (isPremium ? COLOR_BRIGHT_MAGENTA : COLOR_BRIGHT_YELLOW)
             << "Visitor " << id << " is about to enter the photo booth at timestamp "
             << getTimestamp() << COLOR_RESET << endl;
        printLock.unlock();
        // Wait for premium visitors (writers) to finish
        photoBoothCVforStandard.wait(lock, [&]()
                                     { return !photoBoothOccupied && premiumQueue.empty(); });
        if (readersCount < 2)
            readersCount++;
        if (readersCount == 2)
        {
            photoBoothOccupied = true;
        }
        if (readersCount == 1 && premiumQueue.empty())
        { // First reader locks the photo booth
            photoBoothOccupied = false;
            photoBoothCVforStandard.notify_all();
        }

        // Remove the visitor from the standard queue
        // if (!standardQueue.empty() && standardQueue.front() == id)
        // if (!standardQueue.empty())
        // {
        //     standardQueue.pop();
        // }
    }

    // Log shared photo booth entry
    printLock.lock();
    cout << COLOR_BRIGHT_YELLOW << "Visitor " << id << " (Standard) is sharing the photo booth at timestamp "
         << getTimestamp() << COLOR_RESET << endl;
    printLock.unlock();

    // Simulate time in the photo booth
    this_thread::sleep_for(chrono::seconds(z));

    { // Reader exit
        unique_lock<mutex> lock(rwLock);
        if (!standardQueue.empty())
        {
            standardQueue.pop();
        }
        readersCount--;
        if (readersCount == 0)
        { // Last reader unlocks the photo booth
            photoBoothOccupied = false;
            if (premiumQueue.empty())
                photoBoothCVforStandard.notify_all();
            else
                photoBoothCVforPremium.notify_all();
        }
    }

    // Log shared photo booth exit
    printLock.lock();
    cout << COLOR_BRIGHT_YELLOW << "Visitor " << id << " (Standard) has exited the shared photo booth at timestamp "
         << getTimestamp() << COLOR_RESET << endl;
    printLock.unlock();
}
// Visitor behavior
void visitorBehavior(int id)
{
    int timestamp;
    bool isPremium = (id >= 2001); // Determine if the visitor is premium

    // Step A: Arrival
    // Add an extra random delay to stagger arrivals further
    int arrivalDelay = randomDelay(2.0 / (N + M)); // Modify the rate parameter to control staggered arrival times
    this_thread::sleep_for(chrono::seconds(arrivalDelay));

    timestamp = getTimestamp();
    printLock.lock();
    cout << COLOR_BLUE << "Visitor " << id << " has arrived at A at timestamp " << timestamp << COLOR_RESET << endl;
    printLock.unlock();

    // Step B: Hallway
    this_thread::sleep_for(chrono::seconds(w));
    timestamp = getTimestamp();
    printLock.lock();
    cout << COLOR_GREEN << "Visitor " << id << " has arrived at B at timestamp " << timestamp << COLOR_RESET << endl;
    printLock.unlock();

    /// step 1
    sem_wait(&steps[0]);
    stepLock1.lock();
    this_thread::sleep_for(chrono::seconds(1));
    timestamp = getTimestamp();
    printLock.lock();
    cout << COLOR_YELLOW << "Visitor " << id << " is at step 1 at timestamp " << timestamp << COLOR_RESET << endl;
    printLock.unlock();
    /// step 2
    sem_wait(&steps[1]);
    stepLock2.lock();
    sem_post(&steps[0]);
    stepLock1.unlock();
    this_thread::sleep_for(chrono::seconds(1));
    timestamp = getTimestamp();
    printLock.lock();
    cout << COLOR_YELLOW << "Visitor " << id << " is at step 2 at timestamp " << timestamp << COLOR_RESET << endl;
    printLock.unlock();
    /// step 3
    sem_wait(&steps[2]);
    stepLock3.lock();
    sem_post(&steps[1]);
    stepLock2.unlock();
    this_thread::sleep_for(chrono::seconds(1));
    timestamp = getTimestamp();
    printLock.lock();
    cout << COLOR_YELLOW << "Visitor " << id << " is at step 3 at timestamp " << timestamp << COLOR_RESET << endl;
    printLock.unlock();

    // Step C: Entry to Gallery 1
    sem_wait(&gallery1);
    sem_post(&steps[2]);
    stepLock3.unlock();

    this_thread::sleep_for(chrono::seconds(1)); // Simulate time entering the gallery
    timestamp = getTimestamp();
    printLock.lock();
    cout << COLOR_CYAN << "Visitor " << id << " is at C (entered Gallery 1) at timestamp " << timestamp << COLOR_RESET << endl;
    printLock.unlock();

    // Spend time in Gallery 1
    this_thread::sleep_for(chrono::seconds(x));

    //step D
    timestamp = getTimestamp();
    printLock.lock();
    cout << COLOR_GREEN << "Visitor " << id << " is at D at timestamp " << timestamp << COLOR_RESET << endl;
    printLock.unlock();

    // Step D: Exit Gallery 1, Entry to Corridor DE
    sem_wait(&corridorDE);
    sem_post(&gallery1); // Free up space in Gallery 1

    this_thread::sleep_for(chrono::seconds(randomDelay(2.0 / (N + M))));
    timestamp = getTimestamp();
    printLock.lock();
    cout << COLOR_GREEN << "Visitor " << id << " is at E at timestamp " << timestamp << COLOR_RESET << endl;
    printLock.unlock();

    sem_post(&corridorDE);

    // Task 2: Enter Gallery 2
    timestamp = getTimestamp();
    printLock.lock();
    cout << COLOR_GREEN << "Visitor " << id << " has entered Gallery 2 at timestamp " << timestamp << COLOR_RESET << endl;
    printLock.unlock();

    // Add random delay for visiting Gallery 2 (Task 3 Update)
    this_thread::sleep_for(chrono::seconds(randomDelay(1.0 / y))); // Random interval with time unit `y`

    // Spend time in Gallery 2
    this_thread::sleep_for(chrono::seconds(y));

    // Add visitor to the appropriate queue
    {
        unique_lock<mutex> lock(boothLock);
        if (isPremium)
        {
            premiumQueue.push(id);
        }
        else
        {
            standardQueue.push(id);
        }
    }

    // Task 3: Photo Booth with Reader-Writer Logic
    if (isPremium)
    {
        accessPhotoBooth(id, true); // Exclusive access for premium visitors
    }
    else
    {
        sharedPhotoBoothAccess(id, false); // Shared access for standard visitors
    }

    // Exit the museum
    this_thread::sleep_for(chrono::seconds(1));
    timestamp = getTimestamp();
    printLock.lock();
    cout << COLOR_RED << "Visitor " << id << " has exited the museum at F at timestamp " << timestamp << COLOR_RESET << endl;
    printLock.unlock();
}

int main()
{
    // Input values
    cout << "Enter N M w x y z: ";
    cin >> N >> M >> w >> x >> y >> z;

    // Initialize semaphores
    sem_init(&gallery1, 0, 5); // Maximum 5 in Gallery 1
    for (int i = 0; i < 3; ++i)
        sem_init(&steps[i], 0, 1); // One at a time on each step
    sem_init(&corridorDE, 0, 3);   // Maximum 3 in Corridor DE

    startTime = steady_clock::now();

    // Create threads for standard ticket holders
    for (int i = 0; i < N; ++i)
    {
        // Add a random delay before starting each thread for standard visitors
        this_thread::sleep_for(chrono::milliseconds(randomDelay(20000)));
        visitors.push_back(thread(visitorBehavior, 1001 + i));
    }

    // Create threads for premium ticket holders
    for (int i = 0; i < M; ++i)
    {
        // Add a random delay before starting each thread for standard visitors
        this_thread::sleep_for(chrono::milliseconds(randomDelay(10000)));
        visitors.push_back(thread(visitorBehavior, 2001 + i));
    }

    // Join threads
    for (auto &t : visitors)
    {
        t.join();
    }

    // Destroy semaphores
    sem_destroy(&gallery1);
    for (int i = 0; i < 3; ++i)
        sem_destroy(&steps[i]);
    sem_destroy(&corridorDE);

    return 0;
}
