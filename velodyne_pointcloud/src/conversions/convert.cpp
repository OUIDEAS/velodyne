// Copyright 2009, 2010, 2011, 2012, 2019 Austin Robot Technology, Jack O'Quin, Jesse Vera, Joshua Whitley  // NOLINT
// All rights reserved.
//
// Software License Agreement (BSD License 2.0)
//
// Redistribution and use in source and binary forms, with or without
// modification, are permitted provided that the following conditions
// are met:
//
// * Redistributions of source code must retain the above copyright
//   notice, this list of conditions and the following disclaimer.
// * Redistributions in binary form must reproduce the above
//   copyright notice, this list of conditions and the following
//   disclaimer in the documentation and/or other materials provided
//   with the distribution.
// * Neither the name of {copyright_holder} nor the names of its
//   contributors may be used to endorse or promote products derived
//   from this software without specific prior written permission.
//
// THIS SOFTWARE IS PROVIDED BY THE COPYRIGHT HOLDERS AND CONTRIBUTORS
// "AS IS" AND ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT
// LIMITED TO, THE IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS
// FOR A PARTICULAR PURPOSE ARE DISCLAIMED. IN NO EVENT SHALL THE
// COPYRIGHT OWNER OR CONTRIBUTORS BE LIABLE FOR ANY DIRECT, INDIRECT,
// INCIDENTAL, SPECIAL, EXEMPLARY, OR CONSEQUENTIAL DAMAGES (INCLUDING,
// BUT NOT LIMITED TO, PROCUREMENT OF SUBSTITUTE GOODS OR SERVICES;
// LOSS OF USE, DATA, OR PROFITS; OR BUSINESS INTERRUPTION) HOWEVER
// CAUSED AND ON ANY THEORY OF LIABILITY, WHETHER IN CONTRACT, STRICT
// LIABILITY, OR TORT (INCLUDING NEGLIGENCE OR OTHERWISE) ARISING IN
// ANY WAY OUT OF THE USE OF THIS SOFTWARE, EVEN IF ADVISED OF THE
// POSSIBILITY OF SUCH DAMAGE.

#include <velodyne_pointcloud/convert.hpp>

#include <rcl_interfaces/msg/floating_point_range.hpp>
#include <rcl_interfaces/msg/parameter_descriptor.hpp>
#include <rclcpp/rclcpp.hpp>
#include <rcl_interfaces/msg/set_parameters_result.hpp>
#include <rclcpp_components/register_node_macro.hpp>
#include <velodyne_msgs/msg/velodyne_scan.hpp>

#include <cmath>
#include <functional>
#include <memory>
#include <string>
#include <unordered_map>
#include <vector>

#include "velodyne_pointcloud/organized_cloudXYZIR.hpp"
#include "velodyne_pointcloud/pointcloudXYZIR.hpp"
#include "velodyne_pointcloud/rawdata.hpp"

namespace velodyne_pointcloud
{

/** @brief Constructor. */
Convert::Convert(const rclcpp::NodeOptions & options)
: rclcpp::Node("velodyne_convert_node", options),
  tf_buffer_(this->get_clock()),
  diagnostics_(this)
{
  // get path to angles.config file for this device
  std::string calibration_file = this->declare_parameter("calibration", "");
  required_parameters_["calibration"] = true;

  rcl_interfaces::msg::ParameterDescriptor min_range_desc;
  min_range_desc.name = "min_range";
  min_range_desc.type = rcl_interfaces::msg::ParameterType::PARAMETER_DOUBLE;
  min_range_desc.description = "minimum range to publish";
  rcl_interfaces::msg::FloatingPointRange min_range_range;
  min_range_range.from_value = 0.1;
  min_range_range.to_value = 10.0;
  min_range_desc.floating_point_range.push_back(min_range_range);
  double min_range = this->declare_parameter("min_range", 0.9, min_range_desc);
  required_parameters_["min_range"] = true;

  rcl_interfaces::msg::ParameterDescriptor max_range_desc;
  max_range_desc.name = "max_range";
  max_range_desc.type = rcl_interfaces::msg::ParameterType::PARAMETER_DOUBLE;
  max_range_desc.description = "maximum range to publish";
  rcl_interfaces::msg::FloatingPointRange max_range_range;
  max_range_range.from_value = 0.1;
  max_range_range.to_value = 200.0;
  max_range_desc.floating_point_range.push_back(max_range_range);
  double max_range = this->declare_parameter("max_range", 130.0, max_range_desc);
  required_parameters_["max_range"] = true;

  rcl_interfaces::msg::ParameterDescriptor view_direction_desc;
  view_direction_desc.name = "view_direction";
  view_direction_desc.type = rcl_interfaces::msg::ParameterType::PARAMETER_DOUBLE;
  view_direction_desc.description = "angle defining the center of view";
  rcl_interfaces::msg::FloatingPointRange view_direction_range;
  view_direction_range.from_value = -M_PI;
  view_direction_range.to_value = M_PI;
  view_direction_desc.floating_point_range.push_back(view_direction_range);
  double view_direction = this->declare_parameter("view_direction", 0.0, view_direction_desc);
  required_parameters_["view_direction"] = true;

  rcl_interfaces::msg::ParameterDescriptor view_width_desc;
  view_width_desc.name = "view_width";
  view_width_desc.type = rcl_interfaces::msg::ParameterType::PARAMETER_DOUBLE;
  view_width_desc.description = "angle defining the view width";
  rcl_interfaces::msg::FloatingPointRange view_width_range;
  view_width_range.from_value = 0.0;
  view_width_range.to_value = 2.0 * M_PI;
  view_width_desc.floating_point_range.push_back(view_width_range);
  double view_width = this->declare_parameter("view_width", 2.0 * M_PI, view_width_desc);
  required_parameters_["view_width"] = true;

  bool organize_cloud = this->declare_parameter("organize_cloud", true);
  required_parameters_["organize_cloud"] = true;

  std::string target_frame = this->declare_parameter("target_frame", "");
  required_parameters_["target_frame"] = true;

  std::string fixed_frame = this->declare_parameter("fixed_frame", "");
  required_parameters_["fixed_frame"] = true;

  RCLCPP_INFO(this->get_logger(), "correction angles: %s", calibration_file.c_str());

  data_ = std::make_unique<velodyne_rawdata::RawData>(calibration_file);

  if (organize_cloud) {
    container_ptr_ = std::make_unique<OrganizedCloudXYZIR>(
      min_range, max_range, target_frame, fixed_frame,
      data_->numLasers(), data_->scansPerPacket(), tf_buffer_);
  } else {
    container_ptr_ = std::make_unique<PointcloudXYZIR>(
      min_range, max_range, target_frame, fixed_frame,
      data_->scansPerPacket(), tf_buffer_);
  }

  // advertise output point cloud (before subscribing to input data)
  output_ =
    this->create_publisher<sensor_msgs::msg::PointCloud2>("velodyne_points", 10);

  // subscribe to VelodyneScan packets
  velodyne_scan_ =
    this->create_subscription<velodyne_msgs::msg::VelodyneScan>(
    "velodyne_packets", rclcpp::QoS(10),
    std::bind(&Convert::processScan, this, std::placeholders::_1));

  // Diagnostics
  diagnostics_.setHardwareID("Velodyne Convert");
  // Arbitrary frequencies since we don't know which RPM is used, and are only
  // concerned about monitoring the frequency.
  diag_min_freq_ = 2.0;
  diag_max_freq_ = 20.0;
  diag_topic_ = std::make_unique<diagnostic_updater::TopicDiagnostic>(
    "velodyne_points", diagnostics_,
    diagnostic_updater::FrequencyStatusParam(
      &diag_min_freq_, &diag_max_freq_, 0.1, 10),
    diagnostic_updater::TimeStampStatusParam());

  data_->setParameters(min_range, max_range, view_direction, view_width);
  container_ptr_->configure(min_range, max_range, target_frame, fixed_frame);

  set_on_parameters_set_callback(std::bind(&Convert::parametersCallback, this,
    std::placeholders::_1));
}

rcl_interfaces::msg::SetParametersResult Convert::parametersCallback(
  const std::vector<rclcpp::Parameter> & parameters)
{
  // This callback is called *before* the new values are set (so get_parameter
  // always returns the old value).  Collect the old values here, and we'll
  // update them in the loop below, then use the resulting collection of old
  // and new values to do the reconfiguration at the end.
  std::unordered_map<std::string, rclcpp::Parameter> updated_params;
  for (const auto & p : required_parameters_) {
    updated_params[p.first] = this->get_parameter(p.first);
  }

  bool need_new_data = false;
  bool need_new_container = false;
  rcl_interfaces::msg::SetParametersResult result;
  result.successful = true;
  for (const auto & parameter : parameters) {
    if (required_parameters_.find(parameter.get_name()) != required_parameters_.end()) {
      // The user is trying to change one of the required parameters.
      // Ensure they aren't trying to delete it.
      if (parameter.get_type() == rclcpp::ParameterType::PARAMETER_NOT_SET) {
        result.successful = false;
        result.reason = "Cannot delete required parameter";
        break;
      }

      if (parameter.get_name() == "calibration" &&
        this->get_parameter("calibration").get_value<std::string>() !=
        parameter.get_value<std::string>())
      {
        need_new_data = true;
      }

      if (parameter.get_name() == "organize_cloud" &&
        this->get_parameter("organize_cloud").get_value<bool>() !=
        parameter.get_value<bool>())
      {
        need_new_container = true;
      }

      updated_params[parameter.get_name()] = parameter;
    }
  }

  if (result.successful) {
    double min_range = updated_params["min_range"].get_value<double>();
    double max_range = updated_params["max_range"].get_value<double>();
    double view_direction = updated_params["view_direction"].get_value<double>();
    double view_width = updated_params["view_width"].get_value<double>();
    std::string target_frame = updated_params["target_frame"].get_value<std::string>();
    std::string fixed_frame = updated_params["fixed_frame"].get_value<std::string>();
    std::string calibration_file = updated_params["calibration"].get_value<std::string>();

    if (need_new_data) {
      data_ = std::make_unique<velodyne_rawdata::RawData>(calibration_file);
    }

    if (need_new_container) {
      if (updated_params["organize_cloud"].get_value<bool>()) {
        container_ptr_ = std::make_unique<OrganizedCloudXYZIR>(
          min_range, max_range, target_frame, fixed_frame,
          data_->numLasers(), data_->scansPerPacket(), tf_buffer_);
      } else {
        container_ptr_ = std::make_unique<PointcloudXYZIR>(
          min_range, max_range, target_frame, fixed_frame,
          data_->scansPerPacket(), tf_buffer_);
      }
    }

    data_->setParameters(min_range, max_range, view_direction, view_width);
    container_ptr_->configure(min_range, max_range, target_frame, fixed_frame);
  }

  return result;
}

/** @brief Callback for raw scan messages. */
void Convert::processScan(const velodyne_msgs::msg::VelodyneScan::SharedPtr scanMsg)
{
  if (output_->get_subscription_count() == 0 &&
    output_->get_intra_process_subscription_count() == 0)   // no one listening?
  {
    return;                                     // avoid much work
  }

  // allocate a point cloud with same time and frame ID as raw data
  container_ptr_->setup(scanMsg);

  // process each packet provided by the driver
  for (size_t i = 0; i < scanMsg->packets.size(); ++i) {
    data_->unpack(scanMsg->packets[i], *container_ptr_);
  }

  // publish the accumulated cloud message
  diag_topic_->tick(scanMsg->header.stamp);
  output_->publish(container_ptr_->finishCloud());
}

}  // namespace velodyne_pointcloud

RCLCPP_COMPONENTS_REGISTER_NODE(velodyne_pointcloud::Convert)
