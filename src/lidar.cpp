
/// Major rewrite Author: Evan Flynn

// Original Author of FLOAM: Wang Han 
// Email wh200720041@gmail.com
// Homepage https://wanghan.pro

#include <pcl/features/organized_edge_detection.h>
#include <pcl/features/integral_image_normal.h>
#include <pcl/features/normal_3d.h>
#include <pcl/impl/point_types.hpp>

#include "floam/lidar_utils.hpp"
#include "floam/lidar.hpp"

#include <iostream>

namespace floam
{
namespace lidar
{

/// Scanner type
template <>
void Lidar<floam::lidar::Scanner>::detectEdges(
  const pcl::PointCloud<pcl::PointXYZ>::Ptr & points,
  pcl::PointCloud<pcl::PointXYZL>::Ptr & edges)
{
  int N_SCANS = m_settings.lines;
  std::vector<pcl::PointCloud<pcl::PointXYZ>::Ptr> lidarScans;

  for(int i = 0; i < N_SCANS; i++){
      lidarScans.push_back(pcl::PointCloud<pcl::PointXYZ>::Ptr(new pcl::PointCloud<pcl::PointXYZ>()));
  }

  // initialize kdTree for nearest neighbor search
  pcl::KdTreeFLANN<pcl::PointXYZ> kdTree;
  kdTree.setInputCloud(points);

  // radius around point to search for other points
  double radius = m_settings.searchRadius;
  /// TODO(flynneva): use downsampling technique instead of just skipping points?
  /// number of points to skip
  int increment = m_settings.skipPoints;
  std::vector<int> pointIdxRadiusSearch;
  std::vector<float> pointRadiusSquaredDistance;

  double diffX, diffY, diffZ = 0;
  double diffTotal, diffLeft, diffRight = 0;
  double tempX, tempY, tempZ = 0;

  int index = 0;
  // separate out pointcloud into different scan lines
  // essentially trying to make it an "ordered pointcloud"
  for (int i = 0; i < (int) points->points.size(); i+=increment)
  {
    int scanID=0;

    /// calculate radial distance to point
    double distance =
      sqrt(
        points->points[i].x * points->points[i].x +
        points->points[i].y * points->points[i].y);

    // filter pointcloud by min and max distance settings
    if (distance < m_settings.common.limits.distance.min ||
        distance > m_settings.common.limits.distance.max)
    {
      // distance out of min/max limits, go to next point
      continue;
    }
    double angle = atan(points->points[i].z / distance) * 180 / M_PI;
    
    if (N_SCANS == 16) {
      scanID = int((angle + 15) / 2 + 0.5);
      if (scanID > (N_SCANS - 1) || scanID < 0) {
        continue;
      }
    } else if (N_SCANS == 32) {
        scanID = int((angle + 92.0/3.0) * 3.0 / 4.0);
        if (scanID > (N_SCANS - 1) || scanID < 0) {
          continue;
        }
    } else if (N_SCANS == 64) {   
      if (angle >= -8.83) {
        scanID = int((2 - angle) * 3.0 + 0.5);
      } else {
        scanID = N_SCANS / 2 + int((-8.83 - angle) * 2.0 + 0.5);
      }

      if (angle > 2 || angle < -24.33 || scanID > 63 || scanID < 0) {
        continue;
      }
    } else {
      // assume single scan, use kdTree to do nearest K neighbor search
      if(
        kdTree.radiusSearch(
          points->points[i], radius, pointIdxRadiusSearch,
          pointRadiusSquaredDistance) > 0)
      {

        // TODO(flynneva): this should be its own function, used multiple times within code
        // classifyPoint()
        // reset diff
        diffTotal = 0;
        diffX = 0;
        diffY = 0;
        diffZ = 0;

        // points found within radius
        std::cout << "=================" << std::endl;
        std::cout << "points found: " << pointIdxRadiusSearch.size() << std::endl;
        for(std::size_t j = 0; j < pointIdxRadiusSearch.size(); ++j) {
          diffX += (*points)[pointIdxRadiusSearch[j]].x;
          diffY += (*points)[pointIdxRadiusSearch[j]].y;
          diffZ += (*points)[pointIdxRadiusSearch[j]].z;
        }

        /// subtract actual point * num points found
        diffX = diffX - ((int)pointIdxRadiusSearch.size() * points->points[i].x);
        diffY = diffY - ((int)pointIdxRadiusSearch.size() * points->points[i].y);
        diffZ = diffZ - ((int)pointIdxRadiusSearch.size() * points->points[i].z);

        std::cout << "diffX: " << diffX << std::endl;
        std::cout << "diffY: " << diffY << std::endl;
        std::cout << "diffZ: " << diffZ << std::endl;
        /// calculate diff
        diffTotal = diffX * diffX + diffY * diffY + diffZ * diffZ;

        std::cout << "diffTotal: " << diffTotal << std::endl;

        // get current point
        pcl::PointXYZL tempPointL;
        tempPointL.x = points->points[i].x;
        tempPointL.y = points->points[i].y;
        tempPointL.z = points->points[i].z;
        
        if (diffTotal <= m_settings.common.limits.edgeThreshold) {
          // value is smaller than threshold, so assume it is a surface
          tempPointL.label = 0;
        } else {
          // value is larger than threshold, assume it is an edge
          tempPointL.label = 1;
        }

        /// end function

        edges->push_back(tempPointL);
        continue;
      }
    }
    lidarScans[scanID]->push_back(points->points[i]);
  }

  // check to see if we already detected the edges
  if (!edges->empty()) {
    return;
  }

  for(int i = 0; i < N_SCANS; i++) {
    /// (flynneva) why 131?
    // if pointcloud is too small, ignore it
    if(lidarScans[i]->points.size() < 131) {
      continue;
    }

    /// TODO(flynneva): separate out this bit to function?
    /// 
    /// input: points, sectorSize
    /// output: cloudCurvature
    std::vector<floam::lidar::Double2d> cloudCurvature;
    // size of window to calculate curvature of
    // TODO(flynneva): make windowSize adjustable
    int windowSize = 10;
    int halfWindow = windowSize / 2;
    // (TODO: flynneva): do we really need to subtract the windowSize from the total points?
    int totalPoints = lidarScans[i]->points.size() - windowSize;
  
    // calculate number of sectors and the length/size of each sector
    // TODO(flynneva): make sectors a parameter?
    m_settings.common.limits.sectors = 6;
    int sectorSize = (int)lidarScans[i]->points.size() / m_settings.common.limits.sectors;

    for(int j = halfWindow; j < (int)(lidarScans[i]->points.size() - halfWindow); j += 1) {
      // reset diff's at new point
      diffX = 0;
      diffY = 0;
      diffZ = 0;
      diffTotal = 0;
      diffLeft = 0;
      diffRight = 0;

      for (int k = -halfWindow; k <= halfWindow; k++) {
        // the middle point of each window is the "baseline"
        if (k == 0) {
          // calculate diff for window so far (-halfWindow to 0)
          tempX = diffX - (halfWindow * lidarScans[i]->points[j].x);
          tempY = diffY - (halfWindow * lidarScans[i]->points[j].y);
          tempZ = diffZ - (halfWindow * lidarScans[i]->points[j].z);
          diffLeft = tempX * tempX + tempY * tempY + tempZ * tempZ;

          // subtract middle point * size (because we add the rest of the points)
          diffX -= windowSize * lidarScans[i]->points[j].x;
          diffY -= windowSize * lidarScans[i]->points[j].y;
          diffZ -= windowSize * lidarScans[i]->points[j].z;
        } else {
          // add points left and right of middle of window
          diffX += lidarScans[i]->points[j + k].x;
          diffY += lidarScans[i]->points[j + k].y;
          diffZ += lidarScans[i]->points[j + k].z;
        }
      }
      // if diff total is large, sector is very curved or could be an edge
      // j - 1 to store actual location in points index
      diffTotal = diffX * diffX + diffY * diffY + diffZ * diffZ;
      diffRight = diffTotal - diffLeft;
      floam::lidar::Double2d distance(j - 1, diffTotal, diffLeft, diffRight);
      cloudCurvature.push_back(distance);
    }
    /// end of potential cloudCurvature func

    double halfThreshold = m_settings.common.limits.edgeThreshold / 2;
    /// loop over sectors
    for(int j = 0; j < m_settings.common.limits.sectors; j++) {
      int sectorStart = sectorSize * j;
      int sectorEnd = sectorSize * (j + 1) - 1;
      // for last sector, sectorEnd is last point (may or may not be the same size as the rest of the sectors)
      if (j == (m_settings.common.limits.sectors - 1)) {
        sectorEnd = totalPoints - 1; 
      }

      std::vector<floam::lidar::Double2d> subCloudCurvature(
        cloudCurvature.begin() + sectorStart,
        cloudCurvature.begin() + sectorEnd); 

      // sort diff's within sector
      std::sort(subCloudCurvature.begin(), subCloudCurvature.end(),
        [](const floam::lidar::Double2d & a, const floam::lidar::Double2d & b)
        { 
          return a.diffTotal < b.diffTotal;
        });

      // determine if point is an edge or a surface
      for (int k = subCloudCurvature.size() - 1; k >= 0; k--) {
        // get index of point
        index = subCloudCurvature[k].id;
        pcl::PointXYZL tempPointL;
        tempPointL.x = lidarScans[i]->points[index].x;
        tempPointL.y = lidarScans[i]->points[index].y;
        tempPointL.z = lidarScans[i]->points[index].z;

        // determine if point is an edge or surface
        if (subCloudCurvature[k].diffTotal <= m_settings.common.limits.edgeThreshold)
        {
          // value is smaller than threshold, so it is not an edge and assume its a surface
          tempPointL.label = 0;
        } else {
          tempPointL.label = 1;
        }
        edges->push_back(tempPointL);
      }
    }
  }
}

/// Imager type
template <>
void Lidar<floam::lidar::Imager>::detectEdges(
  const pcl::PointCloud<pcl::PointXYZ>::Ptr & points,
  pcl::PointCloud<pcl::PointXYZL>::Ptr & edges)
{
  // pointcloud in has to be organized (i.e. height and width)
  pcl::OrganizedEdgeBase <pcl::PointXYZ, pcl::Label> edgeDetector;
  pcl::PointCloud<pcl::Label>::Ptr labels(new pcl::PointCloud<pcl::Label> ());

  std::vector<pcl::PointIndices> indicies;
  edgeDetector.setInputCloud(points);

  // TODO(flynneva): make these adjustable
  edgeDetector.setDepthDisconThreshold(0.02f);
  edgeDetector.setMaxSearchNeighbors(50);

  // calculate edges
  edgeDetector.compute(*labels, indicies);
  // combine xyz cloud with labels
  pcl::concatenateFields(*points, *labels, *edges);
}

/// Scanner type
template <>
void Lidar<floam::lidar::Scanner>::detectSurfaces(
  const pcl::PointCloud<pcl::PointXYZ>::Ptr & points,
  pcl::PointCloud<pcl::PointNormal>::Ptr & normals)
{
  // Create the normal estimation class, and pass the input pointcloud to it
  pcl::NormalEstimation<pcl::PointXYZ, pcl::Normal> normalDetector;
  pcl::PointCloud<pcl::Normal>::Ptr normalCloud(new pcl::PointCloud<pcl::Normal> ());
  normalDetector.setInputCloud(points);

  // Create an empty kdtree representation, and pass it to the normal estimation object.
  // Its content will be filled inside the object, based on the given input dataset (as no other search surface is given).
  pcl::search::KdTree<pcl::PointXYZ>::Ptr tree (new pcl::search::KdTree<pcl::PointXYZ> ());
  normalDetector.setSearchMethod (tree);
  // Use all neighbors in a sphere of radius 3cm
  normalDetector.setRadiusSearch (0.03);

  // Compute the features
  normalDetector.compute(*normalCloud);
  // combine xyz cloud with normals
  pcl::concatenateFields(*points, *normalCloud, *normals);
}

/// overloaded for Imager type
template <>
void Lidar<floam::lidar::Imager>::detectSurfaces(
  const pcl::PointCloud<pcl::PointXYZ>::Ptr & points,
  pcl::PointCloud<pcl::PointNormal>::Ptr & normals)
{
  pcl::IntegralImageNormalEstimation<pcl::PointXYZ, pcl::Normal> normalDetector;
  pcl::PointCloud<pcl::Normal> normalCloud;

  // only for structured pointclouds
  normalDetector.setNormalEstimationMethod(normalDetector.AVERAGE_3D_GRADIENT);
  // TODO(flynneva): make these adjustable
  normalDetector.setMaxDepthChangeFactor(0.02f);
  normalDetector.setNormalSmoothingSize(7.0f);
  normalDetector.setInputCloud(points);

  normalDetector.compute(normalCloud);

  // combine xyz cloud with normals
  pcl::concatenateFields(*points, normalCloud, *normals);
}

}  // namespace lidar
}  // namespace floam
