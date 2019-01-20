////////////////////////////////////////////////////////////////////////////////////////////////////
//                                                                                                //
//    _)  |  |            _)                This code may be used and modified under the terms    //
//     |  |  |  |  | (_-<  |   _ \    \     of the MIT license. See the LICENSE file for details. //
//    _| _| _| \_,_| ___/ _| \___/ _| _|    Copyright (c) 2018-2019 Simon Schneegans              //
//                                                                                                //
////////////////////////////////////////////////////////////////////////////////////////////////////

#include "FrameResourceIndex.hpp"

namespace Illusion::Graphics {

////////////////////////////////////////////////////////////////////////////////////////////////////

FrameResourceIndex::FrameResourceIndex(uint32_t indexCount)
    : mIndexCount(indexCount) {
}

////////////////////////////////////////////////////////////////////////////////////////////////////

void FrameResourceIndex::step() {
  mIndex = (mIndex + 1) % mIndexCount;
}

////////////////////////////////////////////////////////////////////////////////////////////////////

uint32_t FrameResourceIndex::current() const {
  return mIndex;
}

////////////////////////////////////////////////////////////////////////////////////////////////////

uint32_t FrameResourceIndex::next() const {
  return (mIndex + 1) % mIndexCount;
}

////////////////////////////////////////////////////////////////////////////////////////////////////

uint32_t FrameResourceIndex::previous() const {
  return (mIndex + mIndexCount - 1) % mIndexCount;
}

////////////////////////////////////////////////////////////////////////////////////////////////////

uint32_t FrameResourceIndex::indexCount() const {
  return mIndexCount;
}

////////////////////////////////////////////////////////////////////////////////////////////////////

} // namespace Illusion::Graphics