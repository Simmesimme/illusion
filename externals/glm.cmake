# ------------------------------------------------------------------------------------------------ #
#                                                                                                  #
#    _)  |  |            _)               This code may be used and modified under the terms       #
#     |  |  |  |  | (_-<  |   _ \    \    of the MIT license. See the LICENSE file for details.    #
#    _| _| _| \_,_| ___/ _| \___/ _| _|   Copyright (c) 2018-2019 Simon Schneegans                 #
#                                                                                                  #
# ------------------------------------------------------------------------------------------------ #

add_library(glm INTERFACE)
target_include_directories(glm SYSTEM
  INTERFACE ${CMAKE_CURRENT_SOURCE_DIR}/externals/glm
)
