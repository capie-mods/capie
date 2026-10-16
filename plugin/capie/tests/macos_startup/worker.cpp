/** @file worker.cpp
 * @brief Owned dependency deliberately starts a worker before the marker initializer.
 */
#include <pthread.h>
#include <unistd.h>
namespace {
/// @brief Synchronizes readiness with the dependency's constructor.
pthread_mutex_t mutex = PTHREAD_MUTEX_INITIALIZER;
/// @brief Constructor waits until the worker is alive; worker then waits until process exit.
pthread_cond_t condition = PTHREAD_COND_INITIALIZER;
/// @brief Protected by mutex; immutable after constructor returns.
bool ready = false;
/** @brief Signal readiness and stay alive until fixture process exit.
 * @param unused Unused. @return Never returns during the experiment.
 */
void* worker(void* unused) {
    (void)unused;
    pthread_mutex_lock(&mutex);
    ready = true;
    pthread_cond_broadcast(&condition);
    for (;;) pthread_cond_wait(&condition, &mutex);
}
/** @brief Start a real prior worker, synchronizing constructor completion with its readiness. */
__attribute__((constructor)) void start() {
    pthread_t thread;
    pthread_mutex_lock(&mutex);
    if (pthread_create(&thread, nullptr, worker, nullptr) != 0) _exit(72);
    while (!ready) pthread_cond_wait(&condition, &mutex);
    pthread_mutex_unlock(&mutex);
}
}
/** @brief Confirm dependency constructor readiness. @return One after constructor completion. */
extern "C" int fixture_worker_ready() { return ready ? 1 : 0; }
