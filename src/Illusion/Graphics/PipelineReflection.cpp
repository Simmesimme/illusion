////////////////////////////////////////////////////////////////////////////////////////////////////
//                                                                                                //
//   _)  |  |            _)                 This software may be modified and distributed         //
//    |  |  |  |  | (_-<  |   _ \    \      under the terms of the MIT license.                   //
//   _| _| _| \_,_| ___/ _| \___/ _| _|     See the LICENSE file for details.                     //
//                                                                                                //
//  Authors: Simon Schneegans (code@simonschneegans.de)                                           //
//                                                                                                //
////////////////////////////////////////////////////////////////////////////////////////////////////

// ---------------------------------------------------------------------------------------- includes
#include "PipelineReflection.hpp"

#include "../Core/Logger.hpp"
#include "Device.hpp"

#include <functional>
#include <iostream>

namespace Illusion::Graphics {

////////////////////////////////////////////////////////////////////////////////////////////////////
////////////////////////////////////////////////////////////////////////////////////////////////////

PipelineReflection::PipelineReflection(std::shared_ptr<Device> const& device)
  : mDevice(device) {}

////////////////////////////////////////////////////////////////////////////////////////////////////

PipelineReflection::~PipelineReflection() {}

////////////////////////////////////////////////////////////////////////////////////////////////////

void PipelineReflection::addResource(PipelineResource const& resource) {

  mLayout.reset();

  // As in Vulkan-EZ, the key used for each resource is its name, except in the case of inputs and
  // outputs, since its legal to have separate outputs and inputs with the same name across
  // shader stages.
  auto key = resource.mName;
  if (
    resource.mResourceType == PipelineResource::ResourceType::eOutput ||
    resource.mResourceType == PipelineResource::ResourceType::eInput) {

    key = std::to_string(static_cast<VkShaderStageFlags>(resource.mStages)) + ":" + key;
  }

  if (
    resource.mResourceType == PipelineResource::ResourceType::eInput ||
    resource.mResourceType == PipelineResource::ResourceType::eOutput ||
    resource.mResourceType == PipelineResource::ResourceType::ePushConstantBuffer) {

    std::map<std::string, PipelineResource>* map;

    if (resource.mResourceType == PipelineResource::ResourceType::eInput) {
      map = &mInputs;
    } else if (resource.mResourceType == PipelineResource::ResourceType::eOutput) {
      map = &mOutputs;
    } else if (resource.mResourceType == PipelineResource::ResourceType::ePushConstantBuffer) {
      map = &mPushConstantBuffers;
    }

    auto it = map->find(key);
    if (it != map->end()) {
      it->second.mStages |= resource.mStages;
    } else {
      map->emplace(key, resource);
    }

    return;
  }

  auto it = mDescriptorSetReflections.find(resource.mSet);
  if (it != mDescriptorSetReflections.end()) {
    it->second->addResource(resource);
  } else {
    auto setReflection = std::make_shared<DescriptorSetReflection>(mDevice, resource.mSet);
    setReflection->addResource(resource);
    mDescriptorSetReflections.emplace(resource.mSet, setReflection);
  }
}

////////////////////////////////////////////////////////////////////////////////////////////////////

std::map<uint32_t, std::shared_ptr<DescriptorSetReflection>> const& PipelineReflection::
  getDescriptorSetReflections() const {
  return mDescriptorSetReflections;
};

////////////////////////////////////////////////////////////////////////////////////////////////////

std::map<std::string, PipelineResource> PipelineReflection::getResources(
  PipelineResource::ResourceType type) const {

  if (type == PipelineResource::ResourceType::eInput) {
    return mInputs;
  } else if (type == PipelineResource::ResourceType::eOutput) {
    return mOutputs;
  } else if (type == PipelineResource::ResourceType::ePushConstantBuffer) {
    return mPushConstantBuffers;
  }

  std::map<std::string, PipelineResource> result;

  for (auto const& s : mDescriptorSetReflections) {
    auto const& r = s.second->getResources(type);
    result.insert(r.begin(), r.end());
  }

  return result;
};

////////////////////////////////////////////////////////////////////////////////////////////////////

std::map<std::string, PipelineResource> PipelineReflection::getResources() const {

  std::map<std::string, PipelineResource> result;

  for (auto const& s : mDescriptorSetReflections) {
    auto const& r = s.second->getResources();
    result.insert(r.begin(), r.end());
  }

  result.insert(mInputs.begin(), mInputs.end());
  result.insert(mOutputs.begin(), mOutputs.end());
  result.insert(mPushConstantBuffers.begin(), mPushConstantBuffers.end());

  return result;
};

////////////////////////////////////////////////////////////////////////////////////////////////////

std::shared_ptr<vk::PipelineLayout> const& PipelineReflection::getLayout() const {
  if (!mLayout) {
    std::vector<vk::DescriptorSetLayout> descriptorSetLayouts;
    for (auto const& r : mDescriptorSetReflections) {
      descriptorSetLayouts.push_back(*r.second->getLayout());
    }

    std::vector<vk::PushConstantRange> pushConstantRanges;
    for (auto const& r : mPushConstantBuffers) {
      if (r.second.mStages) {
        pushConstantRanges.push_back({r.second.mStages, r.second.mOffset, r.second.mSize});
      }
    }

    vk::PipelineLayoutCreateInfo pipelineLayoutInfo;
    pipelineLayoutInfo.setLayoutCount         = descriptorSetLayouts.size();
    pipelineLayoutInfo.pSetLayouts            = descriptorSetLayouts.data();
    pipelineLayoutInfo.pushConstantRangeCount = static_cast<uint32_t>(pushConstantRanges.size());
    pipelineLayoutInfo.pPushConstantRanges    = pushConstantRanges.data();

    mLayout = mDevice->createPipelineLayout(pipelineLayoutInfo);
  }

  return mLayout;
}

////////////////////////////////////////////////////////////////////////////////////////////////////

void PipelineReflection::printInfo() const {
  const std::unordered_map<PipelineResource::BaseType, std::string> baseTypes = {
    {PipelineResource::BaseType::eBool, "bool"},
    {PipelineResource::BaseType::eChar, "char"},
    {PipelineResource::BaseType::eInt, "int"},
    {PipelineResource::BaseType::eUint, "uint"},
    {PipelineResource::BaseType::eUint64, "uint64"},
    {PipelineResource::BaseType::eHalf, "half"},
    {PipelineResource::BaseType::eFloat, "float"},
    {PipelineResource::BaseType::eDouble, "double"},
    {PipelineResource::BaseType::eStruct, "struct"},
    {PipelineResource::BaseType::eNone, "none"}};

  const std::unordered_map<PipelineResource::ResourceType, std::string> resourceTypes = {
    {PipelineResource::ResourceType::eInput, "input"},
    {PipelineResource::ResourceType::eOutput, "output"},
    {PipelineResource::ResourceType::eSampler, "sampler"},
    {PipelineResource::ResourceType::eCombinedImageSampler, "combined_image_sampler"},
    {PipelineResource::ResourceType::eSampledImage, "sampled_image"},
    {PipelineResource::ResourceType::eStorageImage, "storage_image"},
    {PipelineResource::ResourceType::eUniformTexelBuffer, "uniform_texel_buffer"},
    {PipelineResource::ResourceType::eStorageTexelBuffer, "storage_texel_buffer"},
    {PipelineResource::ResourceType::eUniformBuffer, "uniform_buffer"},
    {PipelineResource::ResourceType::eStorageBuffer, "storage_buffer"},
    {PipelineResource::ResourceType::eInputAttachment, "input_attachment"},
    {PipelineResource::ResourceType::ePushConstantBuffer, "push_constant_buffer"},
    {PipelineResource::ResourceType::eNone, "none"}};

  std::function<void(PipelineResource::Member const&, int)> printMemberInfo =
    [&printMemberInfo, &baseTypes](PipelineResource::Member const& m, int indent) {

      ILLUSION_MESSAGE << std::string(indent * 2, ' ') << "- \"" << m.mName
                       << "\", type: " << baseTypes.find(m.mBaseType)->second
                       << ", dims: " << m.mColumns << "x" << m.mVecSize << "[" << m.mArraySize
                       << "], size: " << m.mSize << ", offset: " << m.mOffset << std::endl;

      for (auto const& member : m.mMembers) {
        printMemberInfo(member, indent + 1);
      }
    };

  ILLUSION_MESSAGE << "Inputs" << std::endl;
  for (auto const& r : mInputs) {
    ILLUSION_MESSAGE << "  - \"" << r.second.mName << "\" (" << vk::to_string(r.second.mStages)
                     << ", binding: " << r.second.mBinding << ", location: " << r.second.mLocation
                     << ")" << std::endl;
  }

  ILLUSION_MESSAGE << "Outputs" << std::endl;
  for (auto const& r : mOutputs) {
    ILLUSION_MESSAGE << "  - \"" << r.second.mName << "\" (" << vk::to_string(r.second.mStages)
                     << ", binding: " << r.second.mBinding << ", location: " << r.second.mLocation
                     << ")" << std::endl;
  }

  ILLUSION_MESSAGE << "PushConstants" << std::endl;
  for (auto const& r : mPushConstantBuffers) {
    ILLUSION_MESSAGE << "  - \"" << r.second.mName << "\" (" << vk::to_string(r.second.mStages)
                     << ", size: " << r.second.mSize << ", offset: " << r.second.mOffset << ")"
                     << std::endl;
  }

  for (auto const& s : mDescriptorSetReflections) {
    ILLUSION_MESSAGE << "set: " << s.first << std::endl;
    for (auto const& pair : s.second->getResources()) {
      auto const& r = pair.second;
      ILLUSION_MESSAGE << "  - \"" << r.mName << "\" ("
                       << resourceTypes.find(r.mResourceType)->second << ", "
                       << vk::to_string(r.mStages) << ", access: " << vk::to_string(r.mAccess)
                       << ", set: " << r.mSet << ", binding: " << r.mBinding
                       << ", location: " << r.mLocation << ")" << std::endl;
      for (auto const& member : r.mMembers) {
        printMemberInfo(member, 2);
      }
    }
  }
}

////////////////////////////////////////////////////////////////////////////////////////////////////

} // namespace Illusion::Graphics
