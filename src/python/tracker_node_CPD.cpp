#include <pcl/ros/conversions.h>
#include <pcl/common/transforms.h>
#include <pcl/point_cloud.h>
#include <pcl/kdtree/kdtree_flann.h>
#include <pcl/io/pcd_io.h>
#include <ros/ros.h>
#include <sensor_msgs/PointCloud2.h>
#include <sensor_msgs/Image.h>
#include <tf/tf.h>
#include <tf/transform_listener.h>
#include <cv_bridge/cv_bridge.h>
#include <opencv/cv.h>
#include <bulletsim_msgs/TrackedObject.h>
#include <sys/timeb.h>

////////////////////////////
#include <pcl_conversions/pcl_conversions.h>
#include "simulation/config_bullet.h"

#include "clouds/utils_ros.h"
#include "clouds/utils_pcl.h"
#include "utils_tracking.h"
#include "utils/logging.h"
#include "utils/utils_vector.h"
#include "visibility.h"
#include "physics_tracker.h"
#include "feature_extractor.h"
#include "initialization.h"
#include "simulation/simplescene.h"
#include "config_tracking.h"
#include "utils/conversions.h"
#include "clouds/cloud_ops.h"
#include "simulation/util.h"
#include "clouds/utils_cv.h"
#include "simulation/recording.h"
#include "cam_sync.h"

#include "simulation/config_viewer.h"

using sensor_msgs::PointCloud2;
using sensor_msgs::Image;
using namespace std;
using namespace Eigen;

namespace cv {
typedef Vec<uchar, 3> Vec3b;
}

int nCameras;

vector<cv::Mat> rgb_images;
vector<cv::Mat> mask_images;
vector<cv::Mat> depth_images;
vector<CoordinateTransformer*> transformers;

ColorCloudPtr filteredCloud(new ColorCloud()); // filtered cloud in ground frame
bool pending = false; // new message received, waiting to be processed
bool firstCallback = true;

tf::TransformListener* listener;

string type2str(int type) {
    string r;

    uchar depth = type & CV_MAT_DEPTH_MASK;
    uchar chans = 1 + (type >> CV_CN_SHIFT);

    switch ( depth ) {
        case CV_8U:  r = "8U"; break;
        case CV_8S:  r = "8S"; break;
        case CV_16U: r = "16U"; break;
        case CV_16S: r = "16S"; break;
        case CV_32S: r = "32S"; break;
        case CV_32F: r = "32F"; break;
        case CV_64F: r = "64F"; break;
        default:     r = "User"; break;
    }

    r += "C";
    r += (chans+'0');

    return r;
}

void callback(const vector<sensor_msgs::PointCloud2ConstPtr>& cloud_msg, const vector<sensor_msgs::ImageConstPtr>& image_msgs) {
	if (rgb_images.size()!=nCameras) rgb_images.resize(nCameras);
	if (mask_images.size()!=nCameras) mask_images.resize(nCameras);
	if (depth_images.size()!=nCameras) depth_images.resize(nCameras);

	assert(image_msgs.size() == 2*nCameras);
	for (int i=0; i<nCameras; i++) {
		// merge all the clouds progressively
		ColorCloudPtr cloud(new ColorCloud);
		pcl::fromROSMsg(*cloud_msg[i], *cloud);
		pcl::transformPointCloud(*cloud, *cloud, transformers[i]->worldFromCamEigen);

		if (i==0) *filteredCloud = *cloud;
		else *filteredCloud = *filteredCloud + *cloud;

//        std::cout << "data length:" << image_msgs[2*i]->data.size() << std::endl;
//        char temp;
//        for(size_t i = 0; i < 424; i++){
//            for(size_t j = 0; j < 512; j++){
//                for(size_t c = 0; c < 4; c++){
//                    temp = image_msgs[2*i]->data.at(c + 4 * j + 2048 * i);
//                }
//                std::cout << j << std::endl;
//            }
//            std::cout << i << std::endl;
//        }

		depth_images[i] = cv_bridge::toCvCopy(image_msgs[2*i])->image;
//        std::cout << type2str(depth_images[i].type()) << depth_images[i].rows << ' ' << depth_images[i].cols << std::endl;
        boost::shared_ptr<cv_bridge::CvImage> debug_image_ptr = cv_bridge::toCvCopy(image_msgs[2*i+1]);
		extractImageAndMask(debug_image_ptr->image, rgb_images[i], mask_images[i]);

	}

	if (firstCallback) {
		filteredCloud = downsampleCloud(filteredCloud, 0.01*METERS);   //give a dense point cloud for better rope initialization
		firstCallback = false;
	} else {
		filteredCloud = downsampleCloud(filteredCloud, TrackingConfig::downsample*METERS);
	}
	//filteredCloud = filterZ(filteredCloud, -0.1*METERS, 0.20*METERS);
	pending = true;
}

