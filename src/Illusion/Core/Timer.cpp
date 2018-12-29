////////////////////////////////////////////////////////////////////////////////////////////////////
//                                                                                                //
//   _)  |  |            _)                 This software may be modified and distributed         //
//    |  |  |  |  | (_-<  |   _ \    \      under the terms of the MIT license.                   //
//   _| _| _| \_,_| ___/ _| \___/ _| _|     See the LICENSE file for details.                     //
//                                                                                                //
//  Authors: Simon Schneegans (code@simonschneegans.de)                                           //
//                                                                                                //
////////////////////////////////////////////////////////////////////////////////////////////////////

#include "Timer.hpp"

#include <chrono>
#include <iostream>

namespace Illusion::Core {

////////////////////////////////////////////////////////////////////////////////////////////////////

Timer::Timer(bool autoStart) {
  if (autoStart)
    start();
}

////////////////////////////////////////////////////////////////////////////////////////////////////

void Timer::start() {
  mStart = getNow();
}

////////////////////////////////////////////////////////////////////////////////////////////////////

double Timer::reset() {
  double now(getNow());
  double elapsed(now - mStart);
  mStart = now;
  return elapsed;
}

////////////////////////////////////////////////////////////////////////////////////////////////////

double Timer::getElapsed() const {
  return getNow() - mStart;
}

////////////////////////////////////////////////////////////////////////////////////////////////////

bool Timer::isRunning() const {
  return mStart != -1.f;
}

////////////////////////////////////////////////////////////////////////////////////////////////////

double Timer::getNow() {
  auto time       = std::chrono::system_clock::now();
  auto sinceEpoch = time.time_since_epoch();
  return std::chrono::duration_cast<std::chrono::microseconds>(sinceEpoch).count() * 0.000001;
}

////////////////////////////////////////////////////////////////////////////////////////////////////

} // namespace Illusion::Core
