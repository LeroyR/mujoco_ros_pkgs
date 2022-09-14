/*********************************************************************
 * Software License Agreement (BSD License)
 *
 *  Copyright (c) 2022, Bielefeld University
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
 *   * Neither the name of Bielefeld University nor the names of its
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

/* Authors: David P. Leins */

#include <ros/ros.h>

#include <mujoco_ros_msgs/BootstrapNS.h>
#include <mujoco_ros_msgs/ShutdownNS.h>
#include <mujoco_ros/mujoco_env.h>

#include <sstream>

namespace MujocoSim {

namespace environments {
void assignData(mjData *data, MujocoEnvPtr env)
{
	env->data.reset(data);
	env_map_[data] = env;
}

MujocoEnvPtr getEnv(mjData *data)
{
	if (env_map_.find(data) != env_map_.end()) {
		return env_map_[data];
	} else
		return nullptr;
}

} // end namespace environments

MujocoEnvParallel::MujocoEnvParallel(const std::string &ros_ns, const std::string &launchfile,
                                     const std::vector<std::string> &launch_args)
    : MujocoEnv(ros_ns), launchfile(launchfile), launch_args(launch_args)
{
	if (!launchfile.empty())
		bootstrapNamespace();
}

void MujocoEnvParallel::bootstrapNamespace()
{
	mujoco_ros_msgs::BootstrapNS bootstrap_msg;
	bootstrap_msg.request.ros_namespace = name;
	bootstrap_msg.request.launchfile    = launchfile;
	bootstrap_msg.request.args          = launch_args;

	if (!ros::service::waitForService("/bootstrap_ns", ros::Duration(5))) {
		ROS_ERROR_NAMED("mujoco_env",
		                "Timeout while waiting for namespace bootstrapping node under topic '/bootstrap_ns'. "
		                "Is it started correctly?");
		return;
	}

	if (!ros::service::call("/bootstrap_ns", bootstrap_msg))
		ROS_ERROR_STREAM_NAMED("mujoco_env", "Error while bootstrapping ROS environment for namespace '" << name << "'");
}

void MujocoEnv::reload()
{
	ROS_DEBUG_STREAM_NAMED("mujoco_env", "(re)loading MujocoPlugins ... [" << name << "]");
	cb_ready_plugins.clear();
	plugins.clear();

	XmlRpc::XmlRpcValue plugin_config;
	if (plugin_utils::parsePlugins(nh, plugin_config)) {
		plugin_utils::registerPlugins(nh, plugin_config, plugins);
	}

	for (const auto &plugin : plugins) {
		if (plugin->safe_load(model, data)) {
			cb_ready_plugins.push_back(plugin);
		}
	}
}

void MujocoEnv::reset()
{
	for (const auto &plugin : plugins) {
		plugin->safe_reset();
	}
}

const std::vector<MujocoPluginPtr> MujocoEnv::getPlugins()
{
	return plugins;
}

void MujocoEnv::runControlCbs()
{
	for (const auto &plugin : cb_ready_plugins) {
		plugin->controlCallback(model, data);
	}
}

void MujocoEnv::runPassiveCbs()
{
	for (const auto &plugin : cb_ready_plugins) {
		plugin->passiveCallback(model, data);
	}
}

// TODO: Change once rendering is moved to external functionality
void MujocoEnv::runRenderCbs(mjvScene *scene)
{
	if (plugins.empty())
		return;
	for (const auto &plugin : cb_ready_plugins) {
		plugin->renderCallback(model, data, scene);
	}
}

void MujocoEnv::runLastStageCbs()
{
	for (const auto &plugin : cb_ready_plugins) {
		plugin->lastStageCallback(model, data);
	}
}

MujocoEnv::~MujocoEnv()
{
	cb_ready_plugins.clear();
	plugins.clear();
	model.reset();
	data.reset();
	ctrlnoise = nullptr;
	nh.reset();
}

MujocoEnvParallel::~MujocoEnvParallel()
{
	mujoco_ros_msgs::ShutdownNS shutdown_msg;
	shutdown_msg.request.ros_namespace = name;

	ros::service::call("/shutdown_ns", shutdown_msg);
}

} // namespace MujocoSim