int main(int argc, char* argv[]) {
	//////////////Eigen::internal::setNbThreads(2);
	Eigen::setNbThreads(2);

	GeneralConfig::scale = 100;
	BulletConfig::maxSubSteps = 0;
	BulletConfig::gravity = btVector3(0,0,-0.1);

	Parser parser;
	parser.addGroup(TrackingConfig());
	parser.addGroup(GeneralConfig());
	parser.addGroup(BulletConfig());
	parser.addGroup(ViewerConfig());
	parser.addGroup(RecordingConfig());
	parser.read(argc, argv);

	nCameras = TrackingConfig::cameraTopics.size();

	ros::init(argc, argv,"tracker_node");
	ros::NodeHandle nh;

	listener = new tf::TransformListener();

	for (int i=0; i<nCameras; i++){
		std::cout << TrackingConfig::cameraTopics[i] << std::endl;
		transformers.push_back(new CoordinateTransformer(waitForAndGetTransform(*listener, "/ground", TrackingConfig::cameraTopics[i]+"_rgb_optical_frame")));
	}

	vector<string> cloud_topics;
	vector<string> image_topics;
	for (int i=0; i<nCameras; i++) {
		cloud_topics.push_back("/preprocessor" + TrackingConfig::cameraTopics[i] + "/points");
		image_topics.push_back("/preprocessor" + TrackingConfig::cameraTopics[i] + "/image");
		image_topics.push_back("/preprocessor" + TrackingConfig::cameraTopics[i] + "/depth");
	}
	synchronizeAndRegisterCallback(cloud_topics, image_topics, nh, callback);

	ros::Publisher objPub = nh.advertise<bulletsim_msgs::TrackedObject>(trackedObjectTopic,10);

	// wait for first message, then initialize
	while (!pending) {
		ros::spinOnce();
		sleep(.001);
		if (!ros::ok()) throw runtime_error("caught signal while waiting for first message");
	}

	// set up scene
	Scene scene;
	util::setGlobalEnv(scene.env);

	if (TrackingConfig::record_camera_pos_file != "" &&
			TrackingConfig::playback_camera_pos_file != "") {
		throw runtime_error("can't both record and play back camera positions");
	}
	CamSync camsync(scene);
	if (TrackingConfig::record_camera_pos_file != "") {
		camsync.enable(CamSync::RECORD, TrackingConfig::record_camera_pos_file);
	} else if (TrackingConfig::playback_camera_pos_file != "") {
		camsync.enable(CamSync::PLAYBACK, TrackingConfig::playback_camera_pos_file);
	}

	//ViewerConfig::cameraHomePosition = transformers[0]->worldFromCamUnscaled.getOrigin() + btVector3(0,0,0);
	//ViewerConfig::cameraHomeCenter = ViewerConfig::cameraHomePosition + transformers[0]->worldFromCamUnscaled.getBasis().getColumn(2);
	//ViewerConfig::cameraHomeUp = -transformers[0]->worldFromCamUnscaled.getBasis().getColumn(1);
	ViewerConfig::cameraHomePosition = btVector3(0,0,1);
    ViewerConfig::cameraHomeCenter = btVector3(0,0,0);
    ViewerConfig::cameraHomeUp = btVector3(0,0,0);
    // To change initialization view, turn virtual object certain degree clockwise
    if (TrackingConfig::viewDirection == 90) {
        ViewerConfig::cameraHomeUp = btVector3(-1,0,0);
    } else if (TrackingConfig::viewDirection == 180) {
    	ViewerConfig::cameraHomeUp = btVector3(0,-1,0);
    } else if (TrackingConfig::viewDirection == 270) {
    	ViewerConfig::cameraHomeUp = btVector3(1,0,0);
    }
	scene.startViewer();

	TrackedObject::Ptr trackedObj = callInitServiceAndCreateObject(filteredCloud, rgb_images[0], mask_images[0], transformers[0]);
	if (!trackedObj) throw runtime_error("initialization of object failed.");
	trackedObj->init();
	scene.env->add(trackedObj->m_sim);


	// actual tracking algorithm
	MultiVisibility::Ptr visInterface(new MultiVisibility());
	for (int i=0; i<nCameras; i++) {
		if (trackedObj->m_type == "rope") // Don't do self-occlusion if the trackedObj is a rope
			visInterface->addVisibility(DepthImageVisibility::Ptr(new DepthImageVisibility(transformers[i])));
		else
			visInterface->addVisibility(AllOcclusionsVisibility::Ptr(new AllOcclusionsVisibility(scene.env->bullet->dynamicsWorld, transformers[i])));
	}

	TrackedObjectFeatureExtractor::Ptr objectFeatures(new TrackedObjectFeatureExtractor(trackedObj));
	CloudFeatureExtractor::Ptr cloudFeatures(new CloudFeatureExtractor());
	PhysicsTracker::Ptr alg(new PhysicsTracker(objectFeatures, cloudFeatures, visInterface));
	PhysicsTrackerVisualizer::Ptr trackingVisualizer(new PhysicsTrackerVisualizer(&scene, alg));

	bool applyEvidence = true;
	scene.addVoidKeyCallback('a',boost::bind(toggle, &applyEvidence), "apply evidence");
	scene.addVoidKeyCallback('=',boost::bind(&EnvironmentObject::adjustTransparency, trackedObj->getSim(), 0.1f), "increase opacity");
	scene.addVoidKeyCallback('-',boost::bind(&EnvironmentObject::adjustTransparency, trackedObj->getSim(), -0.1f), "decrease opacity");
	bool exit_loop = false;
	scene.addVoidKeyCallback('q',boost::bind(toggle, &exit_loop), "exit");

	boost::shared_ptr<ScreenThreadRecorder> screen_recorder;
	boost::shared_ptr<ImageTopicRecorder> image_topic_recorder;
	if (RecordingConfig::record == RECORD_RENDER_ONLY) {
		screen_recorder.reset(new ScreenThreadRecorder(scene.viewer, RecordingConfig::dir + "/" +  RecordingConfig::video_file + "_tracked.avi"));
	} else if (RecordingConfig::record == RECORD_RENDER_AND_TOPIC) {
		screen_recorder.reset(new ScreenThreadRecorder(scene.viewer, RecordingConfig::dir + "/" +  RecordingConfig::video_file + "_tracked.avi"));
		image_topic_recorder.reset(new ImageTopicRecorder(nh, image_topics[0], RecordingConfig::dir + "/" +  RecordingConfig::video_file + "_topic.avi"));
	}

	scene.setSyncTime(false);
	scene.setDrawing(true);

	while (!exit_loop && ros::ok()) {
		//Update the inputs of the featureExtractors and visibilities (if they have any inputs)
        //cloudFeatures->updateInputs(filteredCloud, rgb_images[0], transformers[0]);
        cloudFeatures->updateInputs(filteredCloud);
		for (int i=0; i<nCameras; i++)
			visInterface->visibilities[i]->updateInput(depth_images[i]);

		alg->updateFeatures();
		Eigen::MatrixXf estPos_next = alg->CPDupdate();

		pending = false;

		while (ros::ok() && !pending) {

			//Do iteration
			alg->updateFeatures();
			objectFeatures->m_obj->CPDapplyEvidence(toBulletVectors(estPos_next));
			trackingVisualizer->update();
			scene.step(.03, 2, .015);

			ros::spinOnce();
		}
		objPub.publish(toTrackedObjectMessage(trackedObj));
	}

//	while (!exit_loop && ros::ok()) {
//
//		trackingVisualizer->update();
//		scene.step(.03,2,.015);
//		ros::spinOnce();
//
//	}

//	struct timeb Time1, Time2;
//	ftime(&Time1);
//	ftime(&Time2);
//	std::cout << "Bullet Time:" << (Time2.time-Time1.time)*1000 + (Time2.millitm - Time1.millitm) << "ms" << std::endl;

}
