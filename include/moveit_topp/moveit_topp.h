/*********************************************************************
 * Software License Agreement (BSD License)
 *
 *  Copyright (c) 2015, PickNik LLC
 *  All rights reserved.
 *
 *  Redistribution and use in source and binary forms, with or without
 *  modification, are permitted provided that the following conditions
 *  are met:
 *
 *   * Redistributions of source code must retain the above copyright
 *     notice, this list of conditions and the following disclaimer.
 *   * Redistributions in binary form must reproduce the above
 *     copyright notice, this list of conditions and the following
 *     disclaimer in the documentation and/or other materials provided
 *     with the distribution.
 *   * Neither the name of PickNik LLC nor the names of its
 *     contributors may be used to endorse or promote products derived
 *     from this software without specific prior written permission.
 *
 *  THIS SOFTWARE IS PROVIDED BY THE COPYRIGHT HOLDERS AND CONTRIBUTORS
 *  "AS IS" AND ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT
 *  LIMITED TO, THE IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS
 *  FOR A PARTICULAR PURPOSE ARE DISCLAIMED. IN NO EVENT SHALL THE
 *  COPYRIGHT OWNER OR CONTRIBUTORS BE LIABLE FOR ANY DIRECT, INDIRECT,
 *  INCIDENTAL, SPECIAL, EXEMPLARY, OR CONSEQUENTIAL DAMAGES (INCLUDING,
 *  BUT NOT LIMITED TO, PROCUREMENT OF SUBSTITUTE GOODS OR SERVICES;
 *  LOSS OF USE, DATA, OR PROFITS; OR BUSINESS INTERRUPTION) HOWEVER
 *  CAUSED AND ON ANY THEORY OF LIABILITY, WHETHER IN CONTRACT, STRICT
 *  LIABILITY, OR TORT (INCLUDING NEGLIGENCE OR OTHERWISE) ARISING IN
 *  ANY WAY OUT OF THE USE OF THIS SOFTWARE, EVEN IF ADVISED OF THE
 *  POSSIBILITY OF SUCH DAMAGE.
 *********************************************************************/

/* Author: Dave Coleman
   Desc:   Wrapper for time opitmal path parameterization
*/

#ifndef MOVEIT_TOPP_MOVEIT_TOPP_H
#define MOVEIT_TOPP_MOVEIT_TOPP_H

// C++
#include <string>
#include <fstream>
#include <streambuf>

// ROS
#include <ros/ros.h>

// ROS parameter loading
#include <rosparam_shortcuts/rosparam_shortcuts.h>

// TOPP
#include "TOPP.h"
#include "KinematicLimits.h"
#include "TorqueLimits.h"
#include "PolygonConstraints.h"

// Spline
#include "spline.hpp"

// MoveIt
#include <moveit_msgs/RobotTrajectory.h>

namespace moveit_topp
{

class MoveItTopp
{
public:

  /**
   * \brief Constructor
   */
  MoveItTopp()
  {
    // Load rosparams
    //ros::NodeHandle rpnh(nh_, name_);
    //std::size_t error = 0;
    //error += !rosparam_shortcuts::get(name_, rpnh, "control_rate", control_rate_);
    // add more parameters here to load if desired
    //rosparam_shortcuts::shutdownIfError(name_, error);


    // Config TODO(davetcoleman): do not hard code
    std::size_t num_joints = 7;
    double discrtimestep = 0.005;
    double max_velocity = 2;
    double max_acceleration = 10;
    std::string constraint_string = std::to_string(discrtimestep);
    constraint_string.append("\n");
    // TODO(davetcoleman): do not hardcode constraints
    std::string temp = std::to_string(max_velocity).append(" ");
    for (std::size_t i = 0; i < num_joints; ++i)
      constraint_string.append(temp); // velocity
    constraint_string.append("\n");
    temp = std::to_string(max_acceleration).append(" ");
    for (std::size_t i = 0; i < num_joints; ++i)
      constraint_string.append(temp); // velocity

    // Debug
    std::cout << "-------------------------------------------------------" << std::endl;
    std::cout << "constraint_string: \n" << constraint_string << std::endl;

    // Setup constraints
    pconstraints_.reset(new TOPP::KinematicLimits(constraint_string));


    ROS_INFO_STREAM_NAMED(name_,"MoveItTopp Ready.");
  }

  void readPPTrajFromFile(const std::string& filename, TOPP::Trajectory &trajectory)
  {
    ROS_INFO_STREAM_NAMED(name_, "Reading PP from filename: " << filename);

    std::string trajectory_string;
    std::ifstream filehandle(filename.c_str());

    filehandle.seekg(0, std::ios::end);
    trajectory_string.reserve(filehandle.tellg());
    filehandle.seekg(0, std::ios::beg);

    trajectory_string.assign((std::istreambuf_iterator<char>(filehandle)),
                  std::istreambuf_iterator<char>());

    // Convert to trajectory
    trajectory.InitFromString(trajectory_string);
  }

  void optimizeTrajectory(const TOPP::Trajectory &old_trajectory, TOPP::Trajectory &new_trajectory)
  {
    // Benchmark runtime
    ros::Time start_time = ros::Time::now();

    ROS_INFO_STREAM_NAMED(name_, "Optimizing tractory with " << old_trajectory.dimension << " dims, "
                          << old_trajectory.chunkslist.size() << " waypoints");
    //moveit_msgs::RobotTrajectory &trajectory_msg;

    pconstraints_->trajectory = old_trajectory;

    // Set default private tuning parameters
    pconstraints_->bisectionprecision = 0.01;
    pconstraints_->loweringcoef = 0.95;

    // Run TOPP
    ROS_INFO_STREAM_NAMED(name_, "Computing profiles");
    TOPP::dReal sdbeg = 0;
    TOPP::dReal sdend= 0;
    pconstraints_->integrationtimestep = 0;
    pconstraints_->passswitchpointnsteps = 5;
    pconstraints_->extrareps = 0;
    pconstraints_->stepthresh = 0.01;
    ros::Time start_time2 = ros::Time::now();
    TOPP::ComputeProfiles(*pconstraints_, sdbeg, sdend);

    // Benchmark runtime
    double duration2 = (ros::Time::now() - start_time2).toSec();
    ROS_ERROR_STREAM_NAMED(name_, "Profiles time: " << duration2 << " seconds (" << 1.0/duration2 << " hz)");

    // Reparameterize
    ROS_INFO_STREAM_NAMED(name_, "Parameterizing");
    //pconstraints_->reparamtimestep = 0; // Original value - sets automatically
    pconstraints_->reparamtimestep = 0.001; // DTC Causes less points to be created, faster
    ros::Time start_time3 = ros::Time::now();
    pconstraints_->trajectory.Reparameterize(*pconstraints_, new_trajectory);

    // Benchmark runtime
    double duration3 = (ros::Time::now() - start_time3).toSec();
    ROS_ERROR_STREAM_NAMED(name_, "Parameterize time: " << duration3 << " seconds (" << 1.0/duration3 << " hz)");

    // Get results
    //WriteResultTrajectory();
    //std::cout << "new_trajectory_string_: " << new_trajectory_string_ << std::endl;


    // Benchmark runtime
    double duration = (ros::Time::now() - start_time).toSec();
    ROS_WARN_STREAM_NAMED(name_, "Total time: " << duration << " seconds (" << 1.0/duration << " hz)");
  }

  bool writeTrajectoryToFile(const std::string &file_path, const TOPP::Trajectory &trajectory)
  {
    ROS_INFO_STREAM_NAMED(name_, "Writing discretized trajectory to file");

    std::ofstream output_handle;
    output_handle.open(file_path.c_str());

    // Output header -------------------------------------------------------
    output_handle << "time_from_start,";
    for (std::size_t i = 0; i < trajectory.dimension; ++i)
    {
      output_handle << "j" << std::to_string(i) << "_pos,";
      output_handle << "j" << std::to_string(i) << "_vel,";
      output_handle << "j" << std::to_string(i) << "_acc,";
    }
    output_handle << std::endl;

    // Debug
    ROS_INFO_STREAM_NAMED(name_, " - Number of waypoints: " << trajectory.chunkslist.size());
    ROS_INFO_STREAM_NAMED(name_, " - Trajectory duration: " << trajectory.duration);

    // Discretize back into waypoints
    double dt = 0.01;

    std::vector<double> position;
    std::vector<double> velocity;
    std::vector<double> acceleration;
    position.resize(trajectory.dimension);
    velocity.resize(trajectory.dimension);
    acceleration.resize(trajectory.dimension);

    for (double time = 0; time < trajectory.duration; time+=dt)
    {
      trajectory.Eval(time, position);
      trajectory.Evald(time, velocity);
      trajectory.Evaldd(time, acceleration);

      output_handle.precision(10);
      output_handle << time << ",";
      for (std::size_t j = 0; j < position.size(); ++j)
      {
        output_handle << position[j] << ", ";
        output_handle << velocity[j] << ", ";
        output_handle << acceleration[j] << ", ";
      }
      output_handle << std::endl;
    }


    output_handle.close();
    ROS_INFO_STREAM_NAMED(name_, "Saved trajectory to " << file_path);
    return true;
  }

private:

  // --------------------------------------------------------

  // The short name of this class
  std::string name_ = "moveit_topp";

  // A shared node handle
  ros::NodeHandle nh_;

  // TOPP vars
  boost::shared_ptr<TOPP::Constraints> pconstraints_;
}; // end class

// Create boost pointers for this class
typedef boost::shared_ptr<MoveItTopp> MoveItToppPtr;
typedef boost::shared_ptr<const MoveItTopp> MoveItToppConstPtr;

} // namespace moveit_topp

#endif  // MOVEIT_TOPP_MOVEIT_TOPP_H
