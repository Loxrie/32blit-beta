/*! \file timer.cpp
*/
#include "engine.hpp"
#include "timer.hpp"

namespace blit {
  std::vector<Timer *> timers;

  Timer::Timer() = default;

  /**
   * Initialize the timer.
   *
   * @param callback Callback function to trigger when timer has elapsed.
   * @param duration Duration of the timer in milliseconds.
   * @param loops Number of times the timer should repeat, -1 = forever.
   */
  void Timer::init(TimerCallback callback, uint32_t duration, int32_t loops) {
    this->callback = callback;
    this->duration = duration;
    this->loops = loops;
    timers.push_back(this);
  }

  /**
   * Start the timer.
   */
  void Timer::start() {
    if (this->state & PAUSED) {
      // Modify duration based on when timer was paused.
      printf("blit::Timer::start resuming timer %d - %d.\n", this->duration, this->paused - this->started);
      this->duration -= this->paused - this->started;
    } 
    this->started = blit::now();
    this->state = RUNNING;
  }

  void Timer::pause() {
    printf("blit::Timer:;pause pausing timer.\n");
    this->paused = blit::now();
    this->state = PAUSED;
  }

  /**
   * Stop the running timer.
   */
  void Timer::stop() {
    this->state = STOPPED;
  }

  /**
   * Update all running timers, triggering any that have elapsed.
   *
   * @param time Time in milliseconds.
   */
  void update_timers(uint32_t time) {
    for (auto t: timers) {
      if (t->state == Timer::RUNNING){
        if (time > (t->started + t->duration)) { // timer triggered
          if(t->loops == -1){
            t->started = time; // reset the start time correcting for any error
          }
          else
          {
            t->loops--;
            t->started = time;
            if (t->loops == 0){
              t->state = Timer::FINISHED;
            }
          }
          t->callback(*t);
        }
      }
    }
  }
}
