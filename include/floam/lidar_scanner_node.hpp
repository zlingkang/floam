// Author of FLOAM: Wang Han 
// Email wh200720041@gmail.com
// Homepage https://wanghan.pro
#ifndef FLOAM__LIDAR_SCANNER_NODE_HPP_
#define FLOAM__LIDAR_SCANNER_NODE_HPP_

#include <ros/ros.h>
#include <nodelet/nodelet.h>
#include <sensor_msgs/PointCloud2.h>

#include <pcl_conversions/pcl_conversions.h>
#include <pcl/point_cloud.h>
#include <pcl/point_types.h>

//local lib
#include "floam/lidar.hpp"


namespace floam
{
namespace lidar
{


class ScanningLidarNode : public nodelet::Nodelet
{
public:
  ScanningLidarNode();
  ~ScanningLidarNode();

  void onInit();

  void handlePoints(const sensor_msgs::PointCloud2ConstPtr &lidarCloudMsg);

private:
  ros::NodeHandle m_nodeHandle;

  ros::Subscriber m_subPoints;

  ros::Publisher m_pubEdgePoints;
  ros::Publisher m_pubSurfacePoints;
  ros::Publisher m_pubPointsFiltered;

  std::queue<sensor_msgs::PointCloud2ConstPtr> m_points;

  Lidar<floam::lidar::Scanner> m_lidar;
};

}  // namespace lidar
}  // namespace floam

#endif  // FLOAM__LIDAR_SCANNER_NODE_HPP_