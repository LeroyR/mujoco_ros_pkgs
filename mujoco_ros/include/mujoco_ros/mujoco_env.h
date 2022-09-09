#pragma once

#include <thread>
#include <ros/ros.h>

#include <mujoco_ros/common_types.h>
#include <mujoco_ros/plugin_utils.h>

namespace unit_testing {
class MujocoRosFixture;
}
namespace MujocoSim {

namespace environments {
static std::map<mjData *, MujocoEnvPtr> env_map_;
void assignData(mjData *data, MujocoEnvPtr env);
MujocoEnvPtr getEnv(mjData *data);
} // end namespace environments

struct MujocoEnv
{
public:
	/**
	 * @brief Construct a new Mujoco Env object in a given namespace.
	 *
	 * @param[in] name namespace of the environment.
	 */
	MujocoEnv(std::string name) : name(name)
	{
		nh.reset(new ros::NodeHandle(name));
		ROS_DEBUG_STREAM_NAMED("mujoco_env", "New env created with namespace: " << name);
	};

	~MujocoEnv();

	MujocoEnv(const MujocoEnv &) = delete;

	/// Pointer to mjModel
	mjModelPtr model;
	/// Pointer to mjData
	mjDataPtr data;
	/// Noise to apply to control signal
	mjtNum *ctrlnoise = nullptr;
	/// Pointer to ros nodehandle in env namespace
	ros::NodeHandlePtr nh;
	/// Env namespace
	std::string name;

	/**
	 * @brief Calls reload functions of all members depeding on mjData.
	 * This function is called when a new mjData object is assigned to the environment.
	 */
	void reload();

	/**
	 * @brief Calls reset functions of all members depending on mjData.
	 * This function is called on a reset request by the user. mjModel and mjData are not reinitialized.
	 */
	void reset();

	const std::vector<MujocoPluginPtr> getPlugins();

	void runControlCbs();
	void runPassiveCbs();
	void runRenderCbs(mjvScene *scene);
	void runLastStageCbs();

protected:
	XmlRpc::XmlRpcValue rpc_plugin_config;
	std::vector<MujocoPluginPtr> plugins;

private:
	std::vector<MujocoPluginPtr> cb_ready_plugins;
	friend class ::unit_testing::MujocoRosFixture;
};

struct MujocoEnvParallel : MujocoEnv
{
public:
	/**
	 * @brief Construct a new Mujoco Env object
	 *
	 * @param[in] ros_ns namespace of the environment used in ros.
	 * @param[in] launchfile (optional) launchfile that will be started to bootstrap a ros environment for the
	 * namespace.
	 * @param[in] launch_args (optional) arguments to start the supplied launchfile with.
	 */
	MujocoEnvParallel(const std::string &ros_ns, const std::string &launchfile = "",
	                  const std::vector<std::string> &launch_args = std::vector<std::string>());
	MujocoEnvParallel(const MujocoEnvParallel &) = delete;
	~MujocoEnvParallel();

	// Env loop stepping thread
	std::thread *loop_thread = nullptr;
	// Bool to notify loop thread it should be stopped.
	std::atomic_bool stop_loop;

	std::string launchfile;
	std::vector<std::string> launch_args;

	/**
	 * @brief Runs the launchfile with the supplied list of arguments, if any was given.
	 */
	void bootstrapNamespace();
};

} // end namespace MujocoSim
