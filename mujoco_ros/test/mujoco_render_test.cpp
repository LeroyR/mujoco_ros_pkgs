/*********************************************************************
 * Software License Agreement (BSD License)
 *
 *  Copyright (c) 2024, Bielefeld University
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

#include <gtest/gtest.h>

#include "mujoco_env_fixture.h"

#include <mujoco_ros/mujoco_env.h>
#include <mujoco_ros/common_types.h>
#include <mujoco_ros/offscreen_camera.h>
#include <mujoco_ros/util.h>

#include <ros/ros.h>
#include <sensor_msgs/Image.h>
#include <sensor_msgs/CameraInfo.h>
#include <sensor_msgs/image_encodings.h>
#include <vector>
#include <chrono>
#include <cmath>

int main(int argc, char **argv)
{
	::testing::InitGoogleTest(&argc, argv);
	ros::init(argc, argv, "mujoco_render_test");

	// Uncomment to enable debug output (useful for debugging failing tests)
	// ros::console::set_logger_level(ROSCONSOLE_DEFAULT_NAME, ros::console::levels::Debug);
	// ros::console::notifyLoggerLevelsChanged();

	// Create spinner to communicate with ROS
	ros::AsyncSpinner spinner(1);
	spinner.start();
	ros::NodeHandle nh;
	int ret = RUN_ALL_TESTS();

	// Stop spinner and shutdown ROS before returning
	spinner.stop();
	ros::shutdown();
	return ret;
}

using namespace mujoco_ros;
namespace mju = ::mujoco::sample_util;

std::vector<sensor_msgs::Image> rgb_images;
std::vector<sensor_msgs::Image> depth_images;
std::vector<sensor_msgs::Image> seg_images;
std::vector<sensor_msgs::CameraInfo> cam_infos;

// callbacks to save an image in a buffer
void rgb_cb(const sensor_msgs::Image::ConstPtr &msg)
{
	rgb_images.emplace_back(*msg);
}
void depth_cb(const sensor_msgs::Image::ConstPtr &msg)
{
	depth_images.emplace_back(*msg);
}
void seg_cb(const sensor_msgs::Image::ConstPtr &msg)
{
	seg_images.emplace_back(*msg);
}
void cam_info_cb(const sensor_msgs::CameraInfo::ConstPtr &msg)
{
	cam_infos.emplace_back(*msg);
}

TEST_F(BaseEnvFixture, Not_Headless_Warn)
{
	nh->setParam("no_render", false);
	std::string xml_path = ros::package::getPath("mujoco_ros") + "/test/camera_world.xml";
	env_ptr              = std::make_unique<MujocoEnvTestWrapper>("");

	env_ptr->startWithXML(xml_path);

	while (env_ptr->getOperationalStatus() != 0) { // wait for model to be loaded
		std::this_thread::sleep_for(std::chrono::milliseconds(3));
	}

	env_ptr->shutdown();
}

#if defined(USE_GLFW) || defined(USE_EGL) || defined(USE_OSMESA) // i.e. any render backend available
TEST_F(BaseEnvFixture, NoRender_Params_Correct)
{
	nh->setParam("no_render", true);
	std::string xml_path = ros::package::getPath("mujoco_ros") + "/test/camera_world.xml";
	env_ptr              = std::make_unique<MujocoEnvTestWrapper>("");

	env_ptr->startWithXML(xml_path);

	bool offscreen = true, headless = false;
	nh->getParam("render_offscreen", offscreen);
	nh->getParam("headless", headless);
	EXPECT_TRUE(headless);
	EXPECT_TRUE(env_ptr->settings_.headless);
	EXPECT_FALSE(offscreen);
	EXPECT_FALSE(env_ptr->settings_.render_offscreen);

	env_ptr->shutdown();
}

TEST_F(BaseEnvFixture, Headless_params_correct)
{
	nh->setParam("no_render", false);
	nh->setParam("headless", true);
	std::string xml_path = ros::package::getPath("mujoco_ros") + "/test/camera_world.xml";
	env_ptr              = std::make_unique<MujocoEnvTestWrapper>("");

	env_ptr->startWithXML(xml_path);

	EXPECT_TRUE(env_ptr->settings_.render_offscreen);
	EXPECT_TRUE(env_ptr->settings_.headless);

	env_ptr->shutdown();
}

TEST_F(BaseEnvFixture, RGB_Topic_Available)
{
	nh->setParam("no_render", false);
	nh->setParam("headless", true);
	nh->setParam("cam_config/test_cam/stream_type", rendering::streamType::RGB);

	std::string xml_path = ros::package::getPath("mujoco_ros") + "/test/camera_world.xml";
	env_ptr              = std::make_unique<MujocoEnvTestWrapper>("");

	env_ptr->startWithXML(xml_path);

	EXPECT_TRUE(env_ptr->settings_.headless);
	EXPECT_TRUE(env_ptr->settings_.render_offscreen);

	OffscreenRenderContext *offscreen = env_ptr->getOffscreenContext();
	ASSERT_EQ(offscreen->cams.size(), 1);
	EXPECT_STREQ(offscreen->cams[0]->cam_name_.c_str(), "test_cam");
	EXPECT_TRUE(offscreen->cams[0]->stream_type_ == rendering::streamType::RGB);

	ros::master::V_TopicInfo master_topics;
	ros::master::getTopics(master_topics);

	bool found = false;
	for (const auto &t : master_topics) {
		if (t.name == env_ptr->getHandleNamespace() + "/cameras/test_cam/rgb") {
			found = true;
			break;
		}
	}
	EXPECT_TRUE(found);

	env_ptr->shutdown();
}

TEST_F(BaseEnvFixture, DEPTH_Topic_Available)
{
	nh->setParam("no_render", false);
	nh->setParam("headless", true);
	nh->setParam("cam_config/test_cam/stream_type", rendering::streamType::DEPTH);

	std::string xml_path = ros::package::getPath("mujoco_ros") + "/test/camera_world.xml";
	env_ptr              = std::make_unique<MujocoEnvTestWrapper>("");

	env_ptr->startWithXML(xml_path);

	EXPECT_TRUE(env_ptr->settings_.headless);
	EXPECT_TRUE(env_ptr->settings_.render_offscreen);

	OffscreenRenderContext *offscreen = env_ptr->getOffscreenContext();
	ASSERT_EQ(offscreen->cams.size(), 1);
	EXPECT_STREQ(offscreen->cams[0]->cam_name_.c_str(), "test_cam");
	EXPECT_TRUE(offscreen->cams[0]->stream_type_ == rendering::streamType::DEPTH);

	ros::master::V_TopicInfo master_topics;
	ros::master::getTopics(master_topics);

	bool found = false;
	for (const auto &t : master_topics) {
		if (t.name == env_ptr->getHandleNamespace() + "/cameras/test_cam/depth") {
			found = true;
			break;
		}
	}
	EXPECT_TRUE(found);

	env_ptr->shutdown();
}

TEST_F(BaseEnvFixture, SEGMENTATION_Topic_Available)
{
	nh->setParam("no_render", false);
	nh->setParam("headless", true);
	nh->setParam("cam_config/test_cam/stream_type", rendering::streamType::SEGMENTED);

	std::string xml_path = ros::package::getPath("mujoco_ros") + "/test/camera_world.xml";
	env_ptr              = std::make_unique<MujocoEnvTestWrapper>("");

	env_ptr->startWithXML(xml_path);

	EXPECT_TRUE(env_ptr->settings_.headless);
	EXPECT_TRUE(env_ptr->settings_.render_offscreen);

	OffscreenRenderContext *offscreen = env_ptr->getOffscreenContext();
	ASSERT_EQ(offscreen->cams.size(), 1);
	EXPECT_STREQ(offscreen->cams[0]->cam_name_.c_str(), "test_cam");
	EXPECT_TRUE(offscreen->cams[0]->stream_type_ == rendering::streamType::SEGMENTED);

	ros::master::V_TopicInfo master_topics;
	ros::master::getTopics(master_topics);

	bool found = false;
	for (const auto &t : master_topics) {
		if (t.name == env_ptr->getHandleNamespace() + "/cameras/test_cam/segmented") {
			found = true;
			break;
		}
	}
	EXPECT_TRUE(found);
	env_ptr->shutdown();
}

TEST_F(BaseEnvFixture, RGB_DEPTH_Topic_Available)
{
	nh->setParam("no_render", false);
	nh->setParam("headless", true);
	nh->setParam("cam_config/test_cam/stream_type", rendering::streamType::RGB_D);

	std::string xml_path = ros::package::getPath("mujoco_ros") + "/test/camera_world.xml";
	env_ptr              = std::make_unique<MujocoEnvTestWrapper>("");

	env_ptr->startWithXML(xml_path);

	EXPECT_TRUE(env_ptr->settings_.headless);
	EXPECT_TRUE(env_ptr->settings_.render_offscreen);

	OffscreenRenderContext *offscreen = env_ptr->getOffscreenContext();
	ASSERT_EQ(offscreen->cams.size(), 1);
	EXPECT_STREQ(offscreen->cams[0]->cam_name_.c_str(), "test_cam");
	EXPECT_TRUE(offscreen->cams[0]->stream_type_ == rendering::streamType::RGB_D);

	ros::master::V_TopicInfo master_topics;
	ros::master::getTopics(master_topics);

	bool found_rgb = false, found_depth = false;
	for (const auto &t : master_topics) {
		if (t.name == env_ptr->getHandleNamespace() + "/cameras/test_cam/rgb") {
			found_rgb = true;
		} else if (t.name == env_ptr->getHandleNamespace() + "/cameras/test_cam/depth") {
			found_depth = true;
		}
		if (found_rgb && found_depth)
			break;
	}
	EXPECT_TRUE(found_rgb && found_depth);

	env_ptr->shutdown();
}

TEST_F(BaseEnvFixture, RGB_SEGMENTATION_Topic_Available)
{
	nh->setParam("no_render", false);
	nh->setParam("headless", true);
	nh->setParam("cam_config/test_cam/stream_type", rendering::streamType::RGB_S);

	std::string xml_path = ros::package::getPath("mujoco_ros") + "/test/camera_world.xml";
	env_ptr              = std::make_unique<MujocoEnvTestWrapper>("");

	env_ptr->startWithXML(xml_path);

	EXPECT_TRUE(env_ptr->settings_.headless);
	EXPECT_TRUE(env_ptr->settings_.render_offscreen);

	OffscreenRenderContext *offscreen = env_ptr->getOffscreenContext();
	ASSERT_EQ(offscreen->cams.size(), 1);
	EXPECT_STREQ(offscreen->cams[0]->cam_name_.c_str(), "test_cam");
	EXPECT_TRUE(offscreen->cams[0]->stream_type_ == rendering::streamType::RGB_S);

	ros::master::V_TopicInfo master_topics;
	ros::master::getTopics(master_topics);

	bool found_rgb = false, found_seg = false;
	for (const auto &t : master_topics) {
		if (t.name == env_ptr->getHandleNamespace() + "/cameras/test_cam/rgb") {
			found_rgb = true;
		} else if (t.name == env_ptr->getHandleNamespace() + "/cameras/test_cam/segmented") {
			found_seg = true;
		}
		if (found_rgb && found_seg)
			break;
	}
	EXPECT_TRUE(found_rgb && found_seg);

	env_ptr->shutdown();
}

TEST_F(BaseEnvFixture, DEPTH_SEGMENTATION_Topic_Available)
{
	nh->setParam("no_render", false);
	nh->setParam("headless", true);
	nh->setParam("cam_config/test_cam/stream_type", rendering::streamType::DEPTH_S);

	std::string xml_path = ros::package::getPath("mujoco_ros") + "/test/camera_world.xml";
	env_ptr              = std::make_unique<MujocoEnvTestWrapper>("");

	env_ptr->startWithXML(xml_path);

	EXPECT_TRUE(env_ptr->settings_.headless);
	EXPECT_TRUE(env_ptr->settings_.render_offscreen);

	OffscreenRenderContext *offscreen = env_ptr->getOffscreenContext();
	ASSERT_EQ(offscreen->cams.size(), 1);
	EXPECT_STREQ(offscreen->cams[0]->cam_name_.c_str(), "test_cam");
	EXPECT_TRUE(offscreen->cams[0]->stream_type_ == rendering::streamType::DEPTH_S);

	ros::master::V_TopicInfo master_topics;
	ros::master::getTopics(master_topics);

	bool found_depth = false, found_seg = false;
	for (const auto &t : master_topics) {
		if (t.name == env_ptr->getHandleNamespace() + "/cameras/test_cam/depth") {
			found_depth = true;
		} else if (t.name == env_ptr->getHandleNamespace() + "/cameras/test_cam/segmented") {
			found_seg = true;
		}
		if (found_depth && found_seg)
			break;
	}
	EXPECT_TRUE(found_depth && found_seg);
	env_ptr->shutdown();
}

TEST_F(BaseEnvFixture, RGB_DEPTH_SEGMENTATION_Topic_Available)
{
	nh->setParam("no_render", false);
	nh->setParam("headless", true);
	nh->setParam("cam_config/test_cam/stream_type", rendering::streamType::RGB_D_S);

	std::string xml_path = ros::package::getPath("mujoco_ros") + "/test/camera_world.xml";
	env_ptr              = std::make_unique<MujocoEnvTestWrapper>("");

	env_ptr->startWithXML(xml_path);

	EXPECT_TRUE(env_ptr->settings_.headless);
	EXPECT_TRUE(env_ptr->settings_.render_offscreen);

	OffscreenRenderContext *offscreen = env_ptr->getOffscreenContext();
	ASSERT_EQ(offscreen->cams.size(), 1);
	EXPECT_STREQ(offscreen->cams[0]->cam_name_.c_str(), "test_cam");
	EXPECT_TRUE(offscreen->cams[0]->stream_type_ == rendering::streamType::RGB_D_S);

	ros::master::V_TopicInfo master_topics;
	ros::master::getTopics(master_topics);

	bool found_rgb = false, found_depth = false, found_seg = false;
	for (const auto &t : master_topics) {
		if (t.name == env_ptr->getHandleNamespace() + "/cameras/test_cam/rgb") {
			found_rgb = true;
		} else if (t.name == env_ptr->getHandleNamespace() + "/cameras/test_cam/depth") {
			found_depth = true;
		} else if (t.name == env_ptr->getHandleNamespace() + "/cameras/test_cam/segmented") {
			found_seg = true;
		}
		if (found_rgb && found_depth && found_seg)
			break;
	}
	EXPECT_TRUE(found_rgb && found_depth && found_seg);
	env_ptr->shutdown();

	env_ptr->shutdown();
}

TEST_F(BaseEnvFixture, Default_Cam_Settings)
{
	nh->setParam("no_render", false);
	nh->setParam("headless", true);
	std::string xml_path = ros::package::getPath("mujoco_ros") + "/test/camera_world.xml";
	env_ptr              = std::make_unique<MujocoEnvTestWrapper>("");

	env_ptr->startWithXML(xml_path);

	EXPECT_TRUE(env_ptr->settings_.headless);
	EXPECT_TRUE(env_ptr->settings_.render_offscreen);

	OffscreenRenderContext *offscreen = env_ptr->getOffscreenContext();
	ASSERT_EQ(offscreen->cams.size(), 1);

	// Check default camera settings
	EXPECT_EQ(offscreen->cams[0]->cam_id_, 0);
	EXPECT_STREQ(offscreen->cams[0]->cam_name_.c_str(), "test_cam");
	EXPECT_EQ(offscreen->cams[0]->stream_type_, rendering::streamType::RGB);
	EXPECT_EQ(offscreen->cams[0]->pub_freq_, 15);
	EXPECT_EQ(offscreen->cams[0]->width_, 720);
	EXPECT_EQ(offscreen->cams[0]->height_, 480);

	env_ptr->shutdown();
}

TEST_F(BaseEnvFixture, Resolution_Settings)
{
	nh->setParam("no_render", false);
	nh->setParam("headless", true);
	nh->setParam("cam_config/test_cam/width", 640);
	nh->setParam("cam_config/test_cam/height", 480);

	std::string xml_path = ros::package::getPath("mujoco_ros") + "/test/camera_world.xml";
	env_ptr              = std::make_unique<MujocoEnvTestWrapper>("");

	env_ptr->startWithXML(xml_path);

	EXPECT_TRUE(env_ptr->settings_.headless);
	EXPECT_TRUE(env_ptr->settings_.render_offscreen);

	OffscreenRenderContext *offscreen = env_ptr->getOffscreenContext();
	ASSERT_EQ(offscreen->cams.size(), 1);

	// Check camera settings
	EXPECT_EQ(offscreen->cams[0]->cam_id_, 0);
	EXPECT_STREQ(offscreen->cams[0]->cam_name_.c_str(), "test_cam");
	EXPECT_EQ(offscreen->cams[0]->stream_type_, rendering::streamType::RGB);
	EXPECT_EQ(offscreen->cams[0]->pub_freq_, 15);
	EXPECT_EQ(offscreen->cams[0]->width_, 640);
	EXPECT_EQ(offscreen->cams[0]->height_, 480);

	env_ptr->shutdown();
}

TEST_F(BaseEnvFixture, RGB_Published_Correctly)
{
	nh->setParam("no_render", false);
	nh->setParam("headless", true);
	nh->setParam("unpause", false);
	nh->setParam("cam_config/test_cam/frequency", 30);
	nh->setParam("cam_config/test_cam/width", 72);
	nh->setParam("cam_config/test_cam/height", 48);

	std::string xml_path = ros::package::getPath("mujoco_ros") + "/test/camera_world.xml";
	env_ptr              = std::make_unique<MujocoEnvTestWrapper>("");

	// Clear image buffers
	rgb_images.clear();
	cam_infos.clear();

	// Subscribe to topic
	ros::Subscriber rgb_sub  = nh->subscribe("cameras/test_cam/rgb", 1, rgb_cb);
	ros::Subscriber info_sub = nh->subscribe("cameras/test_cam/camera_info", 1, cam_info_cb);

	env_ptr->startWithXML(xml_path);

	EXPECT_TRUE(env_ptr->settings_.headless);
	EXPECT_TRUE(env_ptr->settings_.render_offscreen);

	OffscreenRenderContext *offscreen = env_ptr->getOffscreenContext();
	ASSERT_EQ(offscreen->cams.size(), 1);

	// Check default camera settings
	EXPECT_EQ(offscreen->cams[0]->cam_id_, 0);
	EXPECT_STREQ(offscreen->cams[0]->cam_name_.c_str(), "test_cam");
	EXPECT_EQ(offscreen->cams[0]->stream_type_, rendering::streamType::RGB);
	EXPECT_EQ(offscreen->cams[0]->pub_freq_, 30);
	EXPECT_EQ(offscreen->cams[0]->width_, 72);
	EXPECT_EQ(offscreen->cams[0]->height_, 48);

	env_ptr->step(1);
	// Wait for image to be published with 100ms timeout
	float seconds = 0;
	while (rgb_images.size() == 0 && seconds < .1) {
		std::this_thread::sleep_for(std::chrono::milliseconds(1));
		seconds += 0.001;
	}
	EXPECT_LT(seconds, .1) << "RGB image not published within 100ms";

	ASSERT_EQ(cam_infos.size(), 1);
	ASSERT_EQ(rgb_images.size(), 1);

	ros::Time t1 = ros::Time::now();
	EXPECT_EQ(rgb_images[0].header.stamp, t1);
	EXPECT_EQ(rgb_images[0].header.frame_id, "test_cam_optical_frame");
	EXPECT_EQ(rgb_images[0].width, 72);
	EXPECT_EQ(rgb_images[0].height, 48);
	EXPECT_EQ(rgb_images[0].encoding, sensor_msgs::image_encodings::RGB8);

	EXPECT_EQ(cam_infos[0].header.stamp, t1);

	rgb_images.clear();
	cam_infos.clear();

	env_ptr->shutdown();
}

TEST_F(BaseEnvFixture, Cam_Timing_Correct)
{
	nh->setParam("no_render", false);
	nh->setParam("headless", true);
	nh->setParam("unpause", false);
	nh->setParam("cam_config/test_cam/frequency", 30);
	nh->setParam("cam_config/test_cam/width", 72);
	nh->setParam("cam_config/test_cam/height", 48);

	std::string xml_path = ros::package::getPath("mujoco_ros") + "/test/camera_world.xml";
	env_ptr              = std::make_unique<MujocoEnvTestWrapper>("");

	// Clear image buffers
	rgb_images.clear();
	cam_infos.clear();

	// Subscribe to topic
	ros::Subscriber rgb_sub  = nh->subscribe("cameras/test_cam/rgb", 1, rgb_cb);
	ros::Subscriber info_sub = nh->subscribe("cameras/test_cam/camera_info", 1, cam_info_cb);

	env_ptr->startWithXML(xml_path);

	EXPECT_TRUE(env_ptr->settings_.headless);
	EXPECT_TRUE(env_ptr->settings_.render_offscreen);

	env_ptr->step(1);
	// Wait for image to be published with 100ms timeout
	float seconds = 0;
	while (rgb_images.size() == 0 && seconds < .1) {
		std::this_thread::sleep_for(std::chrono::milliseconds(1));
		seconds += 0.001;
	}
	EXPECT_LT(seconds, .1) << "RGB image not published within 100ms";

	OffscreenRenderContext *offscreen = env_ptr->getOffscreenContext();
	ASSERT_EQ(offscreen->cams.size(), 1);

	// Check default camera settings
	EXPECT_EQ(offscreen->cams[0]->cam_id_, 0);
	EXPECT_STREQ(offscreen->cams[0]->cam_name_.c_str(), "test_cam");
	EXPECT_EQ(offscreen->cams[0]->stream_type_, rendering::streamType::RGB);
	EXPECT_EQ(offscreen->cams[0]->pub_freq_, 30);

	// Wait for image to be published with 100ms timeout
	seconds = 0;
	while (rgb_images.size() == 0 && seconds < 0.1) {
		std::this_thread::sleep_for(std::chrono::milliseconds(1));
		seconds += 0.001;
	}
	EXPECT_LT(seconds, 0.1) << "RGB image not published within 100ms";

	ASSERT_EQ(cam_infos.size(), 1);
	ASSERT_EQ(rgb_images.size(), 1);

	ros::Time t1 = ros::Time::now();
	// Step the simulation to as to trigger the camera rendering
	mjModel *m = env_ptr->getModelPtr();
	ROS_INFO_STREAM("Next pub time will be " << (1.0 / 30.0) << " seconds from now. Thus in "
	                                         << std::ceil((1.0 / 30.0) / m->opt.timestep) << " steps.");
	int n_steps = std::ceil((1.0 / 30.0) / env_ptr->getModelPtr()->opt.timestep);

	env_ptr->step(n_steps - 1);
	// wait a little
	std::this_thread::sleep_for(std::chrono::milliseconds(5));

	// should not have received a new image yet
	ASSERT_EQ(cam_infos.size(), 1);
	ASSERT_EQ(rgb_images.size(), 1);

	env_ptr->step(1);
	// Wait for image to be published with 100ms timeout
	seconds = 0;
	while (rgb_images.size() < 2 && seconds < 0.1) {
		std::this_thread::sleep_for(std::chrono::milliseconds(1));
		seconds += 0.001;
	}
	// should now have received new image
	EXPECT_LT(seconds, 0.1) << "RGB image not published within 100ms";

	ASSERT_EQ(cam_infos.size(), 2);
	ASSERT_EQ(rgb_images.size(), 2);
	ros::Time t2 = ros::Time::now();

	ASSERT_EQ(rgb_images[0].header.stamp, t1);
	ASSERT_EQ(rgb_images[1].header.stamp, t2);

	ASSERT_EQ(cam_infos[0].header.stamp, t1);
	ASSERT_EQ(cam_infos[1].header.stamp, t2);

	// int n_steps = std::ceil((1.0 / 30.0) / env_ptr->getModelPtr()->opt.timestep);
	ros::Time t3 = t2 + ros::Duration((std::ceil((1.0 / 30.0) / m->opt.timestep)) * m->opt.timestep);
	// ros::Time t3 = t2 + (t2 - t1);
	// Step over next image trigger but before trigger after that
	env_ptr->step(2 * n_steps - 1);

	ASSERT_EQ(cam_infos.size(), 3);
	ASSERT_EQ(rgb_images.size(), 3);

	// Check that the timestamps are as expected
	EXPECT_EQ(rgb_images[2].header.stamp, cam_infos[2].header.stamp);
	EXPECT_EQ(rgb_images[2].header.stamp, t3);
	EXPECT_EQ(cam_infos[2].header.stamp, t3);

	rgb_images.clear();
	cam_infos.clear();

	env_ptr->shutdown();
}

TEST_F(BaseEnvFixture, RGB_Image_Dtype)
{
	nh->setParam("no_render", false);
	nh->setParam("headless", true);
	nh->setParam("unpause", false);
	nh->setParam("cam_config/test_cam/width", 72);
	nh->setParam("cam_config/test_cam/height", 48);

	std::string xml_path = ros::package::getPath("mujoco_ros") + "/test/camera_world.xml";
	env_ptr              = std::make_unique<MujocoEnvTestWrapper>("");

	// Clear image buffers
	rgb_images.clear();

	// Subscribe to topic
	ros::Subscriber rgb_sub = nh->subscribe("cameras/test_cam/rgb", 1, rgb_cb);

	env_ptr->startWithXML(xml_path);

	EXPECT_TRUE(env_ptr->settings_.headless);
	EXPECT_TRUE(env_ptr->settings_.render_offscreen);

	env_ptr->step(1);

	OffscreenRenderContext *offscreen = env_ptr->getOffscreenContext();
	ASSERT_EQ(offscreen->cams.size(), 1);
	EXPECT_STREQ(offscreen->cams[0]->cam_name_.c_str(), "test_cam");
	EXPECT_EQ(offscreen->cams[0]->stream_type_, rendering::streamType::RGB);
	EXPECT_EQ(offscreen->cams[0]->width_, 72);
	EXPECT_EQ(offscreen->cams[0]->height_, 48);

	// Wait for image to be published with 100ms timeout
	float seconds = 0;
	while (rgb_images.size() == 0 && seconds < .1) {
		std::this_thread::sleep_for(std::chrono::milliseconds(1));
		seconds += 0.001;
	}
	EXPECT_LT(seconds, .1) << "RGB image not published within 100ms";

	ASSERT_EQ(rgb_images.size(), 1);
	EXPECT_EQ(rgb_images[0].data.size(), 72 * 48 * 3);
	EXPECT_EQ(rgb_images[0].encoding, sensor_msgs::image_encodings::RGB8);

	rgb_images.clear();

	env_ptr->shutdown();
}

TEST_F(BaseEnvFixture, DEPTH_Image_Dtype)
{
	nh->setParam("no_render", false);
	nh->setParam("headless", true);
	nh->setParam("unpause", false);
	nh->setParam("cam_config/test_cam/stream_type", rendering::streamType::DEPTH);
	nh->setParam("cam_config/test_cam/width", 72);
	nh->setParam("cam_config/test_cam/height", 48);

	std::string xml_path = ros::package::getPath("mujoco_ros") + "/test/camera_world.xml";
	env_ptr              = std::make_unique<MujocoEnvTestWrapper>("");

	// Clear image buffers
	depth_images.clear();

	// Subscribe to topic
	ros::Subscriber depth_sub = nh->subscribe("cameras/test_cam/depth", 1, depth_cb);

	env_ptr->startWithXML(xml_path);

	EXPECT_TRUE(env_ptr->settings_.headless);
	EXPECT_TRUE(env_ptr->settings_.render_offscreen);

	env_ptr->step(1);

	OffscreenRenderContext *offscreen = env_ptr->getOffscreenContext();
	ASSERT_EQ(offscreen->cams.size(), 1);
	EXPECT_STREQ(offscreen->cams[0]->cam_name_.c_str(), "test_cam");
	EXPECT_EQ(offscreen->cams[0]->stream_type_, rendering::streamType::DEPTH);
	EXPECT_EQ(offscreen->cams[0]->width_, 72);
	EXPECT_EQ(offscreen->cams[0]->height_, 48);

	// Wait for image to be published with 100ms timeout
	float seconds = 0;
	while (depth_images.size() == 0 && seconds < .1) {
		std::this_thread::sleep_for(std::chrono::milliseconds(1));
		seconds += 0.001;
	}
	EXPECT_LT(seconds, .1) << "Depth image not published within 100ms";

	ASSERT_EQ(depth_images.size(), 1);
	EXPECT_EQ(depth_images[0].width, 72);
	EXPECT_EQ(depth_images[0].height, 48);
	EXPECT_EQ(depth_images[0].encoding, sensor_msgs::image_encodings::TYPE_32FC1);

	depth_images.clear();
	env_ptr->shutdown();
}

TEST_F(BaseEnvFixture, SEGMENTED_Image_Dtype)
{
	nh->setParam("no_render", false);
	nh->setParam("headless", true);
	nh->setParam("unpause", false);
	nh->setParam("cam_config/test_cam/stream_type", rendering::streamType::SEGMENTED);
	nh->setParam("cam_config/test_cam/width", 72);
	nh->setParam("cam_config/test_cam/height", 48);

	std::string xml_path = ros::package::getPath("mujoco_ros") + "/test/camera_world.xml";
	env_ptr              = std::make_unique<MujocoEnvTestWrapper>("");

	// Clear image buffers
	seg_images.clear();

	// Subscribe to topic
	ros::Subscriber seg_sub = nh->subscribe("cameras/test_cam/segmented", 1, seg_cb);

	env_ptr->startWithXML(xml_path);

	EXPECT_TRUE(env_ptr->settings_.headless);
	EXPECT_TRUE(env_ptr->settings_.render_offscreen);

	env_ptr->step(1);

	OffscreenRenderContext *offscreen = env_ptr->getOffscreenContext();
	ASSERT_EQ(offscreen->cams.size(), 1);
	EXPECT_STREQ(offscreen->cams[0]->cam_name_.c_str(), "test_cam");
	EXPECT_EQ(offscreen->cams[0]->stream_type_, rendering::streamType::SEGMENTED);
	EXPECT_EQ(offscreen->cams[0]->width_, 72);
	EXPECT_EQ(offscreen->cams[0]->height_, 48);

	// Wait for image to be published with 100ms timeout
	float seconds = 0;
	while (seg_images.size() == 0 && seconds < .1) {
		std::this_thread::sleep_for(std::chrono::milliseconds(1));
		seconds += 0.001;
	}
	EXPECT_LT(seconds, .1) << "Segmentation image not published within 100ms";

	ASSERT_EQ(seg_images.size(), 1);
	EXPECT_EQ(seg_images[0].data.size(), 72 * 48 * 1);

	seg_images.clear();
	env_ptr->shutdown();
}

#endif // defined(USE_GLFW) || defined(USE_EGL) || defined(USE_OSMESA) // i.e. any render backend available

#if !defined(USE_GLFW) && !defined(USE_EGL) && !defined(USE_OSMESA) // i.e. no render backend available
TEST_F(BaseEnvFixture, No_Render_Backend_Headless_Warn)
{
	nh->setParam("headless", true);
	std::string xml_path = ros::package::getPath("mujoco_ros") + "/test/camera_world.xml";
	env_ptr              = std::make_unique<MujocoEnvTestWrapper>("");

	env_ptr->startWithXML(xml_path);

	while (env_ptr->getOperationalStatus() != 0) { // wait for model to be loaded
		std::this_thread::sleep_for(std::chrono::milliseconds(3));
	}

	EXPECT_TRUE(env_ptr->settings_.headless);
	EXPECT_FALSE(env_ptr->settings_.render_offscreen);

	OffscreenRenderContext *offscreen = env_ptr->getOffscreenContext();
	EXPECT_TRUE(offscreen->cams.size() == 0);

	env_ptr->shutdown();
}
#endif // !defined(USE_GLFW) && !defined(USE_EGL) && !defined(USE_OSMESA) // i.e. no render backend available

// TODOs:
// - Tests for offscreen rendering
//   + Test if RGB topic is available (done)
//   + Test if Depth topic is available (done)
//   + Test if Segmentation topic is available (done)
//   + Test if CameraInfo topic is available
//   + Test default camera settings
//   + Resolution settings
//   + test all combinations of RGB, Depth, and Segmentation (done)
//   + Test that camera timings are correct by stepping through the simulation and checking the timestamps
//   + Test camera image data types
