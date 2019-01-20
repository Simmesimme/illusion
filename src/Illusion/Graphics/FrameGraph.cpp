////////////////////////////////////////////////////////////////////////////////////////////////////
//                                                                                                //
//    _)  |  |            _)                This code may be used and modified under the terms    //
//     |  |  |  |  | (_-<  |   _ \    \     of the MIT license. See the LICENSE file for details. //
//    _| _| _| \_,_| ___/ _| \___/ _| _|    Copyright (c) 2018-2019 Simon Schneegans              //
//                                                                                                //
////////////////////////////////////////////////////////////////////////////////////////////////////

#include "FrameGraph.hpp"

#include "../Core/Logger.hpp"
#include "CommandBuffer.hpp"
#include "Device.hpp"
#include "Window.hpp"

#include <iostream>
#include <queue>
#include <unordered_set>

namespace Illusion::Graphics {

////////////////////////////////////////////////////////////////////////////////////////////////////

FrameGraph::LogicalResource& FrameGraph::LogicalResource::setName(std::string const& name) {
  mName = name;
  return *this;
}

////////////////////////////////////////////////////////////////////////////////////////////////////

FrameGraph::LogicalResource& FrameGraph::LogicalResource::setFormat(vk::Format format) {
  mFormat = format;
  mDirty  = true;
  return *this;
}

////////////////////////////////////////////////////////////////////////////////////////////////////

FrameGraph::LogicalResource& FrameGraph::LogicalResource::setSizing(ResourceSizing sizing) {
  mSizing = sizing;
  mDirty  = true;
  return *this;
}

////////////////////////////////////////////////////////////////////////////////////////////////////

FrameGraph::LogicalResource& FrameGraph::LogicalResource::setExtent(glm::uvec2 const& extent) {
  mExtent = extent;
  mDirty  = true;
  return *this;
}

////////////////////////////////////////////////////////////////////////////////////////////////////
////////////////////////////////////////////////////////////////////////////////////////////////////
////////////////////////////////////////////////////////////////////////////////////////////////////

FrameGraph::LogicalPass& FrameGraph::LogicalPass::assignResource(LogicalResource const& resource,
    ResourceUsage usage, ResourceAccess access, std::optional<vk::ClearValue> clear) {

  if (mLogicalResources.find(&resource) != mLogicalResources.end()) {
    throw std::runtime_error("Failed to add resource \"" + resource.mName +
                             "\" to frame graph pass \"" + mName +
                             "\": Resource has already been added to this pass!");
  }

  mLogicalResources[&resource] = {usage, access, clear};
  mDirty                       = true;
  return *this;
}

////////////////////////////////////////////////////////////////////////////////////////////////////

FrameGraph::LogicalPass& FrameGraph::LogicalPass::setName(std::string const& name) {
  mName = name;
  return *this;
}

////////////////////////////////////////////////////////////////////////////////////////////////////

FrameGraph::LogicalPass& FrameGraph::LogicalPass::setOutputWindow(WindowPtr const& window) {
  mOutputWindow = window;
  mDirty        = true;
  return *this;
}

////////////////////////////////////////////////////////////////////////////////////////////////////

FrameGraph::LogicalPass& FrameGraph::LogicalPass::setProcessCallback(
    std::function<void(CommandBufferPtr)> const& callback) {
  mProcessCallback = callback;
  return *this;
}

////////////////////////////////////////////////////////////////////////////////////////////////////
////////////////////////////////////////////////////////////////////////////////////////////////////
////////////////////////////////////////////////////////////////////////////////////////////////////

FrameGraph::FrameGraph(DevicePtr const& device, FrameResourceIndexPtr const& frameIndex)
    : mDevice(device)
    , mPerFrame(frameIndex, [device]() {
      PerFrame perFrame;
      perFrame.mPrimaryCommandBuffer    = CommandBuffer::create(device);
      perFrame.mRenderFinishedSemaphore = device->createSemaphore();
      perFrame.mFrameFinishedFence      = device->createFence();
      return perFrame;
    }) {
}

////////////////////////////////////////////////////////////////////////////////////////////////////

FrameGraph::LogicalResource& FrameGraph::createResource() {
  mDirty = true;
  mLogicalResources.push_back({});
  return mLogicalResources.back();
}

////////////////////////////////////////////////////////////////////////////////////////////////////

FrameGraph::LogicalPass& FrameGraph::createPass() {
  mDirty = true;
  mLogicalPasses.push_back({});
  return mLogicalPasses.back();
}

////////////////////////////////////////////////////////////////////////////////////////////////////

void FrameGraph::process() {

  // graph validation phase ------------------------------------------------------------------------

  // The FrameGraph is dirty when a LogicalPass or LogicalResource was added, when a LogicalResource
  // was added to one of its LogicalPasses or when one of its LogicalResources was reconfigured.
  if (isDirty()) {

    // First make some sanity checks. After this call we can be sure that there is exactly one
    // LogicalPass with an output window and that all references to LogicalResources in the
    // LogicalPasses are valid.
    try {
      validate();
    } catch (std::runtime_error e) {
      throw std::runtime_error("Frame graph validation failed: " + std::string(e.what()));
    }

    // Then make sure that we re-create all physical passes and physical resources.
    for (auto& perFrame : mPerFrame) {
      perFrame.mDirty = true;
    }

    // Reset dirty flags of the FrameGraph, the LogicalPasses and the LogicalResources.
    clearDirty();
  }

  // resource allocation phase ---------------------------------------------------------------------

  // Acquire our current set of per-frame resources.
  auto& perFrame = mPerFrame.current();

  // Make sure that the GPU has finished processing the last frame which has been rendered with this
  // set of per-frame resources.
  mDevice->waitForFence(perFrame.mFrameFinishedFence);
  mDevice->resetFence(perFrame.mFrameFinishedFence);

  // The PhysicalPasses and PhysicalResources need to be updated. This could definitely be optimized
  // with more fine-grained dirty flags, but as this should not happen on a frame-to-frame basis, it
  // seems to be ok to recreate everything from scratch here.
  if (perFrame.mDirty) {
    ILLUSION_TRACE << "Reconstructing frame graph..." << std::endl;
    perFrame.mPhysicalResources.clear();
    perFrame.mPhysicalPasses.clear();

    // First we will create a list of LogicalPasses with a valid execution order. This list may not
    // contain all LogicalPasses of this FrameGraph, as some passes may not be connected to our
    // final pass (pass culling).
    // We will collect the passes bottom-up; that means we start with the final output pass and then
    // collect all passes which provide input for this pass. Then we search for the passes which
    // provide input for those passes... and so on.
    std::list<LogicalPass*> culledPasses;

    // Therefore we have first to find the final pass. That is the one with an output window. Due to
    // the validation above, we are sure that there is exactly one.
    LogicalPass* finalPass = nullptr;
    for (auto& pass : mLogicalPasses) {
      if (pass.mOutputWindow) {
        finalPass = &pass;
        break;
      }
    }

    // Then we will create a queue of inputs which are required for the processing of the passes
    // inserted into our culledPasses. We will insert all the inputs for our final pass first.
    std::list<LogicalPass*> passQueue;
    passQueue.push_back(finalPass);

    while (!passQueue.empty()) {
      // Pop the current pass from the queue and add it to cullepPasses.
      auto pass = passQueue.front();
      passQueue.pop_front();
      culledPasses.push_back(pass);

      ILLUSION_TRACE << "  Resolving dependencies of pass \"" + pass->mName + "\"..." << std::endl;

      // We start searching for preceding passes at our current pass.
      auto currentPassIt = mLogicalPasses.rbegin();
      while (&(*currentPassIt) != pass) {
        ++currentPassIt;
      }

      // Now we have to find the passes which are in front of the current pass in mLogicalPasses
      // list of the FrameGraph and write to the resources of the current pass.
      for (auto r : pass->mLogicalResources) {
        ILLUSION_TRACE << "    resource \"" + r.first->mName + "\"" << std::endl;

        // Step backwards through all LogicalPasses until we find a pass referencing this resource.
        auto prePassIt = currentPassIt;
        while (prePassIt != mLogicalPasses.rend()) {
          do {
            ++prePassIt;
          } while (
              prePassIt != mLogicalPasses.rend() &&
              prePassIt->mLogicalResources.find(r.first) == prePassIt->mLogicalResources.end());

          // Now there are several cases:
          //  * There is a preceding use of this resource and we use it as write-only: As this would
          //    discard any content, we consider this as an error for now.
          //  * There is a preceding use of this resource and it is read-only: We can ignore this
          //    case
          //    as the resource won't be modified by this pass.
          //  * There is a preceding use of this resource and it writes to the resource: The
          //    corresponding pass has to be executed before.
          //  * There is no preceding use: This is alright, if our access is write-only. As soon as
          //    we want to read the resource, this becomes an error.
          if (prePassIt != mLogicalPasses.rend()) {
            if (r.second.mAccess == ResourceAccess::eWriteOnly) {
              throw std::runtime_error("Frame graph construction failed: Write-only output \"" +
                                       r.first->mName + "\" of pass \"" + pass->mName +
                                       "\" is used by the preceding pass \"" + prePassIt->mName +
                                       "\"!");
            } else if (prePassIt->mLogicalResources.find(r.first)->second.mAccess ==
                       ResourceAccess::eReadOnly) {
              ILLUSION_TRACE << "      is read-only in \"" + prePassIt->mName + "\"." << std::endl;
            } else {
              // In order to make sure that there are no duplicates in our queue, we first remove
              // all entries referencing the same pass.
              LogicalPass* prePass = &(*prePassIt);
              passQueue.remove(prePass);
              passQueue.push_back(prePass);
              ILLUSION_TRACE << "      is written by \"" + prePassIt->mName + "\"." << std::endl;
              break;
            }
          } else if (r.second.mAccess != ResourceAccess::eWriteOnly) {
            throw std::runtime_error("Frame graph construction failed: Input \"" + r.first->mName +
                                     "\" of pass \"" + pass->mName +
                                     "\" is not the output of any previous pass!");
          } else {
            ILLUSION_TRACE << "      is created by this pass." << std::endl;
            break;
          }
        }
      }
    }

    // Now we have to reverse our list of passes.
    culledPasses.reverse();

    // Print some debugging information.
    ILLUSION_TRACE << "  Logical pass execution order will be" << std::endl;
    for (auto const& p : culledPasses) {
      ILLUSION_TRACE << "    " << p->mName << std::endl;
    }

    perFrame.mDirty = false;
    ILLUSION_TRACE << "Frame graph reconstruction done." << std::endl;
  }

  // recording phase -------------------------------------------------------------------------------

  // Now we can finally start recording our command buffer.
  perFrame.mPrimaryCommandBuffer->reset();
  perFrame.mPrimaryCommandBuffer->begin();

  // perFrame.mPrimaryCommandBuffer->beginRenderPass(res.mRenderPass);

  for (auto& physicalPass : perFrame.mPhysicalPasses) {
    for (auto& logicalPass : physicalPass.mLogicalPasses) {
      logicalPass->mProcessCallback(perFrame.mPrimaryCommandBuffer);
    }
  }

  // perFrame.mPrimaryCommandBuffer->endRenderPass();

  perFrame.mPrimaryCommandBuffer->end();

  perFrame.mPrimaryCommandBuffer->submit({}, {}, {perFrame.mRenderFinishedSemaphore});

  // window->present(mRenderPass->getFramebuffer()->getImages()[0],
  // perFrame.mRenderFinishedSemaphore,
  //     perFrame.mFrameFinishedFence);
}

////////////////////////////////////////////////////////////////////////////////////////////////////

bool FrameGraph::isDirty() const {
  if (mDirty) {
    return true;
  }

  for (auto const& resource : mLogicalResources) {
    if (resource.mDirty) {
      return true;
    }
  }

  for (auto const& pass : mLogicalPasses) {
    if (pass.mDirty) {
      return true;
    }
  }

  return false;
}

////////////////////////////////////////////////////////////////////////////////////////////////////

void FrameGraph::clearDirty() {
  mDirty = false;

  for (auto& resource : mLogicalResources) {
    resource.mDirty = false;
  }

  for (auto& pass : mLogicalPasses) {
    pass.mDirty = false;
  }
}

////////////////////////////////////////////////////////////////////////////////////////////////////

void FrameGraph::validate() const {

  ILLUSION_TRACE << "Validating frame graph..." << std::endl;

  // Check whether each resource of each pass was actually created by this frame graph.
  for (auto const& pass : mLogicalPasses) {
    for (auto const& passResource : pass.mLogicalResources) {
      bool ours = false;
      for (auto const& graphResource : mLogicalResources) {
        if (&graphResource == passResource.first) {
          ours = true;
          break;
        }
      }
      if (!ours) {
        throw std::runtime_error("Resource \"" + passResource.first->mName + "\" of pass \"" +
                                 pass.mName +
                                 "\" does not belong to this frame graph. Did you accidentally "
                                 "create a copy of the reference?");
      }
    }
  }

  // Check whether we have exactly one pass with an output window.
  LogicalPass const* finalPass = nullptr;
  for (auto const& pass : mLogicalPasses) {
    if (pass.mOutputWindow) {
      if (!finalPass) {
        finalPass = &pass;
      } else {
        throw std::runtime_error("There are multiple output windows in the graph!");
      }
    }
  }

  if (!finalPass) {
    throw std::runtime_error("There is no output window in the graph!");
  }

  // Check whether the resolutions of all attachments of each pass are the same
  glm::vec2 windowExtent = finalPass->mOutputWindow->pExtent.get();
  for (auto const& pass : mLogicalPasses) {
    glm::vec2 passExtent = glm::vec2(-1);
    for (auto const& resource : pass.mLogicalResources) {
      if (resource.second.mUsage == ResourceUsage::eColorAttachment ||
          resource.second.mUsage == ResourceUsage::eDepthAttachment) {
        glm::vec2 resourceExtent = (resource.first->mSizing == ResourceSizing::eAbsolute)
                                       ? resource.first->mExtent
                                       : resource.first->mExtent * windowExtent;
        if (passExtent == glm::vec2(-1)) {
          passExtent = resourceExtent;
        } else if (passExtent != resourceExtent) {
          throw std::runtime_error(
              "Attachments of pass \"" + pass.mName + "\" do not have the same size!");
        }
      }
    }
  }

  ILLUSION_TRACE << "  all good." << std::endl;
}

////////////////////////////////////////////////////////////////////////////////////////////////////

} // namespace Illusion::Graphics